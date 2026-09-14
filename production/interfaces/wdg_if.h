/**
 * @file wdg_if.h
 * @brief Independent watchdog contract shared by every watchdog driver
 * (real or fake) -- see issue #6.
 *
 * Header-only, standard-library types only -- see ARCHITECTURE.md,
 * Interface Layer rules.
 */
#ifndef MONOBOARD_WDG_IF_H
#define MONOBOARD_WDG_IF_H

#include <stdint.h>

#include "if_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Why the MCU last reset, as latched by hardware. Read once at startup,
 * before the next reset overwrites it. */
typedef enum {
    WDG_RESET_CAUSE_UNKNOWN = 0, /* cause not available, or not yet read */
    WDG_RESET_CAUSE_POWER_ON,    /* power-on / brown-out reset */
    WDG_RESET_CAUSE_PIN,         /* external NRST pin asserted */
    WDG_RESET_CAUSE_WATCHDOG,    /* the watchdog fired -- a kick() was missed */
    WDG_RESET_CAUSE_SOFTWARE,    /* requested via a software reset */
} WdgResetCause;

typedef struct {
    uint32_t timeout_ms; /* time between kicks before the watchdog resets the MCU */
} WdgConfig;

typedef struct {
    /**
     * Configure and start the independent watchdog. Must be called from
     * main() before the RTOS scheduler starts: once armed, the watchdog
     * cannot be stopped or reconfigured, so there is no later point at
     * which a missed kick is still recoverable.
     * @param config  countdown timeout
     * @return #IF_OK on success, or #IF_HW_FAULT if @p config is invalid
     *         (NULL, zero timeout_ms, a timeout unreachable from the
     *         watchdog's clock) or the underlying peripheral rejected
     *         the setup
     */
    IfStatus (*init)(const WdgConfig *config);

    /**
     * Reset the countdown to @c timeout_ms. Physically impossible to
     * fail once init() has succeeded: no arguments, no peripheral state
     * that can reject it. Calling before a successful init() is a no-op.
     */
    void (*kick)(void);

    /**
     * Report why the MCU last reset, then clear the hardware's latched
     * reset-cause flags so the next reset reports cleanly. Safe to call
     * before init() -- reflects the previous boot, not the current
     * watchdog configuration, and cannot fail: an unread or already-clear
     * flag simply reports #WDG_RESET_CAUSE_UNKNOWN.
     * @return the latched reset cause
     */
    WdgResetCause (*read_and_clear_reset_cause)(void);
} WdgIf;

#ifdef __cplusplus
}
#endif

#endif  // MONOBOARD_WDG_IF_H
