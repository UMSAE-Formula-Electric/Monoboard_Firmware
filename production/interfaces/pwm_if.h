/**
 * @file pwm_if.h
 * @brief PWM contract shared by every PWM driver (real or fake) -- see
 * issue #6. Covers both directions: driving an output (e.g. an LED) with
 * a duty cycle and frequency, and capturing an input signal's frequency
 * (e.g. a device that encodes its state as a PWM frequency). The two
 * halves use separate channel catalogues -- output compare and input
 * capture are different timer hardware, usually different pins.
 *
 * Frequency is all this layer reports for a captured signal -- raw and
 * hardware-flavored, per ARCHITECTURE.md. Deciding what a given frequency
 * *means* (which state, which fault code) is a service's job, not this
 * contract's.
 *
 * Header-only, standard-library types only -- see ARCHITECTURE.md,
 * Interface Layer rules. Channels are named logically (PwmChannel,
 * PwmCaptureChannel); the concrete driver owns the channel-to-timer/pad
 * mapping for its board, exactly as can_stm32 owns "CAN1 is PA11/PA12".
 */
#ifndef MONOBOARD_PWM_IF_H
#define MONOBOARD_PWM_IF_H

#include <stdint.h>

#include "if_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Logical channel catalogue for this board. The concrete driver maps each
 * entry to a physical timer/pad. Keep PWM_CH_COUNT last -- both the fake
 * and the real driver size their tables from it. */
typedef enum {
    PWM_CH_TEST = 0,
    PWM_CH_COUNT,
} PwmChannel;

typedef struct {
    uint32_t frequency_hz; /* initial carrier frequency, e.g. 20000 for 20 kHz */
    uint8_t  duty_pct;     /* initial duty cycle, 0-100 */
} PwmConfig;

/* Logical input-capture channel catalogue for this board -- distinct from
 * PwmChannel: capture uses the timer's input-capture channels, not its
 * output-compare channels, and is typically wired to a different pin.
 * Keep PWM_CAP_CH_COUNT last -- both the fake and the real driver size
 * their tables from it. */
typedef enum {
    PWM_CAP_CH_TEST = 0,
    PWM_CAP_CH_COUNT,
} PwmCaptureChannel;

typedef struct {
    /**
     * Bring up @p channel's timer and start it running at the given
     * frequency and duty cycle.
     * @param channel  channel to configure
     * @param config   initial frequency and duty cycle
     * @return #IF_OK on success, or #IF_HW_FAULT if @p config is invalid
     *         (NULL, zero frequency_hz, duty_pct > 100, a frequency
     *         unreachable from the peripheral clock) or the underlying
     *         peripheral/HAL rejected the setup
     */
    IfStatus (*init)(PwmChannel channel, const PwmConfig *config);

    /**
     * Change @p channel's duty cycle without touching its frequency.
     * @param channel   channel to update
     * @param duty_pct  0-100
     * @return #IF_OK on success, or #IF_HW_FAULT if @p channel is unknown,
     *         @p duty_pct > 100, or @p channel was never init()'d
     */
    IfStatus (*set_duty_pct)(PwmChannel channel, uint8_t duty_pct);

    /**
     * Change @p channel's carrier frequency without touching its duty
     * cycle.
     * @param channel       channel to update
     * @param frequency_hz  new carrier frequency
     * @return #IF_OK on success, or #IF_HW_FAULT if @p channel is
     *         unknown, @p frequency_hz is zero or unreachable from the
     *         peripheral clock, or @p channel was never init()'d
     */
    IfStatus (*set_frequency_hz)(PwmChannel channel, uint32_t frequency_hz);

    /**
     * Begin measuring the frequency of the signal arriving on @p channel.
     * Non-blocking: measurement runs in the background (timer
     * input-capture + IRQ); the call returns once armed, not once the
     * first period is measured.
     * @param channel  capture channel to arm
     * @return #IF_OK if armed, #IF_BUSY if @p channel is already
     *         capturing (call stop_capture() first), or #IF_HW_FAULT on
     *         an unknown channel or a peripheral fault
     */
    IfStatus (*start_capture)(PwmCaptureChannel channel);

    /**
     * Stop measuring @p channel. read_frequency()'s cached value remains
     * valid (last known good) until the next start_capture().
     * @return #IF_OK, or #IF_HW_FAULT if @p channel was not capturing
     */
    IfStatus (*stop_capture)(PwmCaptureChannel channel);

    /**
     * Read the most recently measured frequency on @p channel.
     * @param      channel           capture channel to read
     * @param      out_frequency_hz  [out] most recent measured frequency
     * @return #IF_OK if @p out_frequency_hz holds a real measurement,
     *         #IF_TIMEOUT if @p channel has completed no full period yet
     *         (including a signal that has gone stale -- no edge within
     *         the driver's silence window), or #IF_HW_FAULT on bad
     *         arguments (unknown channel, NULL @p out_frequency_hz, or a
     *         channel that has never been armed via start_capture())
     */
    IfStatus (*read_frequency)(PwmCaptureChannel channel, uint32_t *out_frequency_hz);
} PwmIf;

#ifdef __cplusplus
}
#endif

#endif  // MONOBOARD_PWM_IF_H
