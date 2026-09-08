/**
 * @file test_framework.c
 * @brief Implementation of the harness declared in test_framework.h.
 */
#include "test_framework.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_MAX_CASES    128U
#define TEST_MAX_FAILURES 32U

typedef struct {
    const char *suite;
    const char *name;
    TestFn      fn;
} TestCase;

typedef struct {
    const char *file;
    int         line;
    const char *expr;
} TestFailure;

static TestCase g_cases[TEST_MAX_CASES];
static size_t   g_case_count;

/*  Assertion failures for the test currently executing; reset per test. */
static TestFailure g_failures[TEST_MAX_FAILURES];
static size_t      g_failure_count;

void test_register(const char *suite, const char *name, TestFn fn)
{
    if (g_case_count >= TEST_MAX_CASES) {
        fprintf(stderr, "test_framework: TEST_MAX_CASES exceeded, raise it\n");
        abort();
    }

    g_cases[g_case_count].suite = suite;
    g_cases[g_case_count].name  = name;
    g_cases[g_case_count].fn    = fn;
    g_case_count++;
}

void test_fail_check(const char *file, int line, const char *expr)
{
    if (g_failure_count < TEST_MAX_FAILURES) {
        g_failures[g_failure_count].file = file;
        g_failures[g_failure_count].line = line;
        g_failures[g_failure_count].expr = expr;
    }
    g_failure_count++;
}

/*  Colour only when writing to a real terminal and NO_COLOR is unset, so
 *  ctest / CI logs stay plain text. */
static bool color_enabled(void)
{
    return (getenv("NO_COLOR") == NULL) && (isatty(STDOUT_FILENO) == 1);
}

static const char *tag_pass(void)
{
    return color_enabled() ? "\033[30;42m PASS \033[0m" : "[PASS]";
}

static const char *tag_fail(void)
{
    return color_enabled() ? "\033[97;41m FAIL \033[0m" : "[FAIL]";
}

int test_run_all(const char *suite_filter)
{
    size_t i;
    size_t j;
    size_t passed = 0U;
    size_t failed = 0U;
    size_t ran    = 0U;

    for (i = 0U; i < g_case_count; i++) {
        const TestCase *test_case = &g_cases[i];

        if ((suite_filter != NULL) && (strcmp(suite_filter, test_case->suite) != 0)) {
            continue;
        }

        g_failure_count = 0U;
        test_case->fn();
        ran++;

        if (g_failure_count == 0U) {
            printf("%s %s / %s\n", tag_pass(), test_case->suite, test_case->name);
            passed++;
        } else {
            printf("%s %s / %s\n", tag_fail(), test_case->suite, test_case->name);
            failed++;

            for (j = 0U; (j < g_failure_count) && (j < TEST_MAX_FAILURES); j++) {
                printf("       %s:%d: %s\n", g_failures[j].file, g_failures[j].line,
                       g_failures[j].expr);
            }
            if (g_failure_count > TEST_MAX_FAILURES) {
                printf("       ... and %zu more\n", g_failure_count - TEST_MAX_FAILURES);
            }
        }
    }

    if (ran == 0U) {
        fprintf(stderr, "no tests matched \"%s\"\n", (suite_filter != NULL) ? suite_filter : "");
        return 1;
    }

    printf("\n%zu passed, %zu failed  (%zu run)\n", passed, failed, ran);
    return (failed == 0U) ? 0 : 1;
}
