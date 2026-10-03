/**
 * @file adc_fake.h
 * @brief Programmable fake ADC driver: implements AdcIf with no hardware
 * and no FreeRTOS dependency, so it builds and runs anywhere. Composition
 * roots wire `adc_fake` in exactly where `adc_stm32` would go; tests
 * additionally use the functions below to act as "the hardware" and to
 * inspect scan state. Follows the fake driver conventions from issue #16:
 * values and events are scripted ahead of time, every failure mode the
 * real hardware can produce can be forced, and there is zero `#ifdef TEST`
 * -- this is ordinary code in every build.
 *
 * Two layers of control (issue #58):
 * - adc_fake_push_sample() is one conversion completing *now* on one
 *   channel -- the lowest-level hook.
 * - The "sensor world" -- a held value per channel (adc_fake_set_value()),
 *   a scripted sequence such as a pedal ramp (adc_fake_play_sequence()),
 *   and a rail-pinned open/short fault (adc_fake_set_rail()) -- is only
 *   sampled when the test calls adc_fake_scan(), the stand-in for one
 *   timer-triggered scan of every channel in the active scan. The sensor
 *   world survives start_scan()/stop_scan() (it is the outside world, not
 *   driver state); only adc_fake_reset() clears it.
 */
#ifndef ADC_FAKE_H
#define ADC_FAKE_H

#include <stdbool.h>
#include <stddef.h>

#include "adc_if.h"

extern const AdcIf adc_fake;

/* Full-scale reading of the 12-bit converter adc_stm32 drives -- what a
 * channel pinned to the high rail reads. */
#define ADC_FAKE_FULL_SCALE 4095U

/* Longest scan accepted by start_scan(), matching adc_stm32's ADC1
 * regular-sequence limit, so a too-long channel list fails on the desktop
 * exactly as it would on the board. */
#define ADC_FAKE_MAX_RANKS 16U

/* Simulated pinned-rail sensor faults. Which wiring fault reads which rail
 * depends on the sensor's pull resistor; for a pulled-up input an open
 * circuit reads full-scale and a short to ground reads zero. */
typedef enum {
    ADC_FAKE_RAIL_NONE = 0, /* healthy: the channel reads its held/scripted value */
    ADC_FAKE_RAIL_LOW,      /* pinned to 0 counts (short to ground) */
    ADC_FAKE_RAIL_HIGH,     /* pinned to ADC_FAKE_FULL_SCALE (open circuit / short to supply) */
} AdcFakeRail;

/* Test-only control surface -- not part of the AdcIf contract. */
void adc_fake_reset(void);
bool adc_fake_is_scanning(void);
bool adc_fake_is_scanned(AdcChannel channel); /* included in the current start_scan() */

/* Fault injection: the next start_scan() call returns IF_HW_FAULT (a
 * peripheral that refuses to arm), then the condition clears itself. */
void adc_fake_force_start_hw_fault(void);

/* Simulates one conversion completing: updates the cached value
 * read_latest() returns and, if channel is in the active scan and sample
 * trips its configured watchdog limits, invokes on_watchdog synchronously
 * -- a test's stand-in for the ISR call. Ignored while not scanning or for
 * a channel outside the scan. */
void adc_fake_push_sample(AdcChannel channel, AdcSample sample);

/* Sensor world: the voltage (in counts) @p channel reads on every scan
 * until changed. Replaces any sequence still playing on that channel. */
void adc_fake_set_value(AdcChannel channel, AdcSample sample);

/* Sensor world: play @p samples, one per adc_fake_scan(), then hold the
 * last one (a ramp ends where it stops). @p samples is not copied -- it
 * must stay valid until the sequence has played out or is replaced.
 * NULL or zero @p count cancels a playing sequence and keeps the current
 * held value. */
void   adc_fake_play_sequence(AdcChannel channel, const AdcSample *samples, size_t count);
size_t adc_fake_sequence_remaining(AdcChannel channel);

/* Sensor world: pin @p channel to a rail (or release it with
 * ADC_FAKE_RAIL_NONE). While pinned, every scan reads the rail value --
 * so on_watchdog fires on every scan, as adc_if.h specifies -- and any
 * playing sequence keeps advancing underneath, the way a pedal keeps
 * moving while its signal wire is broken. */
void adc_fake_set_rail(AdcChannel channel, AdcFakeRail rail);

/* Simulates one complete timer-triggered scan: every channel in the active
 * scan, in start_scan() order, converts its rail value if pinned, else its
 * next scripted sample, else its held value, via adc_fake_push_sample().
 * A channel with none of those set yields no conversion. No-op while not
 * scanning. */
void adc_fake_scan(void);

/* Injects an analog-watchdog event directly, regardless of the configured
 * limits and without changing the cached value -- for testing a service's
 * fault reaction in isolation. Returns true if on_watchdog was invoked
 * (requires an active scan containing @p channel and a non-NULL callback). */
bool adc_fake_inject_watchdog(AdcChannel channel, AdcSample sample);

#endif /* ADC_FAKE_H */
