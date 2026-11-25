/**
 * @file cli_commands_debug.c
 * @brief Output-forcing debug CLI commands (issue #51). Debug builds only.
 *
 * CMake compiles this file for Debug configurations and
 * cli_commands_debug_none.c for every other one, so nothing in here can
 * reach a release image. cli_cmd_set is deliberately not static: its name in
 * the map file is how its absence from mcu-release is verified.
 *
 * `set` carries CLI_FLAG_WRITE, so the dispatcher refuses it unless the
 * vehicle is in a safe state. `cantx` does not: see its comment.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "can_router_service.h"
#include "cli_core.h"
#include "gpio_if.h"

/* Pins `set` may drive: the dash LEDs only. They are harmless to force,
 * unlike the safety-loop and tractive-system outputs, which stay out of
 * this table on purpose. */
typedef struct {
    const char *name;
    GpioPin     pin;
} CliSetPin;

static const CliSetPin set_pins[] = {
    {"rtd_led", GPIO_PIN_RTD_LED_OUT},
    {"lc_led", GPIO_PIN_LC_LED_OUT},
    {"tc_led", GPIO_PIN_TC_LED_OUT},
};

#define SET_PIN_COUNT (sizeof(set_pins) / sizeof(set_pins[0]))
#define SET_PIN_NAMES "rtd_led lc_led tc_led"

/* Not static on purpose: the release check looks for this symbol in the
 * mcu-release map file to prove the command was not compiled in. */
/* NOLINTNEXTLINE(misc-use-internal-linkage) */
bool cli_cmd_set(CliCore *core, int argc, char *argv[], CliOut *out);

/**
 * `set <pin> <0|1>`: drive one dash LED, for wiring checks. The pin is
 * (re)configured as a push-pull output at the requested level, so it works
 * before any service has claimed it.
 * @param core  CLI state; its ports provide the GPIO interface
 * @param argc  must be 2
 * @param argv  pin name, then "0" or "1"
 * @param out   reply body
 * @return true if the pin was driven
 */
/* NOLINTNEXTLINE(misc-use-internal-linkage) */
bool cli_cmd_set(CliCore *core, int argc, char *argv[], CliOut *out)
{
    const CliSetPin *target = NULL;
    GpioLevel        level;
    size_t           i;

    if (argc != 2) {
        return cli_fail(core, "usage: set <pin> <0|1>");
    }
    if ((core->ports == NULL) || (core->ports->gpio == NULL)) {
        return cli_fail(core, "unavailable: no gpio port");
    }
    for (i = 0U; i < SET_PIN_COUNT; i++) {
        if (strcmp(argv[0], set_pins[i].name) == 0) {
            target = &set_pins[i];
            break;
        }
    }
    if (target == NULL) {
        return cli_fail(core, "unknown pin, known: " SET_PIN_NAMES);
    }
    if (strcmp(argv[1], "0") == 0) {
        level = GPIO_LOW;
    } else if (strcmp(argv[1], "1") == 0) {
        level = GPIO_HIGH;
    } else {
        return cli_fail(core, "value must be 0 or 1");
    }
    {
        const GpioConfig config = {
            .dir = GPIO_DIR_OUTPUT, .pull = GPIO_PULL_NONE, .initial = level};

        if (core->ports->gpio->init(target->pin, &config) != IF_OK) {
            return cli_fail(core, "gpio write failed");
        }
    }
    cli_out_printf(out, "%s=%u" CLI_EOL, target->name, (level == GPIO_HIGH) ? 1U : 0U);
    return true;
}

#define CANTX_MAX_STD_ID 0x7FFU

/* Value of one hex digit, or -1. */
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* A standard ID in hex, with or without 0x: 1 to 3 digits, at most 0x7FF. */
static bool parse_std_id(const char *text, uint32_t *id)
{
    uint32_t value  = 0U;
    size_t   digits = 0U;

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text += 2;
    }
    for (; *text != '\0'; text++) {
        const int digit = hex_digit(*text);

        if (digit < 0 || ++digits > 3U) {
            return false;
        }
        value = (value << 4U) | (uint32_t)digit;
    }
    if (digits == 0U || value > CANTX_MAX_STD_ID) {
        return false;
    }
    *id = value;
    return true;
}

/* Payload as one run of hex digit pairs ("DEADBEEF"), 0 to 8 bytes. */
static bool parse_payload(const char *text, CanFrame *frame)
{
    size_t len = strlen(text);
    size_t i;

    if (len % 2U != 0U || len / 2U > CAN_MAX_DLC) {
        return false;
    }
    for (i = 0U; i < len; i += 2U) {
        const int high = hex_digit(text[i]);
        const int low  = hex_digit(text[i + 1U]);

        if (high < 0 || low < 0) {
            return false;
        }
        frame->data[i / 2U] = (uint8_t)((high << 4) | low);
    }
    frame->dlc = (uint8_t)(len / 2U);
    return true;
}

/* Not static on purpose, like cli_cmd_set: the release check looks for
 * this symbol in the mcu-release map file. */
/* NOLINTNEXTLINE(misc-use-internal-linkage) */
bool cli_cmd_cantx(CliCore *core, int argc, char *argv[], CliOut *out);

/**
 * `cantx <id> [hexdata]`: queue one standard data frame on the sensor bus
 * through the CAN router, to check its TX path on a bench, e.g.
 * `cantx 7F1 DEADBEEF`. The router task sends it on its next step; `can`
 * shows the counters.
 *
 * Not CLI_FLAG_WRITE, so it works before the vehicle state exists (#25):
 * it is Debug-only, and it can only reach the sensor bus, which carries
 * validation data and commands nothing. The main bus (MC, BMS, IMD) stays
 * out of reach on purpose; a command that sends there must be a write.
 * @param core  CLI state
 * @param argc  1 or 2
 * @param argv  ID in hex (1-3 digits, at most 7FF, optional 0x), then
 *              optionally 0-8 payload bytes as one hex string
 * @param out   reply body
 * @return true if the router queued the frame
 */
/* NOLINTNEXTLINE(misc-use-internal-linkage) */
bool cli_cmd_cantx(CliCore *core, int argc, char *argv[], CliOut *out)
{
    CanFrame frame = {0};
    IfStatus status;

    if (argc < 1 || argc > 2) {
        return cli_fail(core, "usage: cantx <id> [hexdata]");
    }
    if (!parse_std_id(argv[0], &frame.id)) {
        return cli_fail(core, "id must be hex, at most 7FF");
    }
    if (argc == 2 && !parse_payload(argv[1], &frame)) {
        return cli_fail(core, "data must be 0-8 bytes of hex pairs");
    }

    status = can_router_send(CAN_ROUTER_BUS_SENSOR, &frame);
    if (status == IF_BUSY) {
        return cli_fail(core, "router tx queue full, frame dropped");
    }
    if (status != IF_OK) {
        return cli_fail(core, "unavailable: can router not running");
    }
    cli_out_printf(out, "queued bus=sensor id=0x%03lX dlc=%u" CLI_EOL, (unsigned long)frame.id,
                   (unsigned int)frame.dlc);
    return true;
}

const CliCommand cli_debug_commands[] = {
    {"set", "set <pin> <0|1>, debug only", cli_cmd_set, CLI_FLAG_WRITE},
    {"cantx", "cantx <id> [hexdata], sensor bus, debug only", cli_cmd_cantx, 0U},
};

const size_t cli_debug_command_count = sizeof(cli_debug_commands) / sizeof(cli_debug_commands[0]);
