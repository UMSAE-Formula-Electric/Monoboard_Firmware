/* Minimal xUnit-style harness for the desktop test build.
 *
 * A suite file drops its cases in with TEST() and never touches main():
 * each case registers itself at load time, so adding coverage is "create
 * suites/test_<name>.c" and nothing else. The runner prints a green PASS
 * or red FAIL tag per test (plain [PASS]/[FAIL] when stdout is not a
 * terminal, e.g. under ctest) followed by a one-line summary.
 */
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

typedef void (*TestFn)(void);

/*  Register one test case. TEST() calls this for you; use it directly
 *  only for a case generated at runtime. */
void test_register(const char *suite, const char *name, TestFn fn);

/*  Record a failed assertion (kept with file:line:expr) for the test
 *  that is currently running. Reach for CHECK() instead of calling this. */
void test_fail_check(const char *file, int line, const char *expr);

#define CHECK(cond)                                            \
    do                                                         \
    {                                                          \
        if (!(cond))                                           \
        {                                                      \
            test_fail_check(__FILE__, __LINE__, #cond);        \
        }                                                      \
    } while (0)

/*  Run every registered case whose suite equals suite_filter, or all of
 *  them when suite_filter is NULL. Returns 0 iff nothing failed -- return
 *  it straight out of main(). */
int test_run_all(const char *suite_filter);

/*  Define and self-register a test body:
 *
 *      TEST(can_fake, send_before_init_fails)
 *      {
 *          CHECK(can_fake.send(&frame) == CAN_ERR_HAL);
 *      }
 */
#define TEST(suite, name)                                                     \
    static void test_body_##suite##_##name(void);                             \
    static void test_reg_##suite##_##name(void) __attribute__((constructor)); \
    static void test_reg_##suite##_##name(void)                               \
    {                                                                        \
        test_register(#suite, #name, test_body_##suite##_##name);             \
    }                                                                        \
    static void test_body_##suite##_##name(void)

#endif /* TEST_FRAMEWORK_H */
