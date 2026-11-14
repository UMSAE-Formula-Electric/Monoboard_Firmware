/**
 * @file cli_commands_debug_none.c
 * @brief Empty debug command table for every non-Debug build (issue #51).
 *
 * Stands in for cli_commands_debug.c so release images contain no
 * output-forcing command. C has no zero-length arrays, so the table holds
 * one unused row and reports a count of zero.
 */
#include <stddef.h>

#include "cli_core.h"

const CliCommand cli_debug_commands[] = {
    {NULL, NULL, NULL, 0U},
};

const size_t cli_debug_command_count = 0U;
