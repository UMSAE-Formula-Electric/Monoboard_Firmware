/**
 * @file adc_fake.h
 * @brief Programmable fake ADC driver: implements AdcIf with no hardware
 * and no FreeRTOS dependency, so it builds and runs anywhere. Composition
 * roots wire `adc_fake` in exactly where `adc_stm32` would go; tests
 * additionally use the functions below to act as "the hardware" (push a
 * conversion result) and to inspect scan state.
 */
#ifndef ADC_FAKE_H
#define ADC_FAKE_H

#include <stdbool.h>

#include "adc_if.h"

extern const AdcIf adc_fake;

/* Test-only control surface -- not part of the AdcIf contract. */
void adc_fake_reset(void);
bool adc_fake_is_scanning(void);
bool adc_fake_is_scanned(AdcChannel channel); /* included in the current start_scan() */

/* Simulates one conversion completing: updates the cached value
 * read_latest() returns and, if channel is in the active scan and sample
 * trips its configured watchdog limits, invokes on_watchdog synchronously
 * -- a test's stand-in for the ISR call. */
void adc_fake_push_sample(AdcChannel channel, AdcSample sample);

#endif /* ADC_FAKE_H */
