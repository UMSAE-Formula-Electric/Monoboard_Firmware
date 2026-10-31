/**
 * @file test_time.c
 * @brief Tests the desktop implementation of the monotonic time interface.
 */
#include <stdint.h>
#include <time.h>

#include "test_framework.h"
#include "time_if.h"

static void sleep_ms(long milliseconds)
{
    struct timespec duration = {
        .tv_sec  = milliseconds / 1000L,
        .tv_nsec = (milliseconds % 1000L) * 1000000L,
    };

    (void)nanosleep(&duration, NULL);
}

TEST(time, now_ms_is_monotonic)
{
    uint32_t before = now_ms();

    sleep_ms(2L);

    CHECK(now_ms() >= before);
}

TEST(time, now_ms_isr_is_monotonic)
{
    uint32_t before = now_ms_isr();

    sleep_ms(2L);

    CHECK(now_ms_isr() >= before);
}

TEST(time, now_us_has_finer_resolution_than_millisecond_clock)
{
    uint32_t before = now_us();
    uint32_t after;

    sleep_ms(2L);
    after = now_us();

    CHECK((uint32_t)(after - before) >= 1000U);
    CHECK((uint32_t)(after - before) < 1000000U);
}

TEST(time, now_us_isr_is_monotonic)
{
    uint32_t before = now_us_isr();

    sleep_ms(2L);

    CHECK(now_us_isr() >= before);
}

TEST(time, deadline_passed_reports_elapsed_timeout)
{
    uint32_t start = now_ms();

    CHECK(!deadline_passed(start, UINT32_MAX));
    sleep_ms(2L);
    CHECK(deadline_passed(start, 0U));
}

TEST(time, deadline_comparison_is_wrap_safe)
{
    const uint32_t start = UINT32_MAX - 2U;

    CHECK(deadline_passed_at(2U, start, 4U));
    CHECK(!deadline_passed_at(2U, start, 5U));
}
