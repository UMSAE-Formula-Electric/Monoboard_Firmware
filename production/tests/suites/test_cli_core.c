/**
 * @file test_cli_core.c
 * @brief Exercises the CLI line editor, dispatcher, safe-state check and
 * command log (production/services/cli_core.h).
 */
#include <stdint.h>
#include <string.h>

#include "cli_core.h"
#include "gpio_fake.h"
#include "test_framework.h"

/* Large enough for the longest reply (`help`). */
#define OUT_SIZE 1024U

static CliCore core;
static CliOut  out;
static char    out_buf[OUT_SIZE + 1U];

static void setup(void)
{
    cli_core_init(&core);
    cli_out_init(&out, out_buf, OUT_SIZE);
}

/* Feed a C string; returns bytes consumed. */
static size_t feed(const char *text)
{
    return cli_core_feed(&core, (const uint8_t *)text, strlen(text), &out);
}

/* The output so far, as a C string. */
static const char *text_out(void)
{
    out_buf[out.len] = '\0';
    return out_buf;
}

TEST(cli_core, echoes_printable_characters)
{
    setup();
    CHECK(feed("abc") == 3U);
    CHECK(strcmp(text_out(), "abc") == 0);
    CHECK(out.reply_ready == false);
    CHECK(strcmp(core.line, "abc") == 0);
}

TEST(cli_core, cr_lf_and_crlf_each_end_one_line)
{
    setup();
    CHECK(feed("x\r") == 2U);
    CHECK(out.reply_ready == true);
    CHECK(strcmp(text_out(), "x\r\nERR unknown command, try help\r\nmb> ") == 0);

    setup();
    CHECK(feed("x\n") == 2U);
    CHECK(strcmp(text_out(), "x\r\nERR unknown command, try help\r\nmb> ") == 0);

    /* CRLF: the CR answers, the LF that follows is swallowed silently. */
    setup();
    CHECK(feed("x\r\n") == 2U);
    cli_out_init(&out, out_buf, OUT_SIZE);
    CHECK(feed("\n") == 1U);
    CHECK(out.len == 0U);
    CHECK(out.reply_ready == false);
}

TEST(cli_core, empty_line_prints_only_a_prompt)
{
    setup();
    CHECK(feed("\r") == 1U);
    CHECK(strcmp(text_out(), "\r\nmb> ") == 0);
    CHECK(out.reply_ready == true);
}

TEST(cli_core, stops_after_one_reply)
{
    setup();
    CHECK(feed("a\rb\r") == 2U); /* second command left with the caller */
    CHECK(out.reply_ready == true);
    CHECK(core.line_len == 0U);
}

TEST(cli_core, backspace_and_delete_erase_one_character)
{
    setup();
    CHECK(feed("ab\b") == 3U);
    CHECK(strcmp(core.line, "a") == 0);
    CHECK(strcmp(text_out(), "ab\b \b") == 0);
    CHECK(feed("\x7f") == 1U);
    CHECK(core.line_len == 0U);
    CHECK(feed("\b") == 1U); /* nothing left: no echo, no underflow */
    CHECK(strcmp(text_out(), "ab\b \b\b \b") == 0);
}

TEST(cli_core, ctrl_c_clears_the_line)
{
    setup();
    CHECK(feed("abc\x03") == 4U);
    CHECK(core.line_len == 0U);
    CHECK(strcmp(text_out(), "abc^C\r\nmb> ") == 0);
    CHECK(out.reply_ready == false);
}

TEST(cli_core, control_and_non_ascii_bytes_are_ignored)
{
    const uint8_t noise[] = {0x00U, 0x01U, 0x1BU, 0x80U, 0xFFU, (uint8_t)'a'};

    setup();
    CHECK(cli_core_feed(&core, noise, sizeof(noise), &out) == sizeof(noise));
    CHECK(strcmp(core.line, "a") == 0);
    CHECK(strcmp(text_out(), "a") == 0);
}

TEST(cli_core, line_of_exactly_79_characters_is_accepted)
{
    char text[CLI_LINE_MAX_CHARS + 1U];

    (void)memset(text, 'a', CLI_LINE_MAX_CHARS);
    text[CLI_LINE_MAX_CHARS] = '\0';

    setup();
    CHECK(feed(text) == CLI_LINE_MAX_CHARS);
    CHECK(core.line_len == CLI_LINE_MAX_CHARS);
    CHECK(core.overflow == false);
    CHECK(core.line[CLI_LINE_MAX_CHARS] == '\0');
}

/* Testing if an overlong (bigger than 79 characters) line is rejected, and the
 * next line works. */
TEST(cli_core, overlong_line_is_rejected_then_recovers)
{
    char   text[200];
    size_t fed = 0U;

    (void)memset(text, 'a', sizeof(text));

    setup();
    while (fed < sizeof(text)) {
        const size_t used = cli_core_feed(&core, (const uint8_t *)&text[fed], sizeof(text) - fed,
                                          &out);

        fed += used;
        if (used == 0U) { /* out is full of echo: "transmit" it and go on */
            cli_out_init(&out, out_buf, OUT_SIZE);
        }
    }
    CHECK(core.overflow == true);
    CHECK(core.line_len == CLI_LINE_MAX_CHARS);

    cli_out_init(&out, out_buf, OUT_SIZE);
    CHECK(feed("\r") == 1U);
    CHECK(strcmp(text_out(), "\r\nERR line too long (max 79)\r\nmb> ") == 0);

    cli_out_init(&out, out_buf, OUT_SIZE);
    CHECK(feed("ok\r") == 3U);
    CHECK(strcmp(text_out(), "ok\r\nERR unknown command, try help\r\nmb> ") == 0);
}

TEST(cli_core, full_output_stops_consuming_instead_of_dropping)
{
    char small[CLI_OUT_RESERVE + 5U];

    cli_core_init(&core);
    cli_out_init(&out, small, sizeof(small));
    CHECK(feed("abcdef") == 2U); /* room for 5 bytes, 4 kept per echo */
    CHECK(out.len == 2U);
    CHECK(core.line_len == 2U);

    cli_out_init(&out, small, sizeof(small));
    CHECK(feed("\r") == 1U); /* the reply still fits in the reserve */
    CHECK(out.reply_ready == true);
}

TEST(cli_core, rejects_bad_arguments)
{
    const uint8_t byte = (uint8_t)'a';
    char          tiny[CLI_OUT_RESERVE - 1U];
    CliOut        bad;

    setup();
    CHECK(cli_core_feed(NULL, &byte, 1U, &out) == 0U);
    CHECK(cli_core_feed(&core, NULL, 1U, &out) == 0U);
    CHECK(cli_core_feed(&core, &byte, 1U, NULL) == 0U);

    cli_out_init(&bad, tiny, sizeof(tiny));
    CHECK(cli_core_feed(&core, &byte, 1U, &bad) == 0U);
    cli_out_init(&bad, NULL, OUT_SIZE);
    CHECK(cli_core_feed(&core, &byte, 1U, &bad) == 0U);

    cli_core_init(NULL);
    cli_out_init(NULL, out_buf, OUT_SIZE);
}

/* ---- Dispatcher, safe-state check, log -------------------------------- */

static bool vehicle_safe;

static bool test_is_safe_state(void)
{
    return vehicle_safe;
}

static const CliPorts test_ports = {
    .is_safe_state = test_is_safe_state,
    .gpio          = &gpio_fake,
};

/* Fresh CLI with ports and a configured, low test pin. */
static void setup_with_ports(bool safe)
{
    const GpioConfig config = {
        .dir     = GPIO_DIR_OUTPUT,
        .pull    = GPIO_PULL_NONE,
        .initial = GPIO_LOW,
    };

    setup();
    gpio_fake_reset();
    (void)gpio_fake.init(GPIO_PIN_TEST, &config);
    vehicle_safe = safe;
    cli_core_set_ports(&core, &test_ports);
}

/* Run one line on a fresh output buffer; returns the output text. */
static const char *run(const char *line)
{
    cli_out_init(&out, out_buf, OUT_SIZE);
    (void)feed(line);
    return text_out();
}

static bool ends_with(const char *text, const char *tail)
{
    const size_t text_len = strlen(text);
    const size_t tail_len = strlen(tail);

    return (text_len >= tail_len) && (strcmp(&text[text_len - tail_len], tail) == 0);
}

TEST(cli_core, help_lists_every_command_and_ends_ok)
{
    const char *reply;

    setup();
    reply = run("help\r");
    CHECK(out.reply_ready == true);
    CHECK(out.truncated == false);
    for (size_t row = 0U; row < cli_command_count; row++) {
        CHECK(strstr(reply, cli_commands[row].name) != NULL);
    }
    for (size_t row = 0U; row < cli_debug_command_count; row++) {
        CHECK(strstr(reply, cli_debug_commands[row].name) != NULL);
    }
    CHECK(ends_with(reply, "\r\nOK\r\nmb> "));
}

TEST(cli_core, version_and_uptime_answer_key_value_lines)
{
    setup();
    CHECK(strstr(run("version\r"), "version=") != NULL);
    CHECK(ends_with(text_out(), "OK\r\nmb> "));
    CHECK(strstr(run("uptime\r"), "uptime_ms=") != NULL);
    CHECK(ends_with(text_out(), "OK\r\nmb> "));
}

TEST(cli_core, extra_spaces_between_words_are_ignored)
{
    setup();
    CHECK(strstr(run("  uptime  \r"), "uptime_ms=") != NULL);
    CHECK(strcmp(run("   \r"), "   \r\nmb> ") == 0);
}

TEST(cli_core, command_names_match_whole_words_only)
{
    setup();
    CHECK(ends_with(run("hel\r"), "ERR unknown command, try help\r\nmb> "));
    CHECK(ends_with(run("helpx\r"), "ERR unknown command, try help\r\nmb> "));
    CHECK(ends_with(run("HELP\r"), "ERR unknown command, try help\r\nmb> "));
}

TEST(cli_core, too_many_words_is_an_error)
{
    setup();
    /* CLI_MAX_ARGS words are accepted, one more is not. */
    CHECK(ends_with(run("cal set x 1\r"), "ERR refused: vehicle not in safe state\r\nmb> "));
    CHECK(ends_with(run("cal set x 1 2\r"), "ERR too many words\r\nmb> "));
    CHECK(ends_with(run("a b c d e f g\r"), "ERR too many words\r\nmb> "));
}

TEST(cli_core, placeholder_commands_name_the_issue_they_wait_on)
{
    setup();
    CHECK(ends_with(run("state\r"), "ERR unavailable: needs #25\r\nmb> "));
    CHECK(ends_with(run("faults\r"), "ERR unavailable: needs #59\r\nmb> "));
    CHECK(ends_with(run("cal\r"), "ERR unavailable: needs #61\r\nmb> "));
    CHECK(ends_with(run("selftest\r"), "ERR unavailable: needs #50\r\nmb> "));
}

TEST(cli_core, two_word_command_wins_over_its_first_word)
{
    /* `faults` is a read, `faults clear` a write: only the write is gated. */
    setup_with_ports(false);
    CHECK(ends_with(run("faults\r"), "ERR unavailable: needs #59\r\nmb> "));
    CHECK(ends_with(run("faults clear\r"), "ERR refused: vehicle not in safe state\r\nmb> "));

    setup_with_ports(true);
    CHECK(ends_with(run("faults clear\r"), "ERR unavailable: needs #59\r\nmb> "));
}

TEST(cli_core, write_is_refused_without_ports)
{
    setup(); /* no ports at all: fail closed */
    CHECK(ends_with(run("cal set x 1\r"), "ERR refused: vehicle not in safe state\r\nmb> "));
}

TEST(cli_core, set_is_refused_when_not_safe_and_pin_does_not_move)
{
    setup_with_ports(false);
    if (cli_debug_command_count > 0U) {
        CHECK(ends_with(run("set test 1\r"), "ERR refused: vehicle not in safe state\r\nmb> "));
        CHECK(gpio_fake_level(GPIO_PIN_TEST) == GPIO_LOW);
    }
}

TEST(cli_core, set_drives_the_pin_when_safe)
{
    setup_with_ports(true);
    if (cli_debug_command_count > 0U) {
        CHECK(strcmp(run("set test 1\r"), "set test 1\r\ntest=1\r\nOK\r\nmb> ") == 0);
        CHECK(gpio_fake_level(GPIO_PIN_TEST) == GPIO_HIGH);
        CHECK(strcmp(run("set test 0\r"), "set test 0\r\ntest=0\r\nOK\r\nmb> ") == 0);
        CHECK(gpio_fake_level(GPIO_PIN_TEST) == GPIO_LOW);
    }
}

TEST(cli_core, set_rejects_bad_arguments)
{
    setup_with_ports(true);
    if (cli_debug_command_count > 0U) {
        CHECK(ends_with(run("set\r"), "ERR usage: set <pin> <0|1>\r\nmb> "));
        CHECK(ends_with(run("set nope 1\r"), "ERR unknown pin, known: test\r\nmb> "));
        CHECK(ends_with(run("set test 2\r"), "ERR value must be 0 or 1\r\nmb> "));
        CHECK(gpio_fake_level(GPIO_PIN_TEST) == GPIO_LOW);
    }
}

TEST(cli_core, boot_prints_the_report_with_crlf_or_says_there_is_none)
{
    setup();
    CHECK(ends_with(run("boot\r"), "ERR unavailable: no boot report\r\nmb> "));

    cli_core_set_boot_report(&core, "reset=POR\ncrash=none\n");
    CHECK(strcmp(run("boot\r"), "boot\r\nreset=POR\r\ncrash=none\r\nOK\r\nmb> ") == 0);
}

TEST(cli_core, health_reports_counters)
{
    const char *reply;

    setup();
    core.rx_dropped = 7U;
    reply           = run("health\r");
    CHECK(strstr(reply, "idle_count=") != NULL);
    CHECK(strstr(reply, "rx_dropped=7 tx_dropped=0") != NULL);
    CHECK(strstr(reply, "worst_handler_us=") != NULL);
    CHECK(ends_with(reply, "OK\r\nmb> "));
}

TEST(cli_core, log_records_lines_with_their_verdict)
{
    const char *reply;

    setup_with_ports(false);
    (void)run("uptime\r");
    (void)run("nope\r");
    (void)run("cal set x 1\r");
    (void)run("state\r");
    (void)run("\r"); /* empty lines are not logged */
    CHECK(core.log_count == 4U);

    reply = run("log\r");
    CHECK(strstr(reply, " OK uptime\r\n") != NULL);
    CHECK(strstr(reply, " UNKNOWN nope\r\n") != NULL);
    CHECK(strstr(reply, " REFUSED cal set x 1\r\n") != NULL);
    CHECK(strstr(reply, " ERR state\r\n") != NULL);
    CHECK(ends_with(reply, "OK\r\nmb> "));
}

TEST(cli_core, log_keeps_only_the_newest_entries)
{
    setup();
    for (size_t count = 0U; count < (CLI_LOG_DEPTH + 3U); count++) {
        (void)run("uptime\r");
    }
    (void)run("nope\r");
    CHECK(core.log_count == CLI_LOG_DEPTH);
    /* A full log of the longest lines still fits one reply. */
    CHECK(ends_with(run("log\r"), " UNKNOWN nope\r\nOK\r\nmb> "));
    CHECK(out.truncated == false);
}

TEST(cli_core, reply_that_does_not_fit_ends_with_truncated_error)
{
    char small[CLI_OUT_RESERVE + 40U];

    setup();
    cli_out_init(&out, small, sizeof(small));
    (void)feed("help\r");
    CHECK(out.reply_ready == true);
    CHECK(out.truncated == true);
    CHECK(out.len <= sizeof(small));
    CHECK(out.len >= 26U);
    CHECK(strncmp(&small[out.len - 26U], "ERR output truncated\r\nmb> ", 26U) == 0);
}
