/**
 * @file test_main.c
 * @brief Entry point for the desktop unit-test binary.
 *
 * All cases live in suites/test_*.c and self-register; this file only
 * decides which suite to run. Usage:
 * @code
 *     unit_test            run every suite
 *     unit_test <suite>    run one suite (ctest invokes it this way)
 * @endcode
 */
#include <stddef.h>

#include "test_framework.h"

int main(int argc, char **argv)
{
    const char *suite_filter = (argc > 1) ? argv[1] : NULL;

    return test_run_all(suite_filter);
}
