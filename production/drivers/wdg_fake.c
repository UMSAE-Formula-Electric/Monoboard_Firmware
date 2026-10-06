/**
 * @file wdg_fake.c
 * @brief Programmable fake watchdog driver -- the WdgIf test double.
 */
#include "wdg_fake.h"

static bool          initialized;
static uint32_t      timeout_ms;
static uint32_t      kick_count;
static uint32_t      elapsed_ms;
static bool          fired;
static bool          force_init_hw_fault_next;
static WdgResetCause reset_cause;
static bool          reset_cause_cleared;

static WdgFakeCall call_log[WDG_FAKE_CALL_LOG_DEPTH];
static size_t      call_log_head;
static size_t      call_log_tail;
static size_t      call_log_count;

static void log_call(WdgFakeOp op, uint32_t timeout, IfStatus status, WdgResetCause cause)
{
    if (call_log_count >= WDG_FAKE_CALL_LOG_DEPTH) {
        return;
    }
    call_log[call_log_head].op          = op;
    call_log[call_log_head].timeout_ms  = timeout;
    call_log[call_log_head].status      = status;
    call_log[call_log_head].reset_cause = cause;
    call_log_head                       = (call_log_head + 1U) % WDG_FAKE_CALL_LOG_DEPTH;
    call_log_count++;
}

static IfStatus fake_init(const WdgConfig *config)
{
    uint32_t requested = (config != NULL) ? config->timeout_ms : 0U;
    IfStatus status    = IF_OK;

    if ((config == NULL) || (config->timeout_ms == 0U)) {
        status = IF_HW_FAULT;
    } else if (force_init_hw_fault_next) {
        force_init_hw_fault_next = false;
        status                   = IF_HW_FAULT;
    } else {
        initialized = true;
        timeout_ms  = config->timeout_ms;
        kick_count  = 0U;
        elapsed_ms  = 0U;
    }
    log_call(WDG_FAKE_OP_INIT, requested, status, WDG_RESET_CAUSE_UNKNOWN);
    return status;
}

static void fake_kick(void)
{
    log_call(WDG_FAKE_OP_KICK, 0U, IF_OK, WDG_RESET_CAUSE_UNKNOWN);
    if (initialized && !fired) {
        kick_count++;
        elapsed_ms = 0U;
    }
}

static WdgResetCause fake_read_and_clear_reset_cause(void)
{
    WdgResetCause cause = WDG_RESET_CAUSE_UNKNOWN;

    if (!reset_cause_cleared) {
        cause               = reset_cause;
        reset_cause_cleared = true;
    }
    log_call(WDG_FAKE_OP_READ_AND_CLEAR_RESET_CAUSE, 0U, IF_OK, cause);
    return cause;
}

const WdgIf wdg_fake = {
    .init                       = fake_init,
    .kick                       = fake_kick,
    .read_and_clear_reset_cause = fake_read_and_clear_reset_cause,
};

void wdg_fake_reboot(void)
{
    initialized              = false;
    timeout_ms               = 0U;
    kick_count               = 0U;
    elapsed_ms               = 0U;
    fired                    = false;
    force_init_hw_fault_next = false;
    call_log_head            = 0U;
    call_log_tail            = 0U;
    call_log_count           = 0U;
}

void wdg_fake_reset(void)
{
    wdg_fake_reboot();
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

void wdg_fake_force_init_hw_fault(void)
{
    force_init_hw_fault_next = true;
}

void wdg_fake_advance_ms(uint32_t ms)
{
    if (!initialized || fired) {
        return;
    }
    /* Saturating: a huge step must still fire, never wrap back under. */
    elapsed_ms = (ms > (UINT32_MAX - elapsed_ms)) ? UINT32_MAX : (elapsed_ms + ms);
    if (elapsed_ms >= timeout_ms) {
        fired = true;
        wdg_fake_set_reset_cause(WDG_RESET_CAUSE_WATCHDOG);
    }
}

bool wdg_fake_has_fired(void)
{
    return fired;
}

bool wdg_fake_pop_call(WdgFakeCall *out)
{
    if ((out == NULL) || (call_log_count == 0U)) {
        return false;
    }
    *out          = call_log[call_log_tail];
    call_log_tail = (call_log_tail + 1U) % WDG_FAKE_CALL_LOG_DEPTH;
    call_log_count--;
    return true;
}

size_t wdg_fake_call_count(void)
{
    return call_log_count;
}
