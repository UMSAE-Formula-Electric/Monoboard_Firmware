/**
 * @file board_stm32.h
 * @brief STM32F446 board bring-up: HAL init, clock tree and the HAL timebase.
 *
 * The HAL tick runs from TIM10, as vendor.ioc selects
 * (stm32f4xx_hal_timebase_tim.c), so SysTick belongs to the FreeRTOS kernel
 * alone. This module owns the TIM10 vector that feeds the HAL tick.
 */
#ifndef BOARD_STM32_H
#define BOARD_STM32_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Reset the peripherals, start the HAL tick on TIM10 and configure the
 * clock tree. Call first in main(), before any other driver: every HAL
 * timeout counts ticks, and every driver's timing derives from the clocks.
 * A clock-configuration failure ends in the CubeMX Error_Handler().
 */
void board_stm32_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_STM32_H */
