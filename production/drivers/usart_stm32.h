/**
 * @file usart_stm32.h
 * @brief UartIf driver: implements uart_if on top of ST HAL's USART2 in
 * asynchronous mode (HAL_UART_*). Wires USART2 to PA2 (TX) / PA3 (RX), AF7,
 * TX on DMA1 Stream6 Channel4, RX on DMA1 Stream5 Channel4 -- see
 * usart_stm32.c for the MSP/GPIO/DMA/NVIC setup and the receive scheme.
 * Composition roots inject this in place of `usart_fake` to run on real
 * hardware.
 */
#ifndef MONOBOARD_USART_STM32_H
#define MONOBOARD_USART_STM32_H

#include "uart_if.h"

extern const UartIf usart_stm32;

/* Driver-internal diagnostics -- not part of the UartIf contract. Exposed
 * so a monitoring/logging task can surface them (issue #16 acceptance
 * criteria: "baud error ... calculated and recorded", "overrun/framing
 * errors are detected [and] counted"). */
int32_t  usart_stm32_baud_error_permille(void); /* (actual - configured) * 1000 / configured */
uint32_t usart_stm32_overrun_error_count(void);
uint32_t usart_stm32_framing_error_count(void);
uint32_t usart_stm32_noise_error_count(void);

#endif  // MONOBOARD_USART_STM32_H
