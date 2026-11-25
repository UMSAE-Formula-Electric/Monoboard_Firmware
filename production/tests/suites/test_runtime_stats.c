/**
 * @file test_runtime_stats.c
 * @brief Run-time stats time base (wrap handling) and the timing macros.
 *
 * The 64-bit counter is pure logic, so wraps are tested by feeding it
 * readings that straddle 2^32. Checks that need a live scheduler (a busy
 * task dominating, percentages summing to ~100) belong to the desktop sim.
 */
#include <stdint.h>

#include "runtime_stats.h"
#include "test_framework.h"
#include "timing_if.h"

TEST(runtime_stats, first_update_primes_to_zero)
{
    RuntimeCounter counter = {0};

    CHECK(runtime_counter_update(&counter, 123456U) == 0U);
    CHECK(runtime_counter_update(&counter, 123556U) == 100U);
}

TEST(runtime_stats, accumulates_across_a_wrap)
{
    RuntimeCounter counter = {0};

    (void)runtime_counter_update(&counter, 0xFFFFFF00U);
    /* 0x100 ticks to reach 2^32, then 0x50 past it. */
    CHECK(runtime_counter_update(&counter, 0x00000050U) == 0x150U);
    CHECK(runtime_counter_update(&counter, 0x00000150U) == 0x250U);
}

TEST(runtime_stats, total_passes_32_bits_over_many_wraps)
{
    RuntimeCounter counter  = {0};
    const uint32_t step     = 0x40000000U; /* a quarter wrap per sample */
    uint32_t       raw      = 0U;
    uint64_t       expected = 0U;

    (void)runtime_counter_update(&counter, raw);
    for (int i = 0; i < 40; i++) { /* ten full wraps */
        raw += step;
        expected += step;
        CHECK(runtime_counter_update(&counter, raw) == expected);
    }
    CHECK(counter.total > UINT32_MAX);
}

TEST(runtime_stats, repeated_reading_adds_nothing)
{
    RuntimeCounter counter = {0};

    (void)runtime_counter_update(&counter, 500U);
    CHECK(runtime_counter_update(&counter, 500U) == 0U);
}

TEST(runtime_stats, timing_macros_measure_a_block)
{
    volatile uint32_t sink = 0U;

    TIMING_START(block);
    for (uint32_t i = 0U; i < 1000U; i++) {
        sink += i;
    }
    TIMING_STOP(block);

    CHECK(sink != 0U);
    CHECK(block_cycles > 0U);
    CHECK(block_cycles < (dwt_cycles_per_us() * 1000000U)); /* under a second */
    CHECK(dwt_cycles_per_us() > 0U);
    CHECK(dwt_cycles_to_us(dwt_cycles_per_us() * 5U) == 5U);
}
