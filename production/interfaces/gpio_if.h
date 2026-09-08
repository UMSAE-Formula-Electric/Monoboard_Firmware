/**
 * @file gpio_if.h
 * @brief GPIO contract shared by every GPIO driver (real or fake).
 *
 * Header-only, standard-library types only -- see ARCHITECTURE.md,
 * Interface Layer rules. Pins are named logically (GpioPin); the concrete
 * driver owns the pin-to-pad mapping for its board, exactly as can_stm32
 * owns "CAN1 is PA11/PA12". A service asks for GPIO_PIN_STATUS_LED and
 * never learns which silicon pad that is.
 */
#ifndef GPIO_IF_H
#define GPIO_IF_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Logical pin catalogue for this board. The concrete driver maps each
 * entry to a physical port/pad. Keep GPIO_PIN_COUNT last -- both the fake
 * and the real driver size their tables from it. */
typedef enum {
    GPIO_PIN_TEST = 0,
    GPIO_PIN_COUNT,
} GpioPin;

typedef enum {
    GPIO_DIR_INPUT = 0,
    GPIO_DIR_OUTPUT,
} GpioDir;

typedef enum {
    GPIO_PULL_NONE = 0,
    GPIO_PULL_UP,
    GPIO_PULL_DOWN,
} GpioPull;

typedef enum {
    GPIO_LOW  = 0,
    GPIO_HIGH = 1,
} GpioLevel;

typedef enum {
    GPIO_OK = 0,
    GPIO_ERR_ARG, /* unknown pin, NULL pointer, or out-of-range enum */
    GPIO_ERR_DIR, /* write/toggle attempted on a pin not configured as output */
    GPIO_ERR_HAL, /* pin used before init, or the peripheral/HAL failed */
} GpioStatus;

typedef struct {
    GpioDir   dir;
    GpioPull  pull;    /* input: bias resistor; output: normally GPIO_PULL_NONE */
    GpioLevel initial; /* output only: level driven immediately at init (ignored for inputs) */
} GpioConfig;

typedef struct {
    GpioStatus (*init)(GpioPin pin, const GpioConfig *config);
    GpioStatus (*write)(GpioPin pin, GpioLevel level); /* output pins only */
    GpioStatus (*read)(GpioPin pin, GpioLevel *level); /* input or output */
    GpioStatus (*toggle)(GpioPin pin);                 /* output pins only */
} GpioIf;

#ifdef __cplusplus
}
#endif

#endif /* GPIO_IF_H */
