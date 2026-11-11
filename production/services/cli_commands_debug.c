/**
 * @file cli_commands_debug.c
 * @brief Output-forcing debug CLI commands (issue #51). Debug builds only.
 *
 * CMake compiles this file for Debug configurations and
 * cli_commands_debug_none.c for every other one, so nothing in here can
 * reach a release image. cli_cmd_set is deliberately not static: its name in
 * the map file is how its absence from mcu-release is verified.
 *
 * Every row carries CLI_FLAG_WRITE, so the dispatcher refuses it unless the
 * vehicle is in a safe state.
 */
#include <stddef.h>
#include <string.h>

#include "cli_core.h"
#include "gpio_if.h"

bool cli_cmd_set(CliCore *core, int argc, char *argv[], CliOut *out);

/**
 * `set <pin> <0|1>`: drive one output pin, for wiring checks.
 * @param core  CLI state; its ports provide the GPIO interface
 * @param argc  must be 2
 * @param argv  pin name, then "0" or "1"
 * @param out   reply body
 * @return true if the pin was driven
 */
bool cli_cmd_set(CliCore *core, int argc, char *argv[], CliOut *out)
{
    GpioLevel level;

    if (argc != 2) {
        return cli_fail(core, "usage: set <pin> <0|1>");
    }
    if ((core->ports == NULL) || (core->ports->gpio == NULL)) {
        return cli_fail(core, "unavailable: no gpio port");
    }
    /* The only pin in the catalogue today (gpio_if.h). */
    if (strcmp(argv[0], "test") != 0) {
        return cli_fail(core, "unknown pin, known: test");
    }
    if (strcmp(argv[1], "0") == 0) {
        level = GPIO_LOW;
    } else if (strcmp(argv[1], "1") == 0) {
        level = GPIO_HIGH;
    } else {
        return cli_fail(core, "value must be 0 or 1");
    }
    if (core->ports->gpio->write(GPIO_PIN_TEST, level) != IF_OK) {
        return cli_fail(core, "gpio write failed");
    }
    cli_out_printf(out, "test=%u" CLI_EOL, (level == GPIO_HIGH) ? 1U : 0U);
    return true;
}

const CliCommand cli_debug_commands[] = {
    {"set", "set <pin> <0|1>, debug only", cli_cmd_set, CLI_FLAG_WRITE},
};

const size_t cli_debug_command_count = sizeof(cli_debug_commands) / sizeof(cli_debug_commands[0]);
