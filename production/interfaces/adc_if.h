/**
 * @file adc_if.h
 * @brief ADC contract shared by every ADC driver (real or fake) -- see
 * issue #6. Channels are scanned continuously in the background
 * (DMA/IRQ-driven); read_latest() returns whatever the most recent
 * conversion cached, never blocking to wait for a fresh one.
 *
 * Header-only, standard-library types only -- see ARCHITECTURE.md,
 * Interface Layer rules. Channels are named logically (AdcChannel); the
 * concrete driver owns the channel-to-pad mapping for its board, exactly
 * as can_stm32 owns "CAN1 is PA11/PA12".
 */
#ifndef MONOBOARD_ADC_IF_H
#define MONOBOARD_ADC_IF_H

#include <stddef.h>
#include <stdint.h>

#include "if_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Logical channel catalogue for this board. The concrete driver maps each
 * entry to a physical ADC input. Keep ADC_CH_COUNT last -- both the fake
 * and the real driver size their tables from it. */
typedef enum {
    ADC_CH_TEST = 0,
    ADC_CH_COUNT,
} AdcChannel;

/* Raw conversion result -- hardware-flavored, no domain meaning (volts,
 * temperature, pedal travel) at this layer. That conversion belongs to
 * the service that owns the sensor. */
typedef uint16_t AdcSample;

/* Analog-watchdog thresholds for one channel. low == high disables
 * watchdog checking for that channel -- every conversion passes. */
typedef struct {
    AdcSample low;  /* inclusive low rail */
    AdcSample high; /* inclusive high rail */
} AdcWatchdogLimits;

/**
 * Fired when a channel's freshest conversion falls outside its configured
 * watchdog limits -- the classic pinned-rail sensor fault (an open circuit
 * reads full-scale, a short-to-ground reads zero). Invoked from ISR
 * context -- FromISR APIs only, no blocking, minimal work. Fires once per
 * out-of-range conversion, not once per fault: a sensor stuck at
 * full-scale fires on every sample until it recovers or the scan stops.
 * @param ctx      opaque pointer, exactly what was passed to start_scan()
 * @param channel  channel that tripped its watchdog
 * @param sample   the out-of-range reading
 */
typedef void (*AdcWatchdogCb)(void *ctx, AdcChannel channel, AdcSample sample);

typedef struct {
    /**
     * Begin continuously converting @p channels and caching each one's
     * latest result for read_latest(). Non-blocking: conversions run in
     * the background; the call returns once the scan is armed, not once
     * the first conversion completes.
     * @param      channels       channels to include in the scan
     * @param      channel_count  number of entries in @p channels
     * @param      limits         per-channel watchdog thresholds, indexed
     *                            by AdcChannel; NULL disables watchdog
     *                            checking for every channel
     * @param      on_watchdog    called from ISR context when any scanned
     *                            channel goes out of range; may be NULL
     * @param      ctx            opaque pointer passed back to on_watchdog
     * @return #IF_OK if the scan armed, #IF_BUSY if a scan is already
     *         running (call stop_scan() first), or #IF_HW_FAULT on bad
     *         arguments (NULL channels, zero channel_count, an unknown
     *         channel) or a peripheral fault
     */
    IfStatus (*start_scan)(const AdcChannel       *channels,
                           size_t                  channel_count,
                           const AdcWatchdogLimits limits[ADC_CH_COUNT],
                           AdcWatchdogCb           on_watchdog,
                           void                   *ctx);

    /**
     * Stop the running scan. Cached values from read_latest() remain
     * valid (last known good) until the next start_scan().
     * @return #IF_OK, or #IF_HW_FAULT if no scan was running
     */
    IfStatus (*stop_scan)(void);

    /**
     * Read the most recent conversion result cached for @p channel.
     * @param      channel    channel to read
     * @param      out_value  [out] latest raw sample
     * @return #IF_OK if @p out_value holds a real conversion,
     *         #IF_TIMEOUT if @p channel has completed no conversion yet,
     *         or #IF_HW_FAULT on bad arguments (unknown channel, NULL
     *         @p out_value, or a channel outside the current scan)
     */
    IfStatus (*read_latest)(AdcChannel channel, AdcSample *out_value);
} AdcIf;

#ifdef __cplusplus
}
#endif

#endif  // MONOBOARD_ADC_IF_H
