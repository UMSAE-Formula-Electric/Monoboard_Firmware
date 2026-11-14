/**
 * @file cli_core.c
 * @brief Logic of the debug CLI (issue #51). See cli_core.h.
 */
#include "cli_core.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "time_if.h"

#define ASCII_CTRL_C    0x03U
#define ASCII_BACKSPACE 0x08U
#define ASCII_LF        0x0AU
#define ASCII_CR        0x0DU
#define ASCII_SPACE     0x20U
#define ASCII_TILDE     0x7EU
#define ASCII_DELETE    0x7FU

/* Longest text one input byte can append outside a reply: "^C" + CR LF. */
#define ECHO_MAX_BYTES 4U

/* Append to the reply body, which stops CLI_OUT_RESERVE short of the end. */
static void out_body(CliOut *out, const char *text)
{
    const size_t size  = strlen(text);
    const size_t limit = out->cap - CLI_OUT_RESERVE;

    if ((out->len <= limit) && (size <= (limit - out->len))) {
        (void)memcpy(&out->buf[out->len], text, size);
        out->len += size;
    }
}

/* Append to the reserved tail (verdict line, prompt), clipping if needed. */
static void out_tail(CliOut *out, const char *text)
{
    size_t size = strlen(text);

    if (size > (out->cap - out->len)) {
        size = out->cap - out->len;
    }
    (void)memcpy(&out->buf[out->len], text, size);
    out->len += size;
}

static void line_reset(CliCore *core)
{
    core->line_len = 0U;
    core->line[0]  = '\0';
    core->overflow = false;
}

/* Copy at most size - 1 characters and always terminate. */
static void copy_text(char *dest, size_t size, const char *source)
{
    size_t len = 0U;

    while ((len < (size - 1U)) && (source[len] != '\0')) {
        dest[len] = source[len];
        len++;
    }
    dest[len] = '\0';
}

static void log_add(CliCore *core, CliVerdict verdict, const char *text)
{
    CliLogEntry *entry = &core->log[core->log_next];

    entry->time_ms = now_ms();
    entry->verdict = verdict;
    copy_text(entry->text, sizeof(entry->text), text);

    core->log_next = (core->log_next + 1U) % CLI_LOG_DEPTH;
    if (core->log_count < CLI_LOG_DEPTH) {
        core->log_count++;
    }
}

/* Split core->line on spaces, in place. Returns the word count, or
 * CLI_MAX_ARGS + 1 if there are more words than argv can hold. */
static size_t split_words(CliCore *core, char *argv[])
{
    size_t count = 0U;
    size_t pos   = 0U;

    while (pos < core->line_len) {
        while ((pos < core->line_len) && (core->line[pos] == ' ')) {
            pos++;
        }
        if (pos >= core->line_len) {
            break;
        }
        if (count == CLI_MAX_ARGS) {
            return CLI_MAX_ARGS + 1U;
        }
        argv[count] = &core->line[pos];
        count++;
        while ((pos < core->line_len) && (core->line[pos] != ' ')) {
            pos++;
        }
        core->line[pos] = '\0'; /* pos <= line_len, which is inside line[] */
        pos++;
    }
    return count;
}

/* How many leading words of argv spell this row's name; 0 if not all of the
 * name's words are there. */
/* cppcheck-suppress constParameter ; argv is the handler's char *argv[] */
static size_t match_words(const char *name, size_t argc, char *const argv[])
{
    size_t      words = 0U;
    const char *rest  = name;

    while (*rest != '\0') {
        const char  *space = strchr(rest, ' ');
        const size_t size  = (space != NULL) ? (size_t)(space - rest) : strlen(rest);

        if ((words >= argc) || (strlen(argv[words]) != size)
            || (strncmp(argv[words], rest, size) != 0)) {
            return 0U;
        }
        words++;
        rest = (space != NULL) ? (space + 1) : (rest + size);
    }
    return words;
}

/* The row matching the most leading words, searched in both tables. */
static const CliCommand *find_command(size_t argc, char *const argv[], size_t *name_words)
{
    const CliCommand *const tables[2] = {cli_commands, cli_debug_commands};
    const size_t            counts[2] = {cli_command_count, cli_debug_command_count};
    const CliCommand       *best      = NULL;

    *name_words = 0U;
    for (size_t table = 0U; table < 2U; table++) {
        for (size_t row = 0U; row < counts[table]; row++) {
            const size_t words = match_words(tables[table][row].name, argc, argv);

            if (words > *name_words) {
                *name_words = words;
                best        = &tables[table][row];
            }
        }
    }
    return best;
}

static bool vehicle_is_safe(const CliCore *core)
{
    /* Fail closed: no ports, or no answer, means "not safe". */
    return (core->ports != NULL) && (core->ports->is_safe_state != NULL)
           && core->ports->is_safe_state();
}

/* Run one found command and write its verdict line. */
static CliVerdict
run_command(CliCore *core, const CliCommand *command, int argc, char *argv[], CliOut *out)
{
    uint32_t started_us;
    uint32_t elapsed_us;
    bool     ok;

    /* The one place writes are gated (issue #51): never per handler. */
    if (((command->flags & CLI_FLAG_WRITE) != 0U) && (!vehicle_is_safe(core))) {
        out_tail(out, "ERR refused: vehicle not in safe state" CLI_EOL);
        return CLI_VERDICT_REFUSED;
    }

    core->error = NULL;
    started_us  = now_us();
    ok          = command->handler(core, argc, argv, out);
    elapsed_us  = now_us() - started_us; /* unsigned: correct across a wrap */
    if (elapsed_us > core->worst_handler_us) {
        core->worst_handler_us   = elapsed_us;
        core->worst_handler_name = command->name;
    }

    if (out->truncated) {
        out_tail(out, "ERR output truncated" CLI_EOL);
        return CLI_VERDICT_ERR;
    }
    if (!ok) {
        out_tail(out, "ERR ");
        out_tail(out, (core->error != NULL) ? core->error : "failed");
        out_tail(out, CLI_EOL);
        return CLI_VERDICT_ERR;
    }
    out_tail(out, "OK" CLI_EOL);
    return CLI_VERDICT_OK;
}

/* A complete, non-empty line is in core->line: look it up, run it, log it. */
static void dispatch_line(CliCore *core, CliOut *out)
{
    char   text[CLI_LOG_TEXT_SIZE];
    char  *argv[CLI_MAX_ARGS];
    size_t name_words = 0U;

    /* Keep the text for the log: split_words() cuts the line up. */
    copy_text(text, sizeof(text), core->line);

    const size_t argc = split_words(core, argv);

    if (argc == 0U) {
        return; /* only spaces: just a fresh prompt */
    }
    if (argc > CLI_MAX_ARGS) {
        out_tail(out, "ERR too many words" CLI_EOL);
        log_add(core, CLI_VERDICT_ERR, text);
        return;
    }

    const CliCommand *command = find_command(argc, argv, &name_words);

    if (command == NULL) {
        out_tail(out, "ERR unknown command, try help" CLI_EOL);
        log_add(core, CLI_VERDICT_UNKNOWN, text);
        return;
    }
    log_add(core, run_command(core, command, (int)(argc - name_words), &argv[name_words], out),
            text);
}

/* The line has ended: answer it, then print the prompt. */
static void handle_line(CliCore *core, CliOut *out)
{
    if (core->overflow) {
        out_tail(out, "ERR line too long (max 79)" CLI_EOL);
        log_add(core, CLI_VERDICT_ERR, "(overlong line)");
    } else if (core->line_len > 0U) {
        dispatch_line(core, out);
    } else {
        /* Empty line: just a fresh prompt. */
    }
    out_tail(out, CLI_PROMPT);
    out->reply_ready = true;
}

static void end_of_line(CliCore *core, CliOut *out)
{
    out_body(out, CLI_EOL);
    handle_line(core, out);
    line_reset(core);
}

static void store_char(CliCore *core, CliOut *out, uint8_t byte)
{
    if (core->overflow) {
        return; /* discarding until the end of the line */
    }
    if (core->line_len >= CLI_LINE_MAX_CHARS) {
        core->overflow = true; /* checked before the store: never past the end */
        return;
    }
    core->line[core->line_len] = (char)byte;
    core->line_len++;
    core->line[core->line_len] = '\0';
    out->buf[out->len]         = (char)byte;
    out->len++;
}

static void erase_char(CliCore *core, CliOut *out)
{
    if ((!core->overflow) && (core->line_len > 0U)) {
        core->line_len--;
        core->line[core->line_len] = '\0';
        out_body(out, "\b \b");
    }
}

void cli_core_init(CliCore *core)
{
    if (core != NULL) {
        (void)memset(core, 0, sizeof(*core));
        line_reset(core);
    }
}

void cli_core_set_ports(CliCore *core, const CliPorts *ports)
{
    if (core != NULL) {
        core->ports = ports;
    }
}

void cli_core_set_boot_report(CliCore *core, const char *text)
{
    if (core != NULL) {
        core->boot_report = text;
    }
}

void cli_out_init(CliOut *out, char *buf, size_t cap)
{
    if (out != NULL) {
        out->buf         = buf;
        out->cap         = cap;
        out->len         = 0U;
        out->reply_ready = false;
        out->truncated   = false;
    }
}

void cli_out_printf(CliOut *out, const char *format, ...)
{
    va_list args;
    size_t  room;
    int     written;

    if ((out == NULL) || (out->buf == NULL) || (format == NULL) || (out->cap < CLI_OUT_RESERVE)
        || (out->len > (out->cap - CLI_OUT_RESERVE))) {
        return;
    }
    room = (out->cap - CLI_OUT_RESERVE) - out->len;

    va_start(args, format);
    /* False positive: args is initialised by va_start() on the line above. */
    /* NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized) */
    written = vsnprintf(&out->buf[out->len], room, format, args);
    va_end(args);

    /* vsnprintf also needs room for its NUL, hence >= and not >. */
    if ((written < 0) || ((size_t)written >= room)) {
        out->truncated = true;
    } else {
        out->len += (size_t)written;
    }
}

bool cli_fail(CliCore *core, const char *reason)
{
    if (core != NULL) {
        core->error = reason;
    }
    return false;
}

size_t cli_core_feed(CliCore *core, const uint8_t *bytes, size_t len, CliOut *out)
{
    size_t consumed = 0U;

    if ((core == NULL) || (bytes == NULL) || (out == NULL) || (out->buf == NULL)
        || (out->cap < CLI_OUT_RESERVE)) {
        return 0U;
    }

    while ((consumed < len) && (!out->reply_ready)) {
        const uint8_t byte   = bytes[consumed];
        const bool    was_cr = core->last_was_cr;

        /* Room for this byte's echo, short of the reserved tail? If not,
         * leave the byte with the caller instead of dropping the echo. */
        if ((out->len + ECHO_MAX_BYTES) > (out->cap - CLI_OUT_RESERVE)) {
            break;
        }

        consumed++;
        core->last_was_cr = (byte == ASCII_CR);

        if (byte == ASCII_CR) {
            end_of_line(core, out);
        } else if (byte == ASCII_LF) {
            if (!was_cr) { /* the LF of a CRLF already ended this line */
                end_of_line(core, out);
            }
        } else if ((byte == ASCII_BACKSPACE) || (byte == ASCII_DELETE)) {
            erase_char(core, out);
        } else if (byte == ASCII_CTRL_C) {
            line_reset(core);
            out_body(out, "^C" CLI_EOL);
            out_tail(out, CLI_PROMPT);
        } else if ((byte >= ASCII_SPACE) && (byte <= ASCII_TILDE)) {
            store_char(core, out, byte);
        } else {
            /* Other control bytes and non-ASCII: ignored. */
        }
    }

    return consumed;
}
