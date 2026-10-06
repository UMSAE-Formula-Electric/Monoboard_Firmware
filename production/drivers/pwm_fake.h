/**
 * @file pwm_fake.h
 * @brief Programmable fake PWM driver: implements PwmIf with no hardware
 * and no FreeRTOS dependency, so it builds and runs anywhere. Composition
 * roots wire `pwm_fake` in exactly where `pwm_stm32` would go; tests
 * additionally use the functions below to inspect what a channel is
 * currently driving, script capture samples, and force failures.
 *
 * Follows the fake driver conventions (issues #16/#17): every contract
 * call is recorded with its arguments and result in a ring buffer the test
 * can assert on, capture samples and signal loss are scripted by the test,
 * and every failure mode the real driver can report can be forced ahead of
 * time. Zero `#ifdef TEST` -- this is ordinary code in every build.
 */
#ifndef PWM_FAKE_H
#define PWM_FAKE_H

#include <stdbool.h>
#include <stddef.h>

#include "pwm_if.h"

extern const PwmIf pwm_fake;

/* Capacity of the call log; calls beyond this depth are not recorded
 * (the call itself still behaves normally). Exposed so tests can exercise
 * the "full" boundary without hardcoding the number. */
#define PWM_FAKE_CALL_LOG_DEPTH 32U

/* One entry per PwmIf function pointer, in contract order. */
typedef enum {
    PWM_FAKE_OP_INIT = 0,
    PWM_FAKE_OP_SET_DUTY_PCT,
    PWM_FAKE_OP_SET_FREQUENCY_HZ,
    PWM_FAKE_OP_START_CAPTURE,
    PWM_FAKE_OP_STOP_CAPTURE,
    PWM_FAKE_OP_READ_FREQUENCY,
    PWM_FAKE_OP_READ_DUTY_PERMILLE,
    PWM_FAKE_OP_COUNT,
} PwmFakeOp;

/* One recorded contract call. channel is a PwmChannel for the output ops
 * and a PwmCaptureChannel for the capture ops. frequency_hz/duty_pct hold
 * the arguments the op was called with (init: both; set_duty_pct:
 * duty_pct; set_frequency_hz: frequency_hz; everything else: 0). An init()
 * with a NULL config records 0 for both. */
typedef struct {
    PwmFakeOp    op;
    unsigned int channel;
    uint32_t     frequency_hz;
    uint8_t      duty_pct;
    IfStatus     result;
} PwmFakeCall;

/* Test-only control surface -- not part of the PwmIf contract. */
void pwm_fake_reset(void);

/* Output inspection: the last duty/frequency successfully commanded. */
bool     pwm_fake_is_configured(PwmChannel channel);
uint8_t  pwm_fake_duty_pct(PwmChannel channel);
uint32_t pwm_fake_frequency_hz(PwmChannel channel);

bool pwm_fake_is_capturing(PwmCaptureChannel channel);

/* Simulates one period completing on the "hardware" input-capture pin:
 * updates the cached frequency and duty that read_frequency() and
 * read_duty_permille() return for channel. No-op if channel is not
 * currently capturing (exactly as a real edge arriving on a disarmed timer
 * channel would be) or if duty_permille > 1000. */
void pwm_fake_push_capture(PwmCaptureChannel channel,
                           uint32_t          frequency_hz,
                           uint16_t          duty_permille);

/* Simulates the real driver's silence window expiring with no edges (wire
 * pulled, IMD unpowered): the reads go back to IF_TIMEOUT until the next
 * pwm_fake_push_capture(). No-op if channel is not capturing. */
void pwm_fake_capture_silence(PwmCaptureChannel channel);

/* Fault injection: the next call to op returns status instead of running,
 * with no side effects, then the forcing clears itself -- a one-shot
 * hardware condition. Forcing IF_OK cancels a pending force for op. */
void pwm_fake_force_next(PwmFakeOp op, IfStatus status);

/* Call log: every contract call since reset, FIFO, oldest first. */
bool   pwm_fake_pop_call(PwmFakeCall *out);
size_t pwm_fake_call_count(void);

#endif /* PWM_FAKE_H */
