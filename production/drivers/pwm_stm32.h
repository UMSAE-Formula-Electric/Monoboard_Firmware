/**
 * @file pwm_stm32.h
 * @brief PwmIf driver: implements pwm_if on top of ST HAL's general-purpose
 * timers (HAL_TIM_*). Outputs: TIM3_CH1 on PA6 (AF2). Input capture:
 * TIM4_CH1 on PB6 (AF2), in PWM-input mode (CH1 = period, CH2 = high time).
 * See pwm_stm32.c for the channel tables, the prescaler/ARR policy and the
 * safe-state rules. Composition roots inject this in place of `pwm_fake`
 * to run on real hardware.
 */
#ifndef MONOBOARD_PWM_STM32_H
#define MONOBOARD_PWM_STM32_H

#include "pwm_if.h"

extern const PwmIf pwm_stm32;

/* Driver-internal extras -- not part of the PwmIf contract. */

/* Fault reaction: immediately drives every output pad to its per-channel
 * safe level as a plain GPIO (independent of the timer), and marks every
 * output unconfigured so it stays there until the next init(). Task
 * context, or a fault handler with the scheduler stopped. */
void pwm_stm32_enter_safe_state(void);

/* Frequency the timer actually produces after PSC/ARR rounding, in Hz
 * (0 if channel is not configured) -- the recorded "documented value" a
 * scope reading is checked against (issue #17). */
uint32_t pwm_stm32_actual_frequency_hz(PwmChannel channel);

#endif  // MONOBOARD_PWM_STM32_H
