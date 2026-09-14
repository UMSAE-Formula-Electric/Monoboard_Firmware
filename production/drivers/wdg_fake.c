/**
 * @file wdg_fake.c
 * @brief Programmable fake watchdog driver -- the WdgIf test double.
 */
#include "wdg_fake.h"

#include <stddef.h>

static bool          initialized;
static uint32_t      timeout_ms;
static uint32_t      kick_count;
static WdgResetCause reset_cause;
static bool          reset_cause_cleared;

static IfStatus fake_init(const WdgConfig *config)
{
    if ((config == NULL) || (config->timeout_ms == 0U)) {
        return IF_HW_FAULT;
    }
    initialized = true;
    timeout_ms  = config->timeout_ms;
    kick_count  = 0U;
    return IF_OK;
}

static void fake_kick(void)
{
    if (initialized) {
        kick_count++;
    }
}

static WdgResetCause fake_read_and_clear_reset_cause(void)
{
    WdgResetCause cause;

    if (reset_cause_cleared) {
        return WDG_RESET_CAUSE_UNKNOWN;
    }
    cause               = reset_cause;
    reset_cause_cleared = true;
    return cause;
}

const WdgIf wdg_fake = {
    .init                       = fake_init,
    .kick                       = fake_kick,
    .read_and_clear_reset_cause = fake_read_and_clear_reset_cause,
};

void wdg_fake_reset(void)
{
    initialized         = false;
    timeout_ms          = 0U;
    kick_count          = 0U;
    reset_cause         = WDG_RESET_CAUSE_UNKNOWN;
    reset_cause_cleared = false;
}

bool wdg_fake_is_initialized(void)
{
    return initialized;
}

uint32_t wdg_fake_timeout_ms(void)
{
    return timeout_ms;
}

uint32_t wdg_fake_kick_count(void)
{
    return kick_count;
}

void wdg_fake_set_reset_cause(WdgResetCause cause)
{
    reset_cause         = cause;
    reset_cause_cleared = false;
}
