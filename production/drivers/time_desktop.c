/**
 * @file time_desktop.c
 * @brief Desktop monotonic time implementation using CLOCK_MONOTONIC.
 */
#define _POSIX_C_SOURCE 200809L

#include "time_if.h"

#include <time.h>

static uint32_t monotonic_time_us(void)
{
    struct timespec timestamp;

    if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0) {
        return 0U;
    }

    return (uint32_t)(((uint64_t)timestamp.tv_sec * 1000000U)
                      + ((uint64_t)timestamp.tv_nsec / 1000U));
}

uint32_t now_ms(void)
{
    return monotonic_time_us() / 1000U;
}

uint32_t now_ms_isr(void)
{
    return now_ms();
}

uint32_t now_us(void)
{
    return monotonic_time_us();
}

uint32_t now_us_isr(void)
{
    return monotonic_time_us();
}

bool deadline_passed(uint32_t start, uint32_t timeout_ms)
{
    return (uint32_t)(now_ms() - start) > timeout_ms;
}
