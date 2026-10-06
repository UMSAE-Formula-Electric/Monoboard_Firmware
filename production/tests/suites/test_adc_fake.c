/**
 * @file test_adc_fake.c
 * @brief Exercises the AdcIf contract (production/interfaces/adc_if.h)
 * through adc_fake, the dependency-free double for adc_stm32.
 */
#include <stddef.h>

#include "adc_fake.h"
#include "test_framework.h"

typedef struct {
    int        call_count;
    void      *last_ctx;
    AdcChannel last_channel;
    AdcSample  last_sample;
} WatchdogRecord;

static WatchdogRecord watchdog;

static void on_watchdog(void *ctx, AdcChannel channel, AdcSample sample)
{
    watchdog.call_count++;
    watchdog.last_ctx     = ctx;
    watchdog.last_channel = channel;
    watchdog.last_sample  = sample;
}

static void watchdog_reset(void)
{
    watchdog.call_count   = 0;
    watchdog.last_ctx     = NULL;
    watchdog.last_channel = ADC_CH_TEST;
    watchdog.last_sample  = 0U;
}

TEST(adc_fake, read_latest_before_scan_fails)
{
    AdcSample value;

    adc_fake_reset();
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_HW_FAULT);
}

TEST(adc_fake, start_scan_rejects_null_or_empty_channels)
{
    const AdcChannel chans[] = {ADC_CH_TEST};

    adc_fake_reset();
    CHECK(adc_fake.start_scan(NULL, 1U, NULL, NULL, NULL) == IF_HW_FAULT);
    CHECK(adc_fake.start_scan(chans, 0U, NULL, NULL, NULL) == IF_HW_FAULT);
}

TEST(adc_fake, start_scan_twice_is_busy)
{
    const AdcChannel chans[] = {ADC_CH_TEST};

    adc_fake_reset();
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_BUSY);
}

TEST(adc_fake, read_latest_before_any_conversion_times_out)
{
    const AdcChannel chans[] = {ADC_CH_TEST};
    AdcSample        value;

    adc_fake_reset();
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_TIMEOUT);
}

TEST(adc_fake, push_sample_updates_read_latest)
{
    const AdcChannel chans[] = {ADC_CH_TEST};
    AdcSample        value;

    adc_fake_reset();
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);

    adc_fake_push_sample(ADC_CH_TEST, 1234U);
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_OK);
    CHECK(value == 1234U);
}

TEST(adc_fake, stop_scan_keeps_last_known_good)
{
    const AdcChannel chans[] = {ADC_CH_TEST};
    AdcSample        value;

    adc_fake_reset();
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
    adc_fake_push_sample(ADC_CH_TEST, 42U);

    CHECK(adc_fake.stop_scan() == IF_OK);
    CHECK(adc_fake.stop_scan() == IF_HW_FAULT); /* already stopped */

    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_OK);
    CHECK(value == 42U);
}

TEST(adc_fake, out_of_range_sample_fires_watchdog)
{
    const AdcChannel  chans[]              = {ADC_CH_TEST};
    AdcWatchdogLimits limits[ADC_CH_COUNT] = {0};

    limits[ADC_CH_TEST] = (AdcWatchdogLimits){.low = 100U, .high = 4000U};

    adc_fake_reset();
    watchdog_reset();
    CHECK(adc_fake.start_scan(chans, 1U, limits, on_watchdog, (void *)0xABCD) == IF_OK);

    adc_fake_push_sample(ADC_CH_TEST, 2000U); /* in range */
    CHECK(watchdog.call_count == 0);

    adc_fake_push_sample(ADC_CH_TEST, 4001U); /* pinned high */
    CHECK(watchdog.call_count == 1);
    CHECK(watchdog.last_ctx == (void *)0xABCD);
    CHECK(watchdog.last_channel == ADC_CH_TEST);
    CHECK(watchdog.last_sample == 4001U);
}

TEST(adc_fake, unarmed_watchdog_never_fires)
{
    const AdcChannel chans[] = {ADC_CH_TEST};

    adc_fake_reset();
    watchdog_reset();
    CHECK(adc_fake.start_scan(chans, 1U, NULL, on_watchdog, NULL) == IF_OK);

    adc_fake_push_sample(ADC_CH_TEST, 65535U);
    CHECK(watchdog.call_count == 0);
}

TEST(adc_fake, reset_clears_scan_state)
{
    const AdcChannel chans[] = {ADC_CH_TEST};
    AdcSample        value;

    adc_fake_reset();
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
    adc_fake_push_sample(ADC_CH_TEST, 10U);

    adc_fake_reset();

    CHECK(!adc_fake_is_scanning());
    CHECK(!adc_fake_is_scanned(ADC_CH_TEST));
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_HW_FAULT);
}

TEST(adc_fake, start_scan_rejects_too_many_ranks)
{
    AdcChannel chans[ADC_FAKE_MAX_RANKS + 1U];
    size_t     i;

    for (i = 0U; i < (ADC_FAKE_MAX_RANKS + 1U); i++) {
        chans[i] = ADC_CH_TEST;
    }

    adc_fake_reset();
    CHECK(adc_fake.start_scan(chans, ADC_FAKE_MAX_RANKS + 1U, NULL, NULL, NULL) == IF_HW_FAULT);
    CHECK(adc_fake.start_scan(chans, ADC_FAKE_MAX_RANKS, NULL, NULL, NULL) == IF_OK);
}

TEST(adc_fake, start_scan_rejects_unknown_channel)
{
    const AdcChannel chans[] = {ADC_CH_COUNT};

    adc_fake_reset();
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_HW_FAULT);
    CHECK(!adc_fake_is_scanning());
}

TEST(adc_fake, forced_start_hw_fault_is_one_shot)
{
    const AdcChannel chans[] = {ADC_CH_TEST};

    adc_fake_reset();
    adc_fake_force_start_hw_fault();
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_HW_FAULT);
    CHECK(!adc_fake_is_scanning());
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
}

TEST(adc_fake, read_latest_rejects_null_out)
{
    const AdcChannel chans[] = {ADC_CH_TEST};

    adc_fake_reset();
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
    adc_fake_push_sample(ADC_CH_TEST, 1U);
    CHECK(adc_fake.read_latest(ADC_CH_TEST, NULL) == IF_HW_FAULT);
}

TEST(adc_fake, scan_with_nothing_wired_yields_no_conversion)
{
    const AdcChannel chans[] = {ADC_CH_TEST};
    AdcSample        value;

    adc_fake_reset();
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
    adc_fake_scan();
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_TIMEOUT);
}

TEST(adc_fake, held_value_is_converted_on_every_scan)
{
    const AdcChannel chans[] = {ADC_CH_TEST};
    AdcSample        value   = 0U;

    adc_fake_reset();
    adc_fake_set_value(ADC_CH_TEST, 2048U); /* sensor world is set before the scan */
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_TIMEOUT); /* no scan yet */

    adc_fake_scan();
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_OK);
    CHECK(value == 2048U);

    adc_fake_set_value(ADC_CH_TEST, 100U);
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_OK);
    CHECK(value == 2048U); /* unchanged until the next scan */
    adc_fake_scan();
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_OK);
    CHECK(value == 100U);
}

TEST(adc_fake, scan_is_noop_while_stopped)
{
    const AdcChannel chans[] = {ADC_CH_TEST};
    AdcSample        value   = 0U;

    adc_fake_reset();
    adc_fake_set_value(ADC_CH_TEST, 7U);
    adc_fake_scan(); /* not scanning yet */
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_TIMEOUT);
}

TEST(adc_fake, sequence_plays_one_sample_per_scan_then_holds)
{
    static const AdcSample ramp[]  = {0U, 1000U, 2000U, 3000U};
    const AdcChannel       chans[] = {ADC_CH_TEST};
    AdcSample              value   = 0U;
    size_t                 i;

    adc_fake_reset();
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
    adc_fake_play_sequence(ADC_CH_TEST, ramp, 4U);
    CHECK(adc_fake_sequence_remaining(ADC_CH_TEST) == 4U);

    for (i = 0U; i < 4U; i++) {
        adc_fake_scan();
        CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_OK);
        CHECK(value == ramp[i]);
    }
    CHECK(adc_fake_sequence_remaining(ADC_CH_TEST) == 0U);

    adc_fake_scan(); /* ramp ended: hold the last sample */
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_OK);
    CHECK(value == 3000U);
}

TEST(adc_fake, set_value_replaces_playing_sequence)
{
    static const AdcSample ramp[]  = {10U, 20U, 30U};
    const AdcChannel       chans[] = {ADC_CH_TEST};
    AdcSample              value   = 0U;

    adc_fake_reset();
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
    adc_fake_play_sequence(ADC_CH_TEST, ramp, 3U);
    adc_fake_scan();
    adc_fake_set_value(ADC_CH_TEST, 500U);
    CHECK(adc_fake_sequence_remaining(ADC_CH_TEST) == 0U);

    adc_fake_scan();
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_OK);
    CHECK(value == 500U);
}

TEST(adc_fake, empty_sequence_cancels_and_keeps_held_value)
{
    static const AdcSample ramp[]  = {10U, 20U, 30U};
    const AdcChannel       chans[] = {ADC_CH_TEST};
    AdcSample              value   = 0U;

    adc_fake_reset();
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
    adc_fake_play_sequence(ADC_CH_TEST, ramp, 3U);
    adc_fake_scan();
    adc_fake_play_sequence(ADC_CH_TEST, NULL, 0U);
    CHECK(adc_fake_sequence_remaining(ADC_CH_TEST) == 0U);

    adc_fake_scan();
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_OK);
    CHECK(value == 10U);
}

TEST(adc_fake, sequence_ramp_trips_watchdog_only_out_of_band)
{
    static const AdcSample ramp[]               = {50U, 200U, 3900U, 4050U};
    const AdcChannel       chans[]              = {ADC_CH_TEST};
    AdcWatchdogLimits      limits[ADC_CH_COUNT] = {0};

    limits[ADC_CH_TEST] = (AdcWatchdogLimits){.low = 100U, .high = 4000U};

    adc_fake_reset();
    watchdog_reset();
    CHECK(adc_fake.start_scan(chans, 1U, limits, on_watchdog, NULL) == IF_OK);
    adc_fake_play_sequence(ADC_CH_TEST, ramp, 4U);

    adc_fake_scan(); /* 50: below band */
    CHECK(watchdog.call_count == 1);
    CHECK(watchdog.last_sample == 50U);
    adc_fake_scan(); /* 200 */
    adc_fake_scan(); /* 3900 */
    CHECK(watchdog.call_count == 1);
    adc_fake_scan(); /* 4050: above band */
    CHECK(watchdog.call_count == 2);
    CHECK(watchdog.last_sample == 4050U);
}

TEST(adc_fake, open_circuit_pins_high_and_fires_every_scan)
{
    const AdcChannel  chans[]              = {ADC_CH_TEST};
    AdcWatchdogLimits limits[ADC_CH_COUNT] = {0};
    AdcSample         value                = 0U;

    limits[ADC_CH_TEST] = (AdcWatchdogLimits){.low = 100U, .high = 4000U};

    adc_fake_reset();
    watchdog_reset();
    adc_fake_set_value(ADC_CH_TEST, 2000U);
    CHECK(adc_fake.start_scan(chans, 1U, limits, on_watchdog, NULL) == IF_OK);

    adc_fake_set_rail(ADC_CH_TEST, ADC_FAKE_RAIL_HIGH);
    adc_fake_scan();
    adc_fake_scan();
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_OK);
    CHECK(value == ADC_FAKE_FULL_SCALE);
    CHECK(watchdog.call_count == 2);
    CHECK(watchdog.last_sample == ADC_FAKE_FULL_SCALE);

    adc_fake_set_rail(ADC_CH_TEST, ADC_FAKE_RAIL_NONE); /* wire reconnected */
    adc_fake_scan();
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_OK);
    CHECK(value == 2000U);
    CHECK(watchdog.call_count == 2);
}

TEST(adc_fake, short_to_ground_pins_low)
{
    const AdcChannel  chans[]              = {ADC_CH_TEST};
    AdcWatchdogLimits limits[ADC_CH_COUNT] = {0};
    AdcSample         value                = 1U;

    limits[ADC_CH_TEST] = (AdcWatchdogLimits){.low = 100U, .high = 4000U};

    adc_fake_reset();
    watchdog_reset();
    CHECK(adc_fake.start_scan(chans, 1U, limits, on_watchdog, NULL) == IF_OK);

    adc_fake_set_rail(ADC_CH_TEST, ADC_FAKE_RAIL_LOW); /* pinned even with nothing else wired */
    adc_fake_scan();
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_OK);
    CHECK(value == 0U);
    CHECK(watchdog.call_count == 1);
    CHECK(watchdog.last_sample == 0U);
}

TEST(adc_fake, sequence_keeps_advancing_while_pinned)
{
    static const AdcSample ramp[]  = {10U, 20U, 30U};
    const AdcChannel       chans[] = {ADC_CH_TEST};
    AdcSample              value   = 0U;

    adc_fake_reset();
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
    adc_fake_play_sequence(ADC_CH_TEST, ramp, 3U);

    adc_fake_set_rail(ADC_CH_TEST, ADC_FAKE_RAIL_HIGH);
    adc_fake_scan(); /* consumes 10 underneath */
    adc_fake_scan(); /* consumes 20 underneath */
    CHECK(adc_fake_sequence_remaining(ADC_CH_TEST) == 1U);

    adc_fake_set_rail(ADC_CH_TEST, ADC_FAKE_RAIL_NONE);
    adc_fake_scan();
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_OK);
    CHECK(value == 30U);
}

TEST(adc_fake, injected_watchdog_bypasses_limits_and_cache)
{
    const AdcChannel chans[] = {ADC_CH_TEST};
    AdcSample        value   = 0U;

    adc_fake_reset();
    watchdog_reset();
    CHECK(!adc_fake_inject_watchdog(ADC_CH_TEST, 9U)); /* not scanning */

    CHECK(adc_fake.start_scan(chans, 1U, NULL, on_watchdog, (void *)0x1234) == IF_OK);
    adc_fake_push_sample(ADC_CH_TEST, 1500U);

    CHECK(adc_fake_inject_watchdog(ADC_CH_TEST, 9U)); /* no limits configured */
    CHECK(watchdog.call_count == 1);
    CHECK(watchdog.last_ctx == (void *)0x1234);
    CHECK(watchdog.last_channel == ADC_CH_TEST);
    CHECK(watchdog.last_sample == 9U);

    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_OK);
    CHECK(value == 1500U); /* cache untouched */
}

TEST(adc_fake, injected_watchdog_needs_a_callback)
{
    const AdcChannel chans[] = {ADC_CH_TEST};

    adc_fake_reset();
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
    CHECK(!adc_fake_inject_watchdog(ADC_CH_TEST, 9U));
    CHECK(!adc_fake_inject_watchdog(ADC_CH_COUNT, 9U));
}

TEST(adc_fake, sensor_world_survives_restart_but_not_reset)
{
    const AdcChannel chans[] = {ADC_CH_TEST};
    AdcSample        value   = 0U;

    adc_fake_reset();
    adc_fake_set_value(ADC_CH_TEST, 321U);
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
    CHECK(adc_fake.stop_scan() == IF_OK);
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
    adc_fake_scan();
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_OK);
    CHECK(value == 321U);

    adc_fake_reset();
    CHECK(adc_fake.start_scan(chans, 1U, NULL, NULL, NULL) == IF_OK);
    adc_fake_scan();
    CHECK(adc_fake.read_latest(ADC_CH_TEST, &value) == IF_TIMEOUT);
}
