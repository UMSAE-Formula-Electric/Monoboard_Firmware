/**
 * @file cli_core.h
 * @brief Logic of the debug CLI (issue #51): line editor, command table
 * lookup, the central safe-state check and the command log.
 *
 * Turns a stream of received bytes into complete command lines, runs the
 * matching command, and writes the text to send back (echo, reply, prompt)
 * into a caller-owned buffer. cli_service.c moves the bytes between the UART
 * and this module; the unit tests call it directly.
 *
 * This file does not use the RTOS, the HAL or the heap, so the same code
 * runs on the target and in the desktop unit tests. It is not re-entrant:
 * feed one CliCore from one task only.
 *
 * Memory: sizeof(CliCore) is about 0.8 KB (line buffer plus the command log),
 * static. Timing: cli_core_feed() is O(len) plus one command handler per
 * call at most; the worst handler time is measured and kept in the core.
 */
#ifndef MONOBOARD_CLI_CORE_H
#define MONOBOARD_CLI_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gpio_if.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Line buffer size in bytes, including the terminating NUL. */
#define CLI_LINE_SIZE 80U

/** Longest accepted command line, in characters. */
#define CLI_LINE_MAX_CHARS (CLI_LINE_SIZE - 1U)

/**
 * Bytes of a CliOut kept back for the verdict line and the prompt, so a
 * reply that fills the buffer can still end properly. A CliOut smaller than
 * this cannot hold a reply at all.
 */
#define CLI_OUT_RESERVE 64U

/** Prompt printed after every reply. */
#define CLI_PROMPT "mb> "

/** End of every output line. */
#define CLI_EOL "\r\n"

/**
 * Most words accepted on one line, command name included. Sized to the
 * longest command in the tables, `cal set <name> <value>`: 4 words. Raise it
 * together with the command that needs more.
 */
#define CLI_MAX_ARGS 4U

/** Command lines remembered by the log. */
#define CLI_LOG_DEPTH 16U

/** Characters of a line kept in the log, plus the terminating NUL. */
#define CLI_LOG_TEXT_SIZE 33U

/** CliCommand.flags: the command changes something on the vehicle. */
#define CLI_FLAG_WRITE 0x01U

/** Text to transmit, written by the core into a caller-owned buffer. */
typedef struct {
    char  *buf;         /**< destination; not NUL-terminated */
    size_t cap;         /**< capacity of @ref buf in bytes */
    size_t len;         /**< bytes written so far */
    bool   reply_ready; /**< a command line was completed and answered */
    bool   truncated;   /**< a handler wrote more than fits */
} CliOut;

/**
 * What the CLI needs from the rest of the system, filled in by the
 * composition root. The CLI can only reach what is in here.
 */
typedef struct {
    /** Mandatory. True only when the vehicle is known to be parked with the
     * tractive system off. Write commands are refused otherwise. */
    bool (*is_safe_state)(void);
    /** Optional. Pins for the debug-only `set` command; NULL disables it. */
    const GpioIf *gpio;
} CliPorts;

/** How a logged command line ended. */
typedef enum {
    CLI_VERDICT_OK = 0,
    CLI_VERDICT_ERR,
    CLI_VERDICT_REFUSED,
    CLI_VERDICT_UNKNOWN,
} CliVerdict;

/** One remembered command line. */
typedef struct {
    uint32_t   time_ms;                 /**< now_ms() when the line ended */
    CliVerdict verdict;                 /**< how it ended */
    char       text[CLI_LOG_TEXT_SIZE]; /**< start of the line, NUL-terminated */
} CliLogEntry;

struct CliCore;

/**
 * A command. Writes `key=value` lines with cli_out_printf() and returns true
 * for `OK`, or returns cli_fail() for `ERR <reason>`.
 * @param core  CLI state (ports, log, counters)
 * @param argc  number of arguments after the command name
 * @param argv  the arguments
 * @param out   where the reply body goes
 */
typedef bool (*CliHandler)(struct CliCore *core, int argc, char *argv[], CliOut *out);

/** One row of a command table. Adding a command is adding a row. */
typedef struct {
    const char *name;    /**< one or two words: "faults", "faults clear" */
    const char *help;    /**< one line, printed by `help` */
    CliHandler  handler; /**< what runs */
    uint8_t     flags;   /**< #CLI_FLAG_WRITE or 0 */
} CliCommand;

/** State of one CLI. Treat as opaque; use the functions below. */
typedef struct CliCore {
    char   line[CLI_LINE_SIZE]; /**< characters of the line being typed */
    size_t line_len;            /**< characters stored in @ref line */
    bool   overflow;            /**< line exceeded the limit; discarding */
    bool   last_was_cr;         /**< previous byte was CR: swallow one LF */

    const CliPorts *ports;       /**< may be NULL: every write is refused */
    const char     *boot_report; /**< text for `boot`, or NULL */
    const char     *error;       /**< reason set by cli_fail() */

    CliLogEntry log[CLI_LOG_DEPTH]; /**< ring of the last command lines */
    size_t      log_next;           /**< where the next entry goes */
    size_t      log_count;          /**< entries in use */

    uint32_t    worst_handler_us;   /**< longest handler run so far */
    const char *worst_handler_name; /**< the command that took it */
    uint32_t    rx_dropped;         /**< received bytes lost; set by cli_service.c */
    uint32_t    tx_dropped;         /**< replies the UART refused; set by cli_service.c */
} CliCore;

/** Commands present in every build (cli_commands.c). */
extern const CliCommand cli_commands[];
/** Number of rows in #cli_commands. */
extern const size_t cli_command_count;

/** Output-forcing commands: Debug builds only, empty otherwise. */
extern const CliCommand cli_debug_commands[];
/** Number of rows in #cli_debug_commands; 0 outside Debug builds. */
extern const size_t cli_debug_command_count;

/**
 * Reset @p core: empty line, empty log, no ports, no boot report.
 * @param core  CLI state to initialise (may be NULL: no-op)
 */
void cli_core_init(CliCore *core);

/**
 * Give the core its ports. Without them every write command is refused.
 * @param core   CLI state
 * @param ports  ports that outlive @p core, or NULL
 */
void cli_core_set_ports(CliCore *core, const CliPorts *ports);

/**
 * Set the text printed by `boot`.
 * @param core  CLI state
 * @param text  NUL-terminated text that outlives @p core, or NULL
 */
void cli_core_set_boot_report(CliCore *core, const char *text);

/**
 * Point @p out at an empty buffer.
 * @param out  output descriptor to initialise (may be NULL: no-op)
 * @param buf  destination buffer
 * @param cap  capacity of @p buf in bytes
 */
void cli_out_init(CliOut *out, char *buf, size_t cap);

/**
 * Append formatted text to the reply body. If it does not fit, nothing is
 * written and the reply ends with `ERR output truncated`. Integers and
 * strings only: the target's printf has no float support.
 * @param out     reply being built
 * @param format  printf format, followed by its arguments
 */
void cli_out_printf(CliOut *out, const char *format, ...) __attribute__((format(printf, 2, 3)));

/**
 * Fail the running command with a reason.
 * @param core    CLI state
 * @param reason  short static text shown after `ERR `
 * @return false, so a handler can `return cli_fail(core, "...")`
 */
bool cli_fail(CliCore *core, const char *reason);

/**
 * Feed received bytes to the CLI.
 *
 * Printable characters are stored and echoed, backspace (0x08, 0x7F) removes
 * one, Ctrl-C (0x03) clears the line, and CR, LF or CRLF ends it. A line
 * longer than #CLI_LINE_MAX_CHARS is discarded up to its end and answered
 * with an error. Every other byte is ignored. A complete line is looked up
 * in the command tables, checked against the safe state if it is a write,
 * run, answered with `OK` or `ERR <reason>`, and logged.
 *
 * Stops early in two cases, so nothing is ever dropped: after one line has
 * been answered (@p out->reply_ready is set), and when @p out has no room
 * left for the next byte's echo. In both cases the caller transmits @p out,
 * then calls again with the bytes that were not consumed.
 *
 * @param core   CLI state
 * @param bytes  received bytes
 * @param len    number of bytes in @p bytes
 * @param out    where echo and replies are appended
 * @return number of bytes consumed from @p bytes, at most @p len; 0 if any
 *         pointer is NULL or @p out is smaller than #CLI_OUT_RESERVE
 */
size_t cli_core_feed(CliCore *core, const uint8_t *bytes, size_t len, CliOut *out);

#ifdef __cplusplus
}
#endif

#endif /* MONOBOARD_CLI_CORE_H */
