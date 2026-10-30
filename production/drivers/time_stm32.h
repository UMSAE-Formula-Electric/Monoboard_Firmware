/**
 * @file time_stm32.h
 * @brief STM32-specific initialization for the monotonic time source.
 */
#ifndef MONOBOARD_TIME_STM32_H
#define MONOBOARD_TIME_STM32_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Enable the Cortex-M cycle counter used by now_us().
 *
 * Call once during MCU startup after the final core clock configuration and
 * before any microsecond timestamp is requested.
 */
void time_stm32_init(void);

#ifdef __cplusplus
}
#endif

#endif /* MONOBOARD_TIME_STM32_H */
