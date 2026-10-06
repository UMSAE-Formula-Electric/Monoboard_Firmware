/**
 * @file wdg_stm32.h
 * @brief WdgIf driver: implements wdg_if on top of the STM32F446's
 * independent watchdog (HAL_IWDG_*), with the reset cause decoded from
 * RCC->CSR. See wdg_stm32.c for how the timeout is derived from the LSI's
 * worst-case tolerance and for the debug-freeze policy. Composition roots
 * inject this in place of `wdg_fake` to run on real hardware.
 */
#ifndef MONOBOARD_WDG_STM32_H
#define MONOBOARD_WDG_STM32_H

#include <stdint.h>

#include "wdg_if.h"

extern const WdgIf wdg_stm32;

/* Driver-internal diagnostics -- not part of the WdgIf contract. Exposed
 * so the watchdog manager / boot banner can surface them (issue #18).
 *
 * The IWDG runs from the LSI (17..47 kHz on the F446), so one programmed
 * reload value gives a range of real periods. init() programs it so the
 * period is at most timeout_ms at the slowest LSI; at the fastest LSI it
 * is wdg_stm32_min_period_ms(). Anything that decides when to kick must
 * finish well inside the latter. Both return 0 before a successful init(). */
uint32_t wdg_stm32_max_period_ms(void); /* period at the slowest LSI (<= timeout_ms) */
uint32_t wdg_stm32_min_period_ms(void); /* period at the fastest LSI: the real kick deadline */

/* Raw RCC->CSR reset flags as captured by the last
 * read_and_clear_reset_cause() call (0 before then), for logging the full
 * picture -- e.g. a watchdog reset also sets PINRSTF. */
uint32_t wdg_stm32_raw_reset_flags(void);

#endif  // MONOBOARD_WDG_STM32_H
