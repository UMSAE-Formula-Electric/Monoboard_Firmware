/**
 * @file timing_if.h
 * @brief Cycle-resolution timer and "measure this block" macros, for timing
 * step functions, ISRs and anything else that needs a real number.
 *
 * dwt_cycles() is a free-running 32-bit counter. On the STM32 it is the
 * Cortex-M4 DWT cycle counter: one count per core clock, wrapping every
 * ~24 s at 180 MHz. On the desktop it is CLOCK_MONOTONIC in nanoseconds,
 * wrapping every ~4.3 s, so the wrap path gets exercised there constantly.
 *
 * Only differences are meaningful. Subtract with unsigned arithmetic; the
 * result is valid for intervals shorter than one wrap. Convert with
 * dwt_cycles_per_us() instead of hard-coding a clock.
 *
 * The two counter reads sit inside the measurement, adding a few cycles on
 * the MCU. Time an empty block to see the floor on a given build.
 *
 * Example:
 *
 *     TIMING_START(pedal);
 *     pedal_task_step(&ctx);
 *     TIMING_STOP(pedal);
 *     // pedal_cycles is now a uint32_t
 *
 * Each macro declares a variable, so use them as statements, and use a tag
 * only once per scope.
 */
#ifndef TIMING_IF_H
#define TIMING_IF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read the cycle counter. Safe from a task or an ISR.
 * @return counter value, wraps at 2^32
 */
uint32_t dwt_cycles(void);

/**
 * Counter ticks per microsecond: the core clock in MHz on the STM32, 1000 on
 * the desktop. Never 0.
 */
uint32_t dwt_cycles_per_us(void);

/**
 * Convert a difference of two dwt_cycles() reads to microseconds, rounded down.
 * @param cycles  elapsed counter ticks
 */
uint32_t dwt_cycles_to_us(uint32_t cycles);

/** Start a measured block. Declares `<tag>_start_cycles`. */
#define TIMING_START(tag) const uint32_t tag##_start_cycles = dwt_cycles()

/** End a measured block. Declares `<tag>_cycles`, the elapsed counter ticks. */
#define TIMING_STOP(tag) const uint32_t tag##_cycles = (uint32_t)(dwt_cycles() - tag##_start_cycles)

#ifdef __cplusplus
}
#endif

#endif /* TIMING_IF_H */
