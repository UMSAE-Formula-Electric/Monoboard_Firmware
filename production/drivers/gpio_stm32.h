/**
 * @file gpio_stm32.h
 * @brief GPIO driver on top of ST HAL (HAL_GPIO_*). Owns the logical-pin ->
 * physical-pad map for this board (pin_map[] in gpio_stm32.c); change
 * wiring there and only there. Composition roots inject this in place of
 * `gpio_fake` to run on real hardware.
 */
#ifndef GPIO_STM32_H
#define GPIO_STM32_H

#include "gpio_if.h"

extern const GpioIf gpio_stm32;

#endif /* GPIO_STM32_H */
