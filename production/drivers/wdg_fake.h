/**
 * @file wdg_fake.h
 * @brief Programmable fake watchdog driver: implements WdgIf with no
 * hardware and no FreeRTOS dependency, so it builds and runs anywhere.
 * Composition roots wire `wdg_fake` in exactly where `wdg_stm32` would
 * go; tests additionally use the functions below to inject a simulated
 * boot cause, to inspect kick activity, and to run a simulated countdown.
 * Follows the fake driver conventions from issue #16: every call is
 * recorded (with its arguments and result) in a ring buffer a test can
 * inspect, return values and the next boot's reset cause can be scripted
 * ahead of time, and every failure mode the real hardware can produce
 * (init rejected, watchdog bite) can be forced. Zero `#ifdef TEST` --
 * this is ordinary code in every build.
 */
#ifndef WDG_FAKE_H
#define WDG_FAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wdg_if.h"

extern const WdgIf wdg_fake;

/* Capacity of the call log. Calls beyond it are not logged, but are still
 * counted by wdg_fake_kick_count() and still affect the countdown. */
#define WDG_FAKE_CALL_LOG_DEPTH 16U

typedef enum {
    WDG_FAKE_OP_INIT = 0,
    WDG_FAKE_OP_KICK,
    WDG_FAKE_OP_READ_AND_CLEAR_RESET_CAUSE,
} WdgFakeOp;

typedef struct {
    WdgFakeOp     op;
    uint32_t      timeout_ms;  /* INIT: config->timeout_ms (0 if config was NULL) */
    IfStatus      status;      /* INIT: what init() returned */
    WdgResetCause reset_cause; /* READ_AND_CLEAR_RESET_CAUSE: what it returned */
} WdgFakeCall;

/* Test-only control surface -- not part of the WdgIf contract. */
void     wdg_fake_reset(void);
bool     wdg_fake_is_initialized(void);
uint32_t wdg_fake_timeout_ms(void);
uint32_t wdg_fake_kick_count(void);

/* Simulates hardware latching a reset cause across a boot -- call before
 * init() to control what read_and_clear_reset_cause() reports. */
void wdg_fake_set_reset_cause(WdgResetCause cause);

/* Fault injection: makes exactly the next init() fail with IF_HW_FAULT, as
 * if the peripheral had rejected the setup, then clears itself. */
void wdg_fake_force_init_hw_fault(void);

/* Simulated countdown. Advances the fake's clock by @p ms; once
 * timeout_ms elapses with no kick() the watchdog "fires": the fake latches
 * WDG_RESET_CAUSE_WATCHDOG for the next boot and ignores further kicks,
 * exactly as the MCU would be gone. No effect before a successful init(). */
void wdg_fake_advance_ms(uint32_t ms);
bool wdg_fake_has_fired(void);

/* Simulates the MCU coming back up after a reset: the watchdog is disarmed
 * and kick/countdown/log state is cleared, but the latched reset cause
 * survives -- as it does in RCC->CSR -- for read_and_clear_reset_cause(). */
void wdg_fake_reboot(void);

/* Call log: every WdgIf call since the last reset/reboot, FIFO, oldest first. */
bool   wdg_fake_pop_call(WdgFakeCall *out);
size_t wdg_fake_call_count(void);

#endif /* WDG_FAKE_H */
