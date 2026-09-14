/**
 * @file pwm_fake.h
 * @brief Programmable fake PWM driver: implements PwmIf with no hardware
 * and no FreeRTOS dependency, so it builds and runs anywhere. Composition
 * roots wire `pwm_fake` in exactly where `pwm_stm32` would go; tests
 * additionally use the functions below to inspect what a channel is
 * currently driving.
 */
#ifndef PWM_FAKE_H
#define PWM_FAKE_H

#include <stdbool.h>

#include "pwm_if.h"

extern const PwmIf pwm_fake;

/* Test-only control surface -- not part of the PwmIf contract. */
void     pwm_fake_reset(void);
bool     pwm_fake_is_configured(PwmChannel channel);
uint8_t  pwm_fake_duty_pct(PwmChannel channel);
uint32_t pwm_fake_frequency_hz(PwmChannel channel);

bool pwm_fake_is_capturing(PwmCaptureChannel channel);

/* Simulates one period completing on the "hardware" input-capture pin:
 * updates the cached value read_frequency() returns for channel. No-op if
 * channel is not currently capturing, exactly as a real edge arriving on
 * a disarmed timer channel would be. */
void pwm_fake_push_capture(PwmCaptureChannel channel, uint32_t frequency_hz);

#endif /* PWM_FAKE_H */
