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
