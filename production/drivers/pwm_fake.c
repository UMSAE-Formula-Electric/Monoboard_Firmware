/**
 * @file pwm_fake.c
 * @brief Programmable fake PWM driver -- the PwmIf test double.
 */
#include "pwm_fake.h"

#include <stddef.h>

typedef struct {
    bool     configured;
    uint8_t  duty_pct;
    uint32_t frequency_hz;
} FakeChannel;

typedef struct {
    bool     capturing; /* actively capturing right now */
    bool     armed;     /* has been start_capture()'d at least once since reset */
    bool     has_frequency;
    uint32_t frequency_hz;
} FakeCaptureChannel;

static FakeChannel        channels[PWM_CH_COUNT];
static FakeCaptureChannel capture_channels[PWM_CAP_CH_COUNT];

static bool channel_valid(PwmChannel channel)
{
    return (unsigned int)channel < (unsigned int)PWM_CH_COUNT;
}

static bool capture_channel_valid(PwmCaptureChannel channel)
{
    return (unsigned int)channel < (unsigned int)PWM_CAP_CH_COUNT;
}

static IfStatus fake_init(PwmChannel channel, const PwmConfig *config)
{
    if (!channel_valid(channel) || (config == NULL) || (config->frequency_hz == 0U)
        || (config->duty_pct > 100U)) {
        return IF_HW_FAULT;
    }
    channels[channel].configured   = true;
    channels[channel].duty_pct     = config->duty_pct;
    channels[channel].frequency_hz = config->frequency_hz;
    return IF_OK;
}

static IfStatus fake_set_duty_pct(PwmChannel channel, uint8_t duty_pct)
{
    if (!channel_valid(channel) || (duty_pct > 100U) || !channels[channel].configured) {
        return IF_HW_FAULT;
    }
    channels[channel].duty_pct = duty_pct;
    return IF_OK;
}

static IfStatus fake_set_frequency_hz(PwmChannel channel, uint32_t frequency_hz)
{
    if (!channel_valid(channel) || (frequency_hz == 0U) || !channels[channel].configured) {
        return IF_HW_FAULT;
    }
    channels[channel].frequency_hz = frequency_hz;
    return IF_OK;
}

static IfStatus fake_start_capture(PwmCaptureChannel channel)
{
    if (!capture_channel_valid(channel)) {
        return IF_HW_FAULT;
    }
    if (capture_channels[channel].capturing) {
        return IF_BUSY;
    }
    capture_channels[channel].capturing     = true;
    capture_channels[channel].armed         = true;
    capture_channels[channel].has_frequency = false;
    return IF_OK;
}

static IfStatus fake_stop_capture(PwmCaptureChannel channel)
{
    if (!capture_channel_valid(channel) || !capture_channels[channel].capturing) {
        return IF_HW_FAULT;
    }
    capture_channels[channel].capturing = false;
    return IF_OK;
}

static IfStatus fake_read_frequency(PwmCaptureChannel channel, uint32_t *out_frequency_hz)
{
    if (!capture_channel_valid(channel) || (out_frequency_hz == NULL)
        || !capture_channels[channel].armed) {
        return IF_HW_FAULT;
    }
    if (!capture_channels[channel].has_frequency) {
        return IF_TIMEOUT;
    }
    *out_frequency_hz = capture_channels[channel].frequency_hz;
    return IF_OK;
}

const PwmIf pwm_fake = {
    .init             = fake_init,
    .set_duty_pct     = fake_set_duty_pct,
    .set_frequency_hz = fake_set_frequency_hz,
    .start_capture    = fake_start_capture,
    .stop_capture     = fake_stop_capture,
    .read_frequency   = fake_read_frequency,
};

void pwm_fake_reset(void)
{
    size_t i;

    for (i = 0U; i < (size_t)PWM_CH_COUNT; i++) {
        channels[i].configured   = false;
        channels[i].duty_pct     = 0U;
        channels[i].frequency_hz = 0U;
    }
    for (i = 0U; i < (size_t)PWM_CAP_CH_COUNT; i++) {
        capture_channels[i].capturing     = false;
        capture_channels[i].armed         = false;
        capture_channels[i].has_frequency = false;
        capture_channels[i].frequency_hz  = 0U;
    }
}

bool pwm_fake_is_configured(PwmChannel channel)
{
    return channel_valid(channel) && channels[channel].configured;
}

uint8_t pwm_fake_duty_pct(PwmChannel channel)
{
    return channel_valid(channel) ? channels[channel].duty_pct : 0U;
}

uint32_t pwm_fake_frequency_hz(PwmChannel channel)
{
    return channel_valid(channel) ? channels[channel].frequency_hz : 0U;
}

bool pwm_fake_is_capturing(PwmCaptureChannel channel)
{
    return capture_channel_valid(channel) && capture_channels[channel].capturing;
}

void pwm_fake_push_capture(PwmCaptureChannel channel, uint32_t frequency_hz)
{
    if (!capture_channel_valid(channel) || !capture_channels[channel].capturing) {
        return;
    }
    capture_channels[channel].frequency_hz  = frequency_hz;
    capture_channels[channel].has_frequency = true;
}
