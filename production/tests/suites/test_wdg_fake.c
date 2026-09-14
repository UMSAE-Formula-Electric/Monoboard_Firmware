/**
 * @file test_wdg_fake.c
 * @brief Exercises the WdgIf contract (production/interfaces/wdg_if.h)
 * through wdg_fake, the dependency-free double for wdg_stm32.
 */
#include <stddef.h>

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
