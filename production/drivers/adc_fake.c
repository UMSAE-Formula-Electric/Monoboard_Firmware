/**
 * @file adc_fake.c
 * @brief Programmable fake ADC driver -- the AdcIf test double.
 */
#include "adc_fake.h"

#include <stddef.h>

typedef struct {
    bool              scanned;
    bool              has_sample;
    AdcSample         sample;
    AdcWatchdogLimits limits;
} FakeChannel;

static FakeChannel   channels[ADC_CH_COUNT];
static bool          scanning;
static AdcWatchdogCb watchdog_cb;
static void         *watchdog_ctx;

static bool channel_valid(AdcChannel channel)
{
    return (unsigned int)channel < (unsigned int)ADC_CH_COUNT;
}

static bool watchdog_armed(const AdcWatchdogLimits *limits)
{
    return limits->low != limits->high;
}

static bool watchdog_tripped(const AdcWatchdogLimits *limits, AdcSample sample)
{
    return watchdog_armed(limits) && ((sample < limits->low) || (sample > limits->high));
}

static IfStatus fake_start_scan(const AdcChannel       *chans,
                                size_t                  channel_count,
                                const AdcWatchdogLimits limits[ADC_CH_COUNT],
                                AdcWatchdogCb           on_watchdog,
                                void                   *ctx)
{
    size_t i;

    if ((chans == NULL) || (channel_count == 0U) || scanning) {
        return scanning ? IF_BUSY : IF_HW_FAULT;
    }
    for (i = 0U; i < channel_count; i++) {
        if (!channel_valid(chans[i])) {
            return IF_HW_FAULT;
        }
    }

    for (i = 0U; i < (size_t)ADC_CH_COUNT; i++) {
        channels[i].scanned    = false;
        channels[i].has_sample = false;
        channels[i].limits     = (limits != NULL) ? limits[i] : (AdcWatchdogLimits){0, 0};
    }
    for (i = 0U; i < channel_count; i++) {
        channels[chans[i]].scanned = true;
    }

    watchdog_cb  = on_watchdog;
    watchdog_ctx = ctx;
    scanning     = true;
    return IF_OK;
}

static IfStatus fake_stop_scan(void)
{
    if (!scanning) {
        return IF_HW_FAULT;
    }
    scanning = false;
    return IF_OK;
}

static IfStatus fake_read_latest(AdcChannel channel, AdcSample *out_value)
{
    if (!channel_valid(channel) || (out_value == NULL) || !channels[channel].scanned) {
        return IF_HW_FAULT;
    }
    if (!channels[channel].has_sample) {
        return IF_TIMEOUT;
    }
    *out_value = channels[channel].sample;
    return IF_OK;
}

const AdcIf adc_fake = {
    .start_scan  = fake_start_scan,
    .stop_scan   = fake_stop_scan,
    .read_latest = fake_read_latest,
};

void adc_fake_reset(void)
{
    size_t i;

    for (i = 0U; i < (size_t)ADC_CH_COUNT; i++) {
        channels[i].scanned    = false;
        channels[i].has_sample = false;
        channels[i].sample     = 0U;
        channels[i].limits     = (AdcWatchdogLimits){0, 0};
    }
    scanning     = false;
    watchdog_cb  = NULL;
    watchdog_ctx = NULL;
}

bool adc_fake_is_scanning(void)
{
    return scanning;
}

bool adc_fake_is_scanned(AdcChannel channel)
{
    return channel_valid(channel) && channels[channel].scanned;
}

void adc_fake_push_sample(AdcChannel channel, AdcSample sample)
{
    if (!channel_valid(channel) || !scanning || !channels[channel].scanned) {
        return;
    }
    channels[channel].sample     = sample;
    channels[channel].has_sample = true;

    if (watchdog_tripped(&channels[channel].limits, sample) && (watchdog_cb != NULL)) {
        watchdog_cb(watchdog_ctx, channel, sample);
    }
}
