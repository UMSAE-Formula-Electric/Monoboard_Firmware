/**
 * @file wdg_fake.h
 * @brief Programmable fake watchdog driver: implements WdgIf with no
 * hardware and no FreeRTOS dependency, so it builds and runs anywhere.
 * Composition roots wire `wdg_fake` in exactly where `wdg_stm32` would
 * go; tests additionally use the functions below to inject a simulated
 * boot cause and to inspect kick activity.
 */
#ifndef WDG_FAKE_H
#define WDG_FAKE_H

#include <stdbool.h>
#include <stdint.h>

#include "wdg_if.h"

extern const WdgIf wdg_fake;

/* Test-only control surface -- not part of the WdgIf contract. */
void     wdg_fake_reset(void);
bool     wdg_fake_is_initialized(void);
uint32_t wdg_fake_timeout_ms(void);
uint32_t wdg_fake_kick_count(void);

/* Simulates hardware latching a reset cause across a boot -- call before
 * init() to control what read_and_clear_reset_cause() reports. */
void wdg_fake_set_reset_cause(WdgResetCause cause);

#endif /* WDG_FAKE_H */
