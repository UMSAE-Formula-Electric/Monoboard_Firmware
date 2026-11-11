/**
 * @file cli_commands.c
 * @brief Debug CLI commands present in every build (issue #51).
 *
 * Read commands only, plus the placeholder rows for services that do not
 * exist yet: those keep the command names stable for the validation scripts
 * and answer `ERR unavailable: needs #NN` until their service lands.
 * Output-forcing commands live in cli_commands_debug.c, never here.
 *
 * A handler never reaches into another service's state (issue #51): when a
 * placeholder becomes real, it posts a request to the owning service.
 */
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "build_info_types.h"
#include "cli_core.h"
#include "fw_info.h"
#include "rtos_hooks.h"
#include "task.h"
#include "time_if.h"

static const char *verdict_name(CliVerdict verdict)
{
    const char *name = "ERR";

    switch (verdict) {
        case CLI_VERDICT_OK:
            name = "OK";
            break;
        case CLI_VERDICT_REFUSED:
            name = "REFUSED";
            break;
        case CLI_VERDICT_UNKNOWN:
            name = "UNKNOWN";
            break;
        case CLI_VERDICT_ERR:
        default:
            break;
    }
    return name;
}

static void print_table(CliOut *out, const CliCommand *table, size_t count)
{
    for (size_t row = 0U; row < count; row++) {
        cli_out_printf(out, "%-13s %s" CLI_EOL, table[row].name, table[row].help);
    }
}

static bool cmd_help(CliCore *core, int argc, char *argv[], CliOut *out)
{
    (void)core;
    (void)argc;
    (void)argv;
    print_table(out, cli_commands, cli_command_count);
    print_table(out, cli_debug_commands, cli_debug_command_count);
    return true;
}

static bool cmd_version(CliCore *core, int argc, char *argv[], CliOut *out)
{
    const BuildInfo *info = fw_info_get();

    (void)argc;
    (void)argv;
    if (!fw_info_is_valid(info)) {
        return cli_fail(core, "build info invalid");
    }
    cli_out_printf(out, "version=%s sha=%.12s branch=%s type=%s dirty=%u" CLI_EOL, info->version,
                   info->git_sha, info->git_branch, info->build_type,
                   ((info->flags & BUILD_INFO_FLAG_DIRTY) != 0U) ? 1U : 0U);
    return true;
}

static bool cmd_uptime(CliCore *core, int argc, char *argv[], CliOut *out)
{
    (void)core;
    (void)argc;
    (void)argv;
    cli_out_printf(out, "uptime_ms=%lu" CLI_EOL, (unsigned long)now_ms());
    return true;
}

static bool cmd_log(CliCore *core, int argc, char *argv[], CliOut *out)
{
    /* Oldest entry first. The `log` line itself is added after it runs. */
    const size_t oldest = (core->log_next + CLI_LOG_DEPTH - core->log_count) % CLI_LOG_DEPTH;

    (void)argc;
    (void)argv;
    for (size_t index = 0U; index < core->log_count; index++) {
        const CliLogEntry *entry = &core->log[(oldest + index) % CLI_LOG_DEPTH];

        cli_out_printf(out, "%lu %s %s" CLI_EOL, (unsigned long)entry->time_ms,
                       verdict_name(entry->verdict), entry->text);
    }
    return true;
}

static bool cmd_boot(CliCore *core, int argc, char *argv[], CliOut *out)
{
    const char *text = core->boot_report;
    size_t      line_start;
    size_t      pos;

    (void)argc;
    (void)argv;
    if ((text == NULL) || (text[0] == '\0')) {
        return cli_fail(core, "unavailable: no boot report");
    }
    /* The report ends its lines with LF only; a terminal needs CR LF. */
    line_start = 0U;
    for (pos = 0U; text[pos] != '\0'; pos++) {
        if (text[pos] == '\n') {
            cli_out_printf(out, "%.*s" CLI_EOL, (int)(pos - line_start), &text[line_start]);
            line_start = pos + 1U;
        }
    }
    if (pos > line_start) {
        cli_out_printf(out, "%.*s" CLI_EOL, (int)(pos - line_start), &text[line_start]);
    }
    return true;
}

static bool cmd_health(CliCore *core, int argc, char *argv[], CliOut *out)
{
    (void)argc;
    (void)argv;
    cli_out_printf(out, "idle_count=%lu" CLI_EOL, (unsigned long)rtos_hooks_idle_count());
    /* No current task before the scheduler runs (unit tests, early boot). */
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        cli_out_printf(out, "cli_stack_free_words=%lu" CLI_EOL,
                       (unsigned long)uxTaskGetStackHighWaterMark(NULL));
    }
    cli_out_printf(out, "rx_dropped=%lu tx_dropped=%lu" CLI_EOL, (unsigned long)core->rx_dropped,
                   (unsigned long)core->tx_dropped);
    cli_out_printf(out, "worst_handler_us=%lu worst_handler=%s" CLI_EOL,
                   (unsigned long)core->worst_handler_us,
                   (core->worst_handler_name != NULL) ? core->worst_handler_name : "none");
    return true;
}

static bool cmd_needs_vehicle_state(CliCore *core, int argc, char *argv[], CliOut *out)
{
    (void)argc;
    (void)argv;
    (void)out;
    return cli_fail(core, "unavailable: needs #25");
}

static bool cmd_needs_fault_manager(CliCore *core, int argc, char *argv[], CliOut *out)
{
    (void)argc;
    (void)argv;
    (void)out;
    return cli_fail(core, "unavailable: needs #59");
}

static bool cmd_needs_nvm(CliCore *core, int argc, char *argv[], CliOut *out)
{
    (void)argc;
    (void)argv;
    (void)out;
    return cli_fail(core, "unavailable: needs #61");
}

static bool cmd_needs_post(CliCore *core, int argc, char *argv[], CliOut *out)
{
    (void)argc;
    (void)argv;
    (void)out;
    return cli_fail(core, "unavailable: needs #50");
}

const CliCommand cli_commands[] = {
    {"help", "list commands", cmd_help, 0U},
    {"version", "firmware build identity", cmd_version, 0U},
    {"uptime", "milliseconds since boot", cmd_uptime, 0U},
    {"log", "last 16 command lines", cmd_log, 0U},
    {"boot", "reset cause and last crash", cmd_boot, 0U},
    {"health", "CLI and RTOS counters", cmd_health, 0U},
    {"state", "vehicle state (needs #25)", cmd_needs_vehicle_state, 0U},
    {"faults", "active faults (needs #59)", cmd_needs_fault_manager, 0U},
    {"faults clear", "clear faults (needs #59)", cmd_needs_fault_manager, CLI_FLAG_WRITE},
    {"cal", "read calibration (needs #61)", cmd_needs_nvm, 0U},
    {"cal set", "write calibration (needs #61)", cmd_needs_nvm, CLI_FLAG_WRITE},
    {"selftest", "self-test results (needs #50)", cmd_needs_post, 0U},
};

const size_t cli_command_count = sizeof(cli_commands) / sizeof(cli_commands[0]);
