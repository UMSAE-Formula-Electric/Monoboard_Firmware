/**
 * @file pwm_fake.c
 * @brief Programmable fake PWM driver -- the PwmIf test double.
 */
#include "pwm_fake.h"

typedef struct {
    bool     configured;
    uint8_t  duty_pct;
    uint32_t frequency_hz;
} FakeChannel;

typedef struct {
    bool     capturing;  /* actively capturing right now */
    bool     armed;      /* has been start_capture()'d at least once since reset */
    bool     has_sample; /* a full period has been measured and is not stale */
    uint32_t frequency_hz;
    uint16_t duty_permille;
} FakeCaptureChannel;

typedef struct {
    bool     pending;
    IfStatus status;
} ForcedResult;

static FakeChannel        channels[PWM_CH_COUNT];
static FakeCaptureChannel capture_channels[PWM_CAP_CH_COUNT];

static PwmFakeCall call_log[PWM_FAKE_CALL_LOG_DEPTH];
static size_t      call_log_head;
static size_t      call_log_tail;
static size_t      call_log_count;

static ForcedResult forced[PWM_FAKE_OP_COUNT];

static bool channel_valid(PwmChannel channel)
{
    return (unsigned int)channel < (unsigned int)PWM_CH_COUNT;
}

static bool capture_channel_valid(PwmCaptureChannel channel)
{
    return (unsigned int)channel < (unsigned int)PWM_CAP_CH_COUNT;
}

/* Append one call to the log (dropped if the log is full) and hand back
 * result, so every entry point can end with `return record(...)`. */
static IfStatus
record(PwmFakeOp op, unsigned int channel, uint32_t frequency_hz, uint8_t duty_pct, IfStatus result)
{
    if (call_log_count < PWM_FAKE_CALL_LOG_DEPTH) {
        call_log[call_log_head].op           = op;
        call_log[call_log_head].channel      = channel;
        call_log[call_log_head].frequency_hz = frequency_hz;
        call_log[call_log_head].duty_pct     = duty_pct;
        call_log[call_log_head].result       = result;
        call_log_head                        = (call_log_head + 1U) % PWM_FAKE_CALL_LOG_DEPTH;
        call_log_count++;
    }
    return result;
}

/* Consume a pending forced result for op, if there is one. */
static bool take_forced(PwmFakeOp op, IfStatus *status)
{
    if (!forced[op].pending) {
        return false;
    }
    forced[op].pending = false;
    *status            = forced[op].status;
    return true;
}

static IfStatus fake_init(PwmChannel channel, const PwmConfig *config)
{
    uint32_t frequency_hz = (config != NULL) ? config->frequency_hz : 0U;
    uint8_t  duty_pct     = (config != NULL) ? config->duty_pct : 0U;
    IfStatus status;

    if (take_forced(PWM_FAKE_OP_INIT, &status)) {
        return record(PWM_FAKE_OP_INIT, (unsigned int)channel, frequency_hz, duty_pct, status);
    }
    if (!channel_valid(channel) || (config == NULL) || (frequency_hz == 0U) || (duty_pct > 100U)) {
        return record(PWM_FAKE_OP_INIT, (unsigned int)channel, frequency_hz, duty_pct, IF_HW_FAULT);
    }
    channels[channel].configured   = true;
    channels[channel].duty_pct     = duty_pct;
    channels[channel].frequency_hz = frequency_hz;
    return record(PWM_FAKE_OP_INIT, (unsigned int)channel, frequency_hz, duty_pct, IF_OK);
}

static IfStatus fake_set_duty_pct(PwmChannel channel, uint8_t duty_pct)
{
    IfStatus status;

    if (take_forced(PWM_FAKE_OP_SET_DUTY_PCT, &status)) {
        return record(PWM_FAKE_OP_SET_DUTY_PCT, (unsigned int)channel, 0U, duty_pct, status);
    }
    if (!channel_valid(channel) || (duty_pct > 100U) || !channels[channel].configured) {
        return record(PWM_FAKE_OP_SET_DUTY_PCT, (unsigned int)channel, 0U, duty_pct, IF_HW_FAULT);
    }
    channels[channel].duty_pct = duty_pct;
    return record(PWM_FAKE_OP_SET_DUTY_PCT, (unsigned int)channel, 0U, duty_pct, IF_OK);
}

static IfStatus fake_set_frequency_hz(PwmChannel channel, uint32_t frequency_hz)
{
    IfStatus status;

    if (take_forced(PWM_FAKE_OP_SET_FREQUENCY_HZ, &status)) {
        return record(PWM_FAKE_OP_SET_FREQUENCY_HZ, (unsigned int)channel, frequency_hz, 0U,
                      status);
    }
    if (!channel_valid(channel) || (frequency_hz == 0U) || !channels[channel].configured) {
        return record(PWM_FAKE_OP_SET_FREQUENCY_HZ, (unsigned int)channel, frequency_hz, 0U,
                      IF_HW_FAULT);
    }
    channels[channel].frequency_hz = frequency_hz;
    return record(PWM_FAKE_OP_SET_FREQUENCY_HZ, (unsigned int)channel, frequency_hz, 0U, IF_OK);
}

static IfStatus fake_start_capture(PwmCaptureChannel channel)
{
    IfStatus status;

    if (take_forced(PWM_FAKE_OP_START_CAPTURE, &status)) {
        return record(PWM_FAKE_OP_START_CAPTURE, (unsigned int)channel, 0U, 0U, status);
    }
    if (!capture_channel_valid(channel)) {
        return record(PWM_FAKE_OP_START_CAPTURE, (unsigned int)channel, 0U, 0U, IF_HW_FAULT);
    }
    if (capture_channels[channel].capturing) {
        return record(PWM_FAKE_OP_START_CAPTURE, (unsigned int)channel, 0U, 0U, IF_BUSY);
    }
    capture_channels[channel].capturing  = true;
    capture_channels[channel].armed      = true;
    capture_channels[channel].has_sample = false;
    return record(PWM_FAKE_OP_START_CAPTURE, (unsigned int)channel, 0U, 0U, IF_OK);
}

static IfStatus fake_stop_capture(PwmCaptureChannel channel)
{
    IfStatus status;

    if (take_forced(PWM_FAKE_OP_STOP_CAPTURE, &status)) {
        return record(PWM_FAKE_OP_STOP_CAPTURE, (unsigned int)channel, 0U, 0U, status);
    }
    if (!capture_channel_valid(channel) || !capture_channels[channel].capturing) {
        return record(PWM_FAKE_OP_STOP_CAPTURE, (unsigned int)channel, 0U, 0U, IF_HW_FAULT);
    }
    capture_channels[channel].capturing = false;
    return record(PWM_FAKE_OP_STOP_CAPTURE, (unsigned int)channel, 0U, 0U, IF_OK);
}

/* Shared validity/staleness logic for both capture reads. */
static IfStatus read_status(PwmFakeOp op, PwmCaptureChannel channel, bool out_valid)
{
    IfStatus status;

    /* A forced status is never IF_OK (pwm_fake_force_next() treats IF_OK
     * as "cancel"); the explicit test keeps IF_OK meaning "validated". */
    if (take_forced(op, &status) && (status != IF_OK)) {
        return status;
    }
    if (!capture_channel_valid(channel) || !out_valid || !capture_channels[channel].armed) {
        return IF_HW_FAULT;
    }
    if (!capture_channels[channel].has_sample) {
        return IF_TIMEOUT;
    }
    return IF_OK;
}

static IfStatus fake_read_frequency(PwmCaptureChannel channel, uint32_t *out_frequency_hz)
{
    IfStatus status = read_status(PWM_FAKE_OP_READ_FREQUENCY, channel, out_frequency_hz != NULL);

    if (status == IF_OK) {
        *out_frequency_hz = capture_channels[channel].frequency_hz;
    }
    return record(PWM_FAKE_OP_READ_FREQUENCY, (unsigned int)channel, 0U, 0U, status);
}

static IfStatus fake_read_duty_permille(PwmCaptureChannel channel, uint16_t *out_duty_permille)
{
    IfStatus status = read_status(PWM_FAKE_OP_READ_DUTY_PERMILLE, channel,
                                  out_duty_permille != NULL);

    if (status == IF_OK) {
        *out_duty_permille = capture_channels[channel].duty_permille;
    }
    return record(PWM_FAKE_OP_READ_DUTY_PERMILLE, (unsigned int)channel, 0U, 0U, status);
}

const PwmIf pwm_fake = {
    .init               = fake_init,
    .set_duty_pct       = fake_set_duty_pct,
    .set_frequency_hz   = fake_set_frequency_hz,
    .start_capture      = fake_start_capture,
    .stop_capture       = fake_stop_capture,
    .read_frequency     = fake_read_frequency,
    .read_duty_permille = fake_read_duty_permille,
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
        capture_channels[i].has_sample    = false;
        capture_channels[i].frequency_hz  = 0U;
        capture_channels[i].duty_permille = 0U;
    }
    for (i = 0U; i < (size_t)PWM_FAKE_OP_COUNT; i++) {
        forced[i].pending = false;
        forced[i].status  = IF_OK;
    }
    call_log_head  = 0U;
    call_log_tail  = 0U;
    call_log_count = 0U;
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

void pwm_fake_push_capture(PwmCaptureChannel channel, uint32_t frequency_hz, uint16_t duty_permille)
{
    if (!capture_channel_valid(channel) || !capture_channels[channel].capturing
        || (duty_permille > 1000U)) {
        return;
    }
    capture_channels[channel].frequency_hz  = frequency_hz;
    capture_channels[channel].duty_permille = duty_permille;
    capture_channels[channel].has_sample    = true;
}

void pwm_fake_capture_silence(PwmCaptureChannel channel)
{
    if (!capture_channel_valid(channel) || !capture_channels[channel].capturing) {
        return;
    }
    capture_channels[channel].has_sample = false;
}

void pwm_fake_force_next(PwmFakeOp op, IfStatus status)
{
    if ((unsigned int)op >= (unsigned int)PWM_FAKE_OP_COUNT) {
        return;
    }
    forced[op].pending = (status != IF_OK);
    forced[op].status  = status;
}

bool pwm_fake_pop_call(PwmFakeCall *out)
{
    if ((out == NULL) || (call_log_count == 0U)) {
        return false;
    }
    *out          = call_log[call_log_tail];
    call_log_tail = (call_log_tail + 1U) % PWM_FAKE_CALL_LOG_DEPTH;
    call_log_count--;
    return true;
}

size_t pwm_fake_call_count(void)
{
    return call_log_count;
}
