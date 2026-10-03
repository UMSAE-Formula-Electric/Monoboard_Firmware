/**
 * @file adc_stm32.h
 * @brief AdcIf driver for the STM32F446's ADC1: timer-triggered scan
 * (TIM8 TRGO) into a circular, double-buffered DMA2 Stream0 Channel0
 * transfer. ADC_CH_TEST is wired to PA0 (ADC1_IN0) -- see adc_stm32.c for
 * the channel map, the sample-time calculation, and the reason this is
 * written at register level rather than against HAL_ADC. Composition roots
 * inject this in place of `adc_fake` to run on real hardware.
 */
#ifndef MONOBOARD_ADC_STM32_H
#define MONOBOARD_ADC_STM32_H

#include <stdint.h>

#include "adc_if.h"

extern const AdcIf adc_stm32;

/* Driver-internal diagnostics -- not part of the AdcIf contract. Exposed so
 * a monitoring/logging task can surface them. Each counts an event the
 * driver detected and recovered from on its own (scan restarted from rank
 * 1, no reset needed); a non-zero, growing count means scans are being
 * lost. */
uint32_t adc_stm32_overrun_count(void);   /* ADC1 OVR: a conversion was not read by the DMA */
uint32_t adc_stm32_dma_error_count(void); /* DMA2 Stream0 transfer error */

#endif  // MONOBOARD_ADC_STM32_H
