/**
 * @file test_wdg_fake.c
 * @brief Exercises the WdgIf contract (production/interfaces/wdg_if.h)
 * through wdg_fake, the dependency-free double for wdg_stm32: contract
 * behaviour, the call log, fault injection, and the simulated countdown.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "test_framework.h"
#include "wdg_fake.h"

static WdgConfig make_config(void)
{
    WdgConfig config = {.timeout_ms = 500U};
    return config;
}

TEST(wdg_fake, init_rejects_null_or_zero_timeout)
{
    WdgConfig zero_timeout  = make_config();
    zero_timeout.timeout_ms = 0U;

    wdg_fake_reset();
    CHECK(wdg_fake.init(NULL) == IF_HW_FAULT);
    CHECK(wdg_fake.init(&zero_timeout) == IF_HW_FAULT);
    CHECK(!wdg_fake_is_initialized());
}

TEST(wdg_fake, init_succeeds_and_records_timeout)
{
    WdgConfig config = make_config();

    wdg_fake_reset();
    CHECK(wdg_fake.init(&config) == IF_OK);
    CHECK(wdg_fake_is_initialized());
    CHECK(wdg_fake_timeout_ms() == 500U);
}

TEST(wdg_fake, kick_before_init_is_a_noop)
{
    wdg_fake_reset();
    wdg_fake.kick();
    CHECK(wdg_fake_kick_count() == 0U);
}

TEST(wdg_fake, kick_after_init_is_counted)
{
    WdgConfig config = make_config();

    wdg_fake_reset();
    CHECK(wdg_fake.init(&config) == IF_OK);

    wdg_fake.kick();
    wdg_fake.kick();
    CHECK(wdg_fake_kick_count() == 2U);
}

TEST(wdg_fake, reset_cause_defaults_to_unknown)
{
    wdg_fake_reset();
    CHECK(wdg_fake.read_and_clear_reset_cause() == WDG_RESET_CAUSE_UNKNOWN);
}

TEST(wdg_fake, reset_cause_reports_once_then_clears)
{
    wdg_fake_reset();
    wdg_fake_set_reset_cause(WDG_RESET_CAUSE_WATCHDOG);

    CHECK(wdg_fake.read_and_clear_reset_cause() == WDG_RESET_CAUSE_WATCHDOG);
    CHECK(wdg_fake.read_and_clear_reset_cause() == WDG_RESET_CAUSE_UNKNOWN);
}

TEST(wdg_fake, reset_cause_readable_before_init)
{
    wdg_fake_reset();
    wdg_fake_set_reset_cause(WDG_RESET_CAUSE_POWER_ON);

    CHECK(!wdg_fake_is_initialized());
    CHECK(wdg_fake.read_and_clear_reset_cause() == WDG_RESET_CAUSE_POWER_ON);
}

TEST(wdg_fake, reset_clears_all_state)
{
    WdgConfig config = make_config();

    wdg_fake_reset();
    CHECK(wdg_fake.init(&config) == IF_OK);
    wdg_fake.kick();
    wdg_fake_set_reset_cause(WDG_RESET_CAUSE_SOFTWARE);

    wdg_fake_reset();

    CHECK(!wdg_fake_is_initialized());
    CHECK(wdg_fake_timeout_ms() == 0U);
    CHECK(wdg_fake_kick_count() == 0U);
    CHECK(wdg_fake.read_and_clear_reset_cause() == WDG_RESET_CAUSE_UNKNOWN);
}

TEST(wdg_fake, forced_init_fault_is_one_shot)
{
    WdgConfig config = make_config();

    wdg_fake_reset();
    wdg_fake_force_init_hw_fault();
    CHECK(wdg_fake.init(&config) == IF_HW_FAULT);
    CHECK(!wdg_fake_is_initialized());

    CHECK(wdg_fake.init(&config) == IF_OK);
    CHECK(wdg_fake_is_initialized());
}

TEST(wdg_fake, call_log_records_every_call_in_order)
{
    WdgConfig   config = make_config();
    WdgFakeCall call;

    wdg_fake_reset();
    wdg_fake_set_reset_cause(WDG_RESET_CAUSE_PIN);

    CHECK(wdg_fake.read_and_clear_reset_cause() == WDG_RESET_CAUSE_PIN);
    CHECK(wdg_fake.init(NULL) == IF_HW_FAULT);
    CHECK(wdg_fake.init(&config) == IF_OK);
    wdg_fake.kick();
    CHECK(wdg_fake_call_count() == 4U);

    CHECK(wdg_fake_pop_call(&call));
    CHECK(call.op == WDG_FAKE_OP_READ_AND_CLEAR_RESET_CAUSE);
    CHECK(call.reset_cause == WDG_RESET_CAUSE_PIN);

    CHECK(wdg_fake_pop_call(&call));
    CHECK(call.op == WDG_FAKE_OP_INIT);
    CHECK(call.timeout_ms == 0U);
    CHECK(call.status == IF_HW_FAULT);

    CHECK(wdg_fake_pop_call(&call));
    CHECK(call.op == WDG_FAKE_OP_INIT);
    CHECK(call.timeout_ms == 500U);
    CHECK(call.status == IF_OK);

    CHECK(wdg_fake_pop_call(&call));
    CHECK(call.op == WDG_FAKE_OP_KICK);

    CHECK(!wdg_fake_pop_call(&call));
    CHECK(wdg_fake_call_count() == 0U);
}

TEST(wdg_fake, call_log_stops_at_depth_but_kicks_still_count)
{
    WdgConfig config = make_config();
    uint32_t  i;

    wdg_fake_reset();
    CHECK(wdg_fake.init(&config) == IF_OK);
    for (i = 0U; i < (uint32_t)WDG_FAKE_CALL_LOG_DEPTH + 4U; i++) {
        wdg_fake.kick();
    }
    CHECK(wdg_fake_call_count() == WDG_FAKE_CALL_LOG_DEPTH);
    CHECK(wdg_fake_kick_count() == (uint32_t)WDG_FAKE_CALL_LOG_DEPTH + 4U);
}

TEST(wdg_fake, countdown_does_not_run_before_init)
{
    wdg_fake_reset();
    wdg_fake_advance_ms(UINT32_MAX);
    CHECK(!wdg_fake_has_fired());
}

TEST(wdg_fake, kicks_inside_timeout_keep_it_alive)
{
    WdgConfig config = make_config();
    uint32_t  i;

    wdg_fake_reset();
    CHECK(wdg_fake.init(&config) == IF_OK);
    for (i = 0U; i < 10U; i++) {
        wdg_fake_advance_ms(499U);
        wdg_fake.kick();
    }
    CHECK(!wdg_fake_has_fired());
    CHECK(wdg_fake_kick_count() == 10U);
}

TEST(wdg_fake, missed_kick_fires_and_latches_watchdog_cause)
{
    WdgConfig config = make_config();

    wdg_fake_reset();
    CHECK(wdg_fake.init(&config) == IF_OK);
    wdg_fake_advance_ms(499U);
    CHECK(!wdg_fake_has_fired());
    wdg_fake_advance_ms(1U);
    CHECK(wdg_fake_has_fired());

    /* The MCU is gone: a late kick changes nothing. */
    wdg_fake.kick();
    CHECK(wdg_fake_kick_count() == 0U);

    wdg_fake_reboot();
    CHECK(!wdg_fake_is_initialized());
    CHECK(!wdg_fake_has_fired());
    CHECK(wdg_fake.read_and_clear_reset_cause() == WDG_RESET_CAUSE_WATCHDOG);
    CHECK(wdg_fake.read_and_clear_reset_cause() == WDG_RESET_CAUSE_UNKNOWN);
}

TEST(wdg_fake, countdown_saturates_instead_of_wrapping)
{
    WdgConfig config = make_config();

    wdg_fake_reset();
    CHECK(wdg_fake.init(&config) == IF_OK);
    wdg_fake_advance_ms(100U);
    wdg_fake_advance_ms(UINT32_MAX);
    CHECK(wdg_fake_has_fired());
}

/* Minimal stand-in for the watchdog manager (issue "Watchdog supervision
 * and validation"): kick only when every registered task has checked in
 * since the last kick. Lives here only to prove, through the fake, that a
 * missed check-in produces no kick -- and therefore a reset. */
#define SUPERVISED_TASKS 2U

static bool checked_in[SUPERVISED_TASKS];

static void supervisor_step(const WdgIf *wdg)
{
    uint32_t i;

    for (i = 0U; i < SUPERVISED_TASKS; i++) {
        if (!checked_in[i]) {
            return;
        }
    }
    for (i = 0U; i < SUPERVISED_TASKS; i++) {
        checked_in[i] = false;
    }
    wdg->kick();
}

TEST(wdg_fake, missed_check_in_results_in_no_kick)
{
    WdgConfig config = make_config();

    wdg_fake_reset();
    CHECK(wdg_fake.init(&config) == IF_OK);

    /* Both tasks healthy: one kick per supervisor period. */
    checked_in[0] = true;
    checked_in[1] = true;
    supervisor_step(&wdg_fake);
    wdg_fake_advance_ms(250U);
    CHECK(wdg_fake_kick_count() == 1U);

    /* Task 1 stalls: no further kick, so the countdown runs out. */
    checked_in[0] = true;
    supervisor_step(&wdg_fake);
    wdg_fake_advance_ms(250U);
    checked_in[0] = true;
    supervisor_step(&wdg_fake);
    wdg_fake_advance_ms(250U);

    CHECK(wdg_fake_kick_count() == 1U);
    CHECK(wdg_fake_has_fired());

    wdg_fake_reboot();
    CHECK(wdg_fake.read_and_clear_reset_cause() == WDG_RESET_CAUSE_WATCHDOG);
}

TEST(wdg_fake, reboot_keeps_scripted_cause_but_clears_the_rest)
{
    WdgConfig config = make_config();

    wdg_fake_reset();
    wdg_fake_set_reset_cause(WDG_RESET_CAUSE_SOFTWARE);
    CHECK(wdg_fake.init(&config) == IF_OK);
    wdg_fake.kick();

    wdg_fake_reboot();

    CHECK(!wdg_fake_is_initialized());
    CHECK(wdg_fake_kick_count() == 0U);
    CHECK(wdg_fake_call_count() == 0U);
    CHECK(wdg_fake.read_and_clear_reset_cause() == WDG_RESET_CAUSE_SOFTWARE);
}
