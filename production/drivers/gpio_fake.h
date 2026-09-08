/**
 * @file gpio_fake.h
 * @brief Programmable fake GPIO driver: implements GpioIf with no hardware and
 * no FreeRTOS dependency, so it builds and runs anywhere. Composition
 * roots wire `gpio_fake` in exactly where `gpio_stm32` would go; tests
 * additionally use the functions below to act as "the outside world"
 * (drive input pins) and to inspect what an output pin is driving.
 */
#ifndef GPIO_FAKE_H
#define GPIO_FAKE_H

#include <stdbool.h>

#include "gpio_if.h"

extern const GpioIf gpio_fake;

/* Test-only control surface -- not part of the GpioIf contract. */
void      gpio_fake_reset(void);
bool      gpio_fake_is_configured(GpioPin pin);
GpioDir   gpio_fake_dir(GpioPin pin);
GpioLevel gpio_fake_level(GpioPin pin);                        /* current level on the pad */
void      gpio_fake_drive_input(GpioPin pin, GpioLevel level); /* external source on the pin */

#endif /* GPIO_FAKE_H */
