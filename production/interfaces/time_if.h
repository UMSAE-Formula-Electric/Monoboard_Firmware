/**
 * @file time_if.h
 * @brief Monotonic time contract shared by application and services.
 *
 * This interface deliberately exposes no wall-clock or calendar time. The
 * system has no RTC battery and elapsed-time decisions do not need one.
 *
 * All timestamps returned here are monotonic: they do not jump because of
 * wall-clock adjustment and do not go backwards while the underlying clock
 * is operating normally.
 *
 * Millisecond timestamps are 32-bit values and wrap after approximately
 * 49.7 days. Code comparing timestamps must use unsigned subtraction:
 *
 *     (uint32_t)(now - then) > timeout_ms
 *
 * Never write `now > then + timeout_ms`; addition can wrap and cause a
 * timeout to be missed. Use deadline_passed() when checking a millisecond
 * deadline so this rule is implemented consistently.
 *
 * On the STM32 target, now_ms() is backed by the FreeRTOS tick. The current
 * target configuration is 1000 Hz, giving 1 ms resolution; callers must not
 * assume finer resolution than the configured tick period. On the desktop,
 * now_ms() is backed by CLOCK_MONOTONIC.
 *
 * Microsecond timestamps are intended for short profiling intervals. They
 * are 32-bit values and wrap after approximately 71.6 minutes; compare
 * nearby values using unsigned subtraction. On the STM32 target they are
 * backed by the Cortex-M cycle counter, and on the desktop by CLOCK_MONOTONIC.
 *
 * A sample's timestamp belongs to the instant at which the sample was taken,
 * not the later instant at which its containing state or message is filled
 * in. In particular, a future PedalState.timestamp_ms must be captured at
 * acquisition and preserved through subsequent processing.
 */

#ifndef MONOBOARD_TIME_IF_H
#define MONOBOARD_TIME_IF_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return the current monotonic time in milliseconds.
 *
 * Call from task or ordinary application context. The 32-bit result wraps
 * naturally; use deadline_passed() or unsigned subtraction for comparisons.
 *
 * @return current monotonic millisecond timestamp
 */
uint32_t now_ms(void);

/**
 * Return the current monotonic time in milliseconds from interrupt context.
 *
 * This variant uses the ISR-safe time source on the target and must be used
 * by interrupt handlers instead of now_ms(). It does not block, allocate, or
 * perform any other work beyond reading the clock.
 *
 * @return current monotonic millisecond timestamp
 */
uint32_t now_ms_isr(void);

/**
 * Return the current high-resolution monotonic time in microseconds.
 *
 * Use this for short profiling intervals, not long-lived deadlines. The
 * 32-bit result wraps after approximately 71.6 minutes; unsigned subtraction
 * is valid for intervals shorter than one full wrap.
 *
 * @return current monotonic microsecond timestamp
 */
uint32_t now_us(void);

/**
 * Return the current high-resolution monotonic time in microseconds from
 * interrupt context.
 *
 * This variant does not block or allocate and is suitable for timestamping
 * an interrupt-originated event or profiling interrupt work.
 *
 * @return current monotonic microsecond timestamp
 */
uint32_t now_us_isr(void);

/**
 * Test whether a millisecond deadline has passed.
 *
 * The comparison is deliberately based on unsigned subtraction, making it
 * correct when the 32-bit millisecond counter wraps. A deadline is considered
 * passed only when elapsed time is strictly greater than @p timeout_ms;
 * equality is not yet passed.
 *
 * @param start       timestamp at which the interval began
 * @param timeout_ms  allowed elapsed time in milliseconds
 * @return true if more than @p timeout_ms has elapsed, otherwise false
 */
bool deadline_passed(uint32_t start, uint32_t timeout_ms);

/**
 * Test a millisecond deadline using an explicitly supplied current timestamp.
 *
 * This pure form is useful when a caller already has a timestamp and for
 * deterministic tests of counter wraparound. It uses the same strict
 * greater-than semantics as deadline_passed().
 *
 * @param now         current monotonic timestamp
 * @param start       timestamp at which the interval began
 * @param timeout_ms  allowed elapsed time in milliseconds
 * @return true if more than @p timeout_ms has elapsed, otherwise false
 */
bool deadline_passed_at(uint32_t now, uint32_t start, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* MONOBOARD_TIME_IF_H */
