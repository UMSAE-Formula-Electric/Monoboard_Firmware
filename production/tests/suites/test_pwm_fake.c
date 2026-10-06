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

    pwm_fake_push_capture(PWM_CAP_CH_TEST, 500U, 250U);
    CHECK(pwm_fake.read_frequency(PWM_CAP_CH_TEST, &frequency_hz) == IF_OK);
    CHECK(frequency_hz == 500U);
}

TEST(pwm_fake, push_capture_while_not_capturing_is_ignored)
{
    uint32_t frequency_hz;

    pwm_fake_reset();
    pwm_fake_push_capture(PWM_CAP_CH_TEST, 500U, 250U);
    CHECK(pwm_fake.read_frequency(PWM_CAP_CH_TEST, &frequency_hz) == IF_HW_FAULT);
}

TEST(pwm_fake, stop_capture_keeps_last_known_good)
{
    uint32_t frequency_hz;

    pwm_fake_reset();
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);
    pwm_fake_push_capture(PWM_CAP_CH_TEST, 250U, 500U);

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
    pwm_fake_push_capture(PWM_CAP_CH_TEST, 250U, 500U);
    CHECK(pwm_fake.stop_capture(PWM_CAP_CH_TEST) == IF_OK);

    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);
    CHECK(pwm_fake.read_frequency(PWM_CAP_CH_TEST, &frequency_hz) == IF_TIMEOUT);
}

TEST(pwm_fake, reset_clears_capture_state)
{
    pwm_fake_reset();
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);
    pwm_fake_push_capture(PWM_CAP_CH_TEST, 100U, 0U);

    pwm_fake_reset();

    CHECK(!pwm_fake_is_capturing(PWM_CAP_CH_TEST));
}

TEST(pwm_fake, duty_endpoints_are_exact)
{
    PwmConfig config = make_config();

    pwm_fake_reset();
    CHECK(pwm_fake.init(PWM_CH_TEST, &config) == IF_OK);
    CHECK(pwm_fake.set_duty_pct(PWM_CH_TEST, 0U) == IF_OK);
    CHECK(pwm_fake_duty_pct(PWM_CH_TEST) == 0U);
    CHECK(pwm_fake.set_duty_pct(PWM_CH_TEST, 25U) == IF_OK);
    CHECK(pwm_fake_duty_pct(PWM_CH_TEST) == 25U);
    CHECK(pwm_fake.set_duty_pct(PWM_CH_TEST, 100U) == IF_OK);
    CHECK(pwm_fake_duty_pct(PWM_CH_TEST) == 100U);
}

TEST(pwm_fake, unknown_channels_are_rejected)
{
    PwmConfig config = make_config();
    uint32_t  frequency_hz;
    uint16_t  duty_permille;

    pwm_fake_reset();
    CHECK(pwm_fake.init(PWM_CH_COUNT, &config) == IF_HW_FAULT);
    CHECK(pwm_fake.set_duty_pct(PWM_CH_COUNT, 10U) == IF_HW_FAULT);
    CHECK(pwm_fake.set_frequency_hz(PWM_CH_COUNT, 1000U) == IF_HW_FAULT);
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_COUNT) == IF_HW_FAULT);
    CHECK(pwm_fake.stop_capture(PWM_CAP_CH_COUNT) == IF_HW_FAULT);
    CHECK(pwm_fake.read_frequency(PWM_CAP_CH_COUNT, &frequency_hz) == IF_HW_FAULT);
    CHECK(pwm_fake.read_duty_permille(PWM_CAP_CH_COUNT, &duty_permille) == IF_HW_FAULT);
}

TEST(pwm_fake, read_with_null_out_fails)
{
    pwm_fake_reset();
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);
    pwm_fake_push_capture(PWM_CAP_CH_TEST, 20U, 500U);
    CHECK(pwm_fake.read_frequency(PWM_CAP_CH_TEST, NULL) == IF_HW_FAULT);
    CHECK(pwm_fake.read_duty_permille(PWM_CAP_CH_TEST, NULL) == IF_HW_FAULT);
}

TEST(pwm_fake, push_capture_updates_read_duty_permille)
{
    uint16_t duty_permille;

    pwm_fake_reset();
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);
    CHECK(pwm_fake.read_duty_permille(PWM_CAP_CH_TEST, &duty_permille) == IF_TIMEOUT);

    pwm_fake_push_capture(PWM_CAP_CH_TEST, 10U, 950U);
    CHECK(pwm_fake.read_duty_permille(PWM_CAP_CH_TEST, &duty_permille) == IF_OK);
    CHECK(duty_permille == 950U);
}

TEST(pwm_fake, push_capture_rejects_duty_over_1000)
{
    uint16_t duty_permille;

    pwm_fake_reset();
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);
    pwm_fake_push_capture(PWM_CAP_CH_TEST, 10U, 1001U);
    CHECK(pwm_fake.read_duty_permille(PWM_CAP_CH_TEST, &duty_permille) == IF_TIMEOUT);
}

TEST(pwm_fake, measured_zero_duty_is_distinct_from_silence)
{
    uint32_t frequency_hz;
    uint16_t duty_permille;

    pwm_fake_reset();
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);

    pwm_fake_push_capture(PWM_CAP_CH_TEST, 30U, 0U);
    CHECK(pwm_fake.read_duty_permille(PWM_CAP_CH_TEST, &duty_permille) == IF_OK);
    CHECK(duty_permille == 0U);

    pwm_fake_capture_silence(PWM_CAP_CH_TEST);
    CHECK(pwm_fake.read_duty_permille(PWM_CAP_CH_TEST, &duty_permille) == IF_TIMEOUT);
    CHECK(pwm_fake.read_frequency(PWM_CAP_CH_TEST, &frequency_hz) == IF_TIMEOUT);

    /* Signal comes back: readings resume. */
    pwm_fake_push_capture(PWM_CAP_CH_TEST, 30U, 400U);
    CHECK(pwm_fake.read_frequency(PWM_CAP_CH_TEST, &frequency_hz) == IF_OK);
    CHECK(frequency_hz == 30U);
}

TEST(pwm_fake, silence_while_not_capturing_is_ignored)
{
    uint32_t frequency_hz;

    pwm_fake_reset();
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);
    pwm_fake_push_capture(PWM_CAP_CH_TEST, 40U, 500U);
    CHECK(pwm_fake.stop_capture(PWM_CAP_CH_TEST) == IF_OK);

    pwm_fake_capture_silence(PWM_CAP_CH_TEST);
    CHECK(pwm_fake.read_frequency(PWM_CAP_CH_TEST, &frequency_hz) == IF_OK);
    CHECK(frequency_hz == 40U);
}

TEST(pwm_fake, call_log_records_arguments_and_results)
{
    PwmConfig   config = make_config();
    PwmFakeCall call;

    pwm_fake_reset();
    CHECK(pwm_fake.init(PWM_CH_TEST, &config) == IF_OK);
    CHECK(pwm_fake.set_duty_pct(PWM_CH_TEST, 101U) == IF_HW_FAULT);
    CHECK(pwm_fake.set_frequency_hz(PWM_CH_TEST, 25000U) == IF_OK);
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);
    CHECK(pwm_fake_call_count() == 4U);

    CHECK(pwm_fake_pop_call(&call));
    CHECK(call.op == PWM_FAKE_OP_INIT);
    CHECK(call.channel == (unsigned int)PWM_CH_TEST);
    CHECK(call.frequency_hz == 20000U);
    CHECK(call.duty_pct == 50U);
    CHECK(call.result == IF_OK);

    CHECK(pwm_fake_pop_call(&call));
    CHECK(call.op == PWM_FAKE_OP_SET_DUTY_PCT);
    CHECK(call.duty_pct == 101U);
    CHECK(call.result == IF_HW_FAULT);

    CHECK(pwm_fake_pop_call(&call));
    CHECK(call.op == PWM_FAKE_OP_SET_FREQUENCY_HZ);
    CHECK(call.frequency_hz == 25000U);
    CHECK(call.result == IF_OK);

    CHECK(pwm_fake_pop_call(&call));
    CHECK(call.op == PWM_FAKE_OP_START_CAPTURE);
    CHECK(call.channel == (unsigned int)PWM_CAP_CH_TEST);

    CHECK(!pwm_fake_pop_call(&call));
    CHECK(pwm_fake_call_count() == 0U);
}

TEST(pwm_fake, call_log_stops_recording_when_full)
{
    PwmConfig config = make_config();
    size_t    i;

    pwm_fake_reset();
    CHECK(pwm_fake.init(PWM_CH_TEST, &config) == IF_OK);
    for (i = 0U; i < PWM_FAKE_CALL_LOG_DEPTH; i++) {
        CHECK(pwm_fake.set_duty_pct(PWM_CH_TEST, 10U) == IF_OK);
    }
    CHECK(pwm_fake_call_count() == PWM_FAKE_CALL_LOG_DEPTH);
    /* The call still works even though it is no longer logged. */
    CHECK(pwm_fake_duty_pct(PWM_CH_TEST) == 10U);
}

TEST(pwm_fake, forced_init_fault_has_no_side_effects)
{
    PwmConfig config = make_config();

    pwm_fake_reset();
    pwm_fake_force_next(PWM_FAKE_OP_INIT, IF_HW_FAULT);
    CHECK(pwm_fake.init(PWM_CH_TEST, &config) == IF_HW_FAULT);
    CHECK(!pwm_fake_is_configured(PWM_CH_TEST));

    /* One-shot: the next call behaves normally. */
    CHECK(pwm_fake.init(PWM_CH_TEST, &config) == IF_OK);
}

TEST(pwm_fake, forced_set_faults_leave_outputs_unchanged)
{
    PwmConfig config = make_config();

    pwm_fake_reset();
    CHECK(pwm_fake.init(PWM_CH_TEST, &config) == IF_OK);

    pwm_fake_force_next(PWM_FAKE_OP_SET_DUTY_PCT, IF_HW_FAULT);
    CHECK(pwm_fake.set_duty_pct(PWM_CH_TEST, 80U) == IF_HW_FAULT);
    CHECK(pwm_fake_duty_pct(PWM_CH_TEST) == 50U);

    pwm_fake_force_next(PWM_FAKE_OP_SET_FREQUENCY_HZ, IF_HW_FAULT);
    CHECK(pwm_fake.set_frequency_hz(PWM_CH_TEST, 1000U) == IF_HW_FAULT);
    CHECK(pwm_fake_frequency_hz(PWM_CH_TEST) == 20000U);
}

TEST(pwm_fake, forced_capture_failures)
{
    uint32_t frequency_hz;
    uint16_t duty_permille;

    pwm_fake_reset();
    pwm_fake_force_next(PWM_FAKE_OP_START_CAPTURE, IF_BUSY);
    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_BUSY);
    CHECK(!pwm_fake_is_capturing(PWM_CAP_CH_TEST));

    CHECK(pwm_fake.start_capture(PWM_CAP_CH_TEST) == IF_OK);
    pwm_fake_push_capture(PWM_CAP_CH_TEST, 50U, 500U);

    pwm_fake_force_next(PWM_FAKE_OP_READ_FREQUENCY, IF_TIMEOUT);
    CHECK(pwm_fake.read_frequency(PWM_CAP_CH_TEST, &frequency_hz) == IF_TIMEOUT);
    pwm_fake_force_next(PWM_FAKE_OP_READ_DUTY_PERMILLE, IF_HW_FAULT);
    CHECK(pwm_fake.read_duty_permille(PWM_CAP_CH_TEST, &duty_permille) == IF_HW_FAULT);

    pwm_fake_force_next(PWM_FAKE_OP_STOP_CAPTURE, IF_HW_FAULT);
    CHECK(pwm_fake.stop_capture(PWM_CAP_CH_TEST) == IF_HW_FAULT);
    CHECK(pwm_fake_is_capturing(PWM_CAP_CH_TEST));
}

TEST(pwm_fake, forcing_ok_cancels_a_pending_force)
{
    PwmConfig config = make_config();

    pwm_fake_reset();
    pwm_fake_force_next(PWM_FAKE_OP_INIT, IF_HW_FAULT);
    pwm_fake_force_next(PWM_FAKE_OP_INIT, IF_OK);
    CHECK(pwm_fake.init(PWM_CH_TEST, &config) == IF_OK);
}

TEST(pwm_fake, reset_clears_forced_results_and_call_log)
{
    PwmConfig config = make_config();

    pwm_fake_reset();
    CHECK(pwm_fake.init(PWM_CH_TEST, &config) == IF_OK);
    pwm_fake_force_next(PWM_FAKE_OP_INIT, IF_HW_FAULT);

    pwm_fake_reset();

    CHECK(pwm_fake_call_count() == 0U);
    CHECK(pwm_fake.init(PWM_CH_TEST, &config) == IF_OK);
}
