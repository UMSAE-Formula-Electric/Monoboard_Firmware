/**
 * @file test_pwm_fake.c
 * @brief Exercises the PwmIf contract (production/interfaces/pwm_if.h)
 * through pwm_fake, the dependency-free double for pwm_stm32.
 */
#include <stddef.h>

#include "pwm_fake.h"
#include "test_framework.h"

static PwmConfig make_config(void)
{
    PwmConfig config = {.frequency_hz = 20000U, .duty_pct = 50U};
    return config;
}

TEST(pwm_fake, set_duty_before_init_fails)
{
    pwm_fake_reset();
    CHECK(pwm_fake.set_duty_pct(PWM_CH_TEST, 10U) == IF_HW_FAULT);
    CHECK(!pwm_fake_is_configured(PWM_CH_TEST));
}

TEST(pwm_fake, init_rejects_bad_config)
{
    PwmConfig zero_freq    = make_config();
    zero_freq.frequency_hz = 0U;
    PwmConfig over_duty    = make_config();
    over_duty.duty_pct     = 101U;

    pwm_fake_reset();
    CHECK(pwm_fake.init(PWM_CH_TEST, NULL) == IF_HW_FAULT);
    CHECK(pwm_fake.init(PWM_CH_TEST, &zero_freq) == IF_HW_FAULT);
    CHECK(pwm_fake.init(PWM_CH_TEST, &over_duty) == IF_HW_FAULT);
}

TEST(pwm_fake, init_sets_initial_duty_and_frequency)
{
    PwmConfig config = make_config();

    pwm_fake_reset();
    CHECK(pwm_fake.init(PWM_CH_TEST, &config) == IF_OK);
    CHECK(pwm_fake_is_configured(PWM_CH_TEST));
    CHECK(pwm_fake_duty_pct(PWM_CH_TEST) == 50U);
    CHECK(pwm_fake_frequency_hz(PWM_CH_TEST) == 20000U);
}

TEST(pwm_fake, set_duty_pct_updates_without_touching_frequency)
{
    PwmConfig config = make_config();

    pwm_fake_reset();
    CHECK(pwm_fake.init(PWM_CH_TEST, &config) == IF_OK);
    CHECK(pwm_fake.set_duty_pct(PWM_CH_TEST, 75U) == IF_OK);

    CHECK(pwm_fake_duty_pct(PWM_CH_TEST) == 75U);
    CHECK(pwm_fake_frequency_hz(PWM_CH_TEST) == 20000U);
}

TEST(pwm_fake, set_duty_pct_rejects_over_100)
{
    PwmConfig config = make_config();

    pwm_fake_reset();
    CHECK(pwm_fake.init(PWM_CH_TEST, &config) == IF_OK);
    CHECK(pwm_fake.set_duty_pct(PWM_CH_TEST, 101U) == IF_HW_FAULT);
    CHECK(pwm_fake_duty_pct(PWM_CH_TEST) == 50U); /* unchanged */
}

TEST(pwm_fake, set_frequency_hz_updates_without_touching_duty)
{
    PwmConfig config = make_config();

    pwm_fake_reset();
    CHECK(pwm_fake.init(PWM_CH_TEST, &config) == IF_OK);
    CHECK(pwm_fake.set_frequency_hz(PWM_CH_TEST, 10000U) == IF_OK);

    CHECK(pwm_fake_frequency_hz(PWM_CH_TEST) == 10000U);
    CHECK(pwm_fake_duty_pct(PWM_CH_TEST) == 50U);
}

TEST(pwm_fake, set_frequency_hz_rejects_zero)
{
    PwmConfig config = make_config();

    pwm_fake_reset();
    CHECK(pwm_fake.init(PWM_CH_TEST, &config) == IF_OK);
    CHECK(pwm_fake.set_frequency_hz(PWM_CH_TEST, 0U) == IF_HW_FAULT);
}

TEST(pwm_fake, reset_clears_configuration)
{
    PwmConfig config = make_config();

    pwm_fake_reset();
    CHECK(pwm_fake.init(PWM_CH_TEST, &config) == IF_OK);

    pwm_fake_reset();

    CHECK(!pwm_fake_is_configured(PWM_CH_TEST));
    CHECK(pwm_fake.set_duty_pct(PWM_CH_TEST, 10U) == IF_HW_FAULT);
}

TEST(pwm_fake, read_frequency_before_capture_fails)
{
    uint32_t frequency_hz;

    pwm_fake_reset();
    CHECK(pwm_fake.read_frequency(PWM_CAP_CH_TEST, &frequency_hz) == IF_HW_FAULT);
}

TEST(pwm_fake, start_capture_twice_is_busy)
{
    pwm_fake_reset();
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_BUSY);
}

TEST(pwm_fake, read_frequency_before_first_period_times_out)
{
    uint32_t frequency_hz;

    pwm_fake_reset();
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);
    CHECK(pwm_fake.read_frequency(PWM_CAP_CH_TEST, &frequency_hz) == IF_TIMEOUT);
}

TEST(pwm_fake, push_capture_updates_read_frequency)
{
    uint32_t frequency_hz;

    pwm_fake_reset();
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);

    pwm_fake_push_capture(PWM_CAP_CH_TEST, 500U);
    CHECK(pwm_fake.read_frequency(PWM_CAP_CH_TEST, &frequency_hz) == IF_OK);
    CHECK(frequency_hz == 500U);
}

TEST(pwm_fake, push_capture_while_not_capturing_is_ignored)
{
    uint32_t frequency_hz;

    pwm_fake_reset();
    pwm_fake_push_capture(PWM_CAP_CH_TEST, 500U);
    CHECK(pwm_fake.read_frequency(PWM_CAP_CH_TEST, &frequency_hz) == IF_HW_FAULT);
}

TEST(pwm_fake, stop_capture_keeps_last_known_good)
{
    uint32_t frequency_hz;

    pwm_fake_reset();
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);
    pwm_fake_push_capture(PWM_CAP_CH_TEST, 250U);

    CHECK(pwm_fake.stop_capture(PWM_CAP_CH_TEST) == IF_OK);
    CHECK(pwm_fake.stop_capture(PWM_CAP_CH_TEST) == IF_HW_FAULT); /* already stopped */
    CHECK(!pwm_fake_is_capturing(PWM_CAP_CH_TEST));

    /* Cached value from before the stop is still readable. */
    CHECK(pwm_fake.read_frequency(PWM_CAP_CH_TEST, &frequency_hz) == IF_OK);
    CHECK(frequency_hz == 250U);
}

TEST(pwm_fake, restarting_capture_clears_the_cached_value)
{
    uint32_t frequency_hz;

    pwm_fake_reset();
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);
    pwm_fake_push_capture(PWM_CAP_CH_TEST, 250U);
    CHECK(pwm_fake.stop_capture(PWM_CAP_CH_TEST) == IF_OK);

    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);
    CHECK(pwm_fake.read_frequency(PWM_CAP_CH_TEST, &frequency_hz) == IF_TIMEOUT);
}

TEST(pwm_fake, reset_clears_capture_state)
{
    pwm_fake_reset();
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);
    pwm_fake_push_capture(PWM_CAP_CH_TEST, 100U);

    pwm_fake_reset();

    CHECK(!pwm_fake_is_capturing(PWM_CAP_CH_TEST));
}
