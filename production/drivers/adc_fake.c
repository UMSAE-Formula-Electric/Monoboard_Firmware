/**
 * @file adc_fake.c
 * @brief Programmable fake ADC driver -- the AdcIf test double.
 */
#include "adc_fake.h"

typedef struct {
    bool              scanned;
    bool              has_sample;
    AdcSample         sample;
    AdcWatchdogLimits limits;
} FakeChannel;

/* The outside world one channel is wired to -- see adc_fake.h. */
typedef struct {
    bool             has_value;
    AdcSample        value;
    const AdcSample *sequence;
    size_t           sequence_left;
    AdcFakeRail      rail;
} FakeSensor;

static FakeChannel   channels[ADC_CH_COUNT];
static FakeSensor    sensors[ADC_CH_COUNT];
static AdcChannel    scan_order[ADC_FAKE_MAX_RANKS];
static size_t        scan_length;
static bool          scanning;
static bool          force_start_hw_fault;
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

    if (scanning) {
        return IF_BUSY;
    }
    if ((chans == NULL) || (channel_count == 0U) || (channel_count > ADC_FAKE_MAX_RANKS)) {
        return IF_HW_FAULT;
    }
    for (i = 0U; i < channel_count; i++) {
        if (!channel_valid(chans[i])) {
            return IF_HW_FAULT;
        }
    }
    if (force_start_hw_fault) {
        force_start_hw_fault = false;
        return IF_HW_FAULT;
    }

    for (i = 0U; i < (size_t)ADC_CH_COUNT; i++) {
        channels[i].scanned    = false;
        channels[i].has_sample = false;
        channels[i].limits     = (limits != NULL) ? limits[i] : (AdcWatchdogLimits){0, 0};
    }
    for (i = 0U; i < channel_count; i++) {
        channels[chans[i]].scanned = true;
        scan_order[i]              = chans[i];
    }
    scan_length = channel_count;

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
        channels[i].scanned      = false;
        channels[i].has_sample   = false;
        channels[i].sample       = 0U;
        channels[i].limits       = (AdcWatchdogLimits){0, 0};
        sensors[i].has_value     = false;
        sensors[i].value         = 0U;
        sensors[i].sequence      = NULL;
        sensors[i].sequence_left = 0U;
        sensors[i].rail          = ADC_FAKE_RAIL_NONE;
    }
    scan_length          = 0U;
    scanning             = false;
    force_start_hw_fault = false;
    watchdog_cb          = NULL;
    watchdog_ctx         = NULL;
}

bool adc_fake_is_scanning(void)
{
    return scanning;
}

bool adc_fake_is_scanned(AdcChannel channel)
{
    return channel_valid(channel) && channels[channel].scanned;
}

void adc_fake_force_start_hw_fault(void)
{
    force_start_hw_fault = true;
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

void adc_fake_set_value(AdcChannel channel, AdcSample sample)
{
    if (!channel_valid(channel)) {
        return;
    }
    sensors[channel].has_value     = true;
    sensors[channel].value         = sample;
    sensors[channel].sequence      = NULL;
    sensors[channel].sequence_left = 0U;
}

void adc_fake_play_sequence(AdcChannel channel, const AdcSample *samples, size_t count)
{
    if (!channel_valid(channel)) {
        return;
    }
    if ((samples == NULL) || (count == 0U)) {
        sensors[channel].sequence      = NULL;
        sensors[channel].sequence_left = 0U;
        return;
    }
    sensors[channel].sequence      = samples;
    sensors[channel].sequence_left = count;
}

size_t adc_fake_sequence_remaining(AdcChannel channel)
{
    return channel_valid(channel) ? sensors[channel].sequence_left : 0U;
}

void adc_fake_set_rail(AdcChannel channel, AdcFakeRail rail)
{
    if (!channel_valid(channel)) {
        return;
    }
    sensors[channel].rail = rail;
}

/* Advance @p sensor one scan: consume the next scripted sample (which then
 * becomes the held value) and report whether the sensor has anything to
 * convert. */
static bool sensor_advance(FakeSensor *sensor)
{
    if (sensor->sequence_left > 0U) {
        sensor->value     = sensor->sequence[0];
        sensor->has_value = true;
        sensor->sequence++;
        sensor->sequence_left--;
    }
    return sensor->has_value;
}

void adc_fake_scan(void)
{
    size_t slot;

    if (!scanning) {
        return;
    }
    for (slot = 0U; slot < scan_length; slot++) {
        AdcChannel  channel = scan_order[slot];
        FakeSensor *sensor  = &sensors[channel];
        bool        live    = sensor_advance(sensor);

        if (sensor->rail == ADC_FAKE_RAIL_LOW) {
            adc_fake_push_sample(channel, 0U);
        } else if (sensor->rail == ADC_FAKE_RAIL_HIGH) {
            adc_fake_push_sample(channel, (AdcSample)ADC_FAKE_FULL_SCALE);
        } else if (live) {
            adc_fake_push_sample(channel, sensor->value);
        } else {
            /* nothing wired to this channel yet: no conversion */
        }
    }
}

bool adc_fake_inject_watchdog(AdcChannel channel, AdcSample sample)
{
    if (!channel_valid(channel) || !scanning || !channels[channel].scanned
        || (watchdog_cb == NULL)) {
        return false;
    }
    watchdog_cb(watchdog_ctx, channel, sample);
    return true;
}
