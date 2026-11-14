/**
 * @file test_cli_fuzz.c
 * @brief Feeds the CLI 100,000 pseudo-random bytes (issue #51: "garbage in
 * never crashes"). The run is under the sanitizers, so any out-of-bounds
 * access fails the suite; the checks below cover the CLI's own invariants.
 */
#include <stddef.h>
#include <stdint.h>

#include "cli_core.h"
#include "gpio_fake.h"
#include "test_framework.h"

#define FUZZ_BYTES    100000U
#define FUZZ_OUT_SIZE 1024U

static uint32_t fuzz_state;

/* xorshift32: fixed seed, so a failure reproduces exactly. */
static uint32_t fuzz_next(void)
{
    fuzz_state ^= fuzz_state << 13;
    fuzz_state ^= fuzz_state >> 17;
    fuzz_state ^= fuzz_state << 5;
    return fuzz_state;
}

static bool fuzz_never_safe(void)
{
    return false;
}

static const CliPorts fuzz_ports = {
    .is_safe_state = fuzz_never_safe,
    .gpio          = &gpio_fake,
};

/* Command words mixed into the noise so real handlers run too. */
static const char *const fuzz_words[] = {
    "help\r",  "log\r",     "health\r", "version\r",      "boot\r", "set test 1\r",
    "faults ", "clear\r\n", "cal set ", "uptime uptime ", "\b\b",   "\x03",
};

static size_t fuzz_fill(uint8_t *chunk, size_t max)
{
    const uint32_t roll = fuzz_next();
    size_t         len  = 0U;

    if ((roll % 4U) == 0U) {
        const char *word = fuzz_words[(roll >> 8) % (sizeof(fuzz_words) / sizeof(fuzz_words[0]))];

        while ((len < max) && (word[len] != '\0')) {
            chunk[len] = (uint8_t)word[len];
            len++;
        }
    } else {
        len = 1U + ((roll >> 8) % (max - 1U));
        for (size_t index = 0U; index < len; index++) {
            chunk[index] = (uint8_t)fuzz_next();
        }
    }
    return len;
}

TEST(cli_fuzz, random_bytes_never_break_the_cli)
{
    static CliCore   core;
    static char      out_buf[FUZZ_OUT_SIZE];
    CliOut           out;
    uint8_t          chunk[32];
    size_t           fed          = 0U;
    bool             bounds_ok    = true;
    bool             progress_ok  = true;
    bool             never_forced = true;
    const GpioConfig config = {.dir = GPIO_DIR_OUTPUT, .pull = GPIO_PULL_NONE, .initial = GPIO_LOW};

    fuzz_state = 0x51C0FFEEU;
    gpio_fake_reset();
    (void)gpio_fake.init(GPIO_PIN_TEST, &config);
    cli_core_init(&core);
    cli_core_set_ports(&core, &fuzz_ports);
    cli_core_set_boot_report(&core, "reset=POR\n");

    while (fed < FUZZ_BYTES) {
        const size_t len = fuzz_fill(chunk, sizeof(chunk));
        size_t       pos = 0U;

        while (pos < len) {
            size_t consumed;

            cli_out_init(&out, out_buf, sizeof(out_buf));
            consumed = cli_core_feed(&core, &chunk[pos], len - pos, &out);
            pos += consumed;

            bounds_ok = bounds_ok && (out.len <= sizeof(out_buf))
                        && (core.line_len <= CLI_LINE_MAX_CHARS)
                        && (core.log_count <= CLI_LOG_DEPTH);
            /* A fresh output buffer always has room: no progress is a bug. */
            progress_ok  = progress_ok && (consumed > 0U);
            never_forced = never_forced && (gpio_fake_level(GPIO_PIN_TEST) == GPIO_LOW);
            if (consumed == 0U) {
                break;
            }
        }
        fed += len;
    }

    CHECK(bounds_ok);
    CHECK(progress_ok);
    CHECK(never_forced); /* not safe: no write may ever get through */

    /* Still usable afterwards. */
    cli_out_init(&out, out_buf, sizeof(out_buf));
    (void)cli_core_feed(&core, (const uint8_t *)"\x03uptime\r", 8U, &out);
    CHECK(out.reply_ready == true);
}
