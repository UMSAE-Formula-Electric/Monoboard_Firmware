/**
 * @file gpio_fake.c
 * @brief Programmable fake GPIO driver -- the GpioIf test double.
 */
#include "gpio_fake.h"

#include <stddef.h>

typedef struct {
    bool      configured;
    GpioDir   dir;
    GpioPull  pull;
    GpioLevel level;
} FakePin;

static FakePin pins[GPIO_PIN_COUNT];

static bool pin_valid(GpioPin pin)
{
    return (unsigned int)pin < (unsigned int)GPIO_PIN_COUNT;
}

static bool dir_valid(GpioDir dir)
{
    return (dir == GPIO_DIR_INPUT) || (dir == GPIO_DIR_OUTPUT);
}

static bool level_valid(GpioLevel level)
{
    return (level == GPIO_LOW) || (level == GPIO_HIGH);
}

static GpioStatus fake_init(GpioPin pin, const GpioConfig *config)
{
    FakePin *p;

    if (!pin_valid(pin) || (config == NULL) || !dir_valid(config->dir)) {
        return GPIO_ERR_ARG;
    }

    p             = &pins[pin];
    p->configured = true;
    p->dir        = config->dir;
    p->pull       = config->pull;

    if (config->dir == GPIO_DIR_OUTPUT) {
        p->level = level_valid(config->initial) ? config->initial : GPIO_LOW;
    } else {
        /* Input: the level reflects the outside world. Nothing is driving
         * it yet, so seed it from the configured bias resistor. */
        p->level = (config->pull == GPIO_PULL_UP) ? GPIO_HIGH : GPIO_LOW;
    }
    return GPIO_OK;
}

static GpioStatus fake_write(GpioPin pin, GpioLevel level)
{
    if (!pin_valid(pin) || !level_valid(level)) {
        return GPIO_ERR_ARG;
    }
    if (!pins[pin].configured) {
        return GPIO_ERR_HAL;
    }
    if (pins[pin].dir != GPIO_DIR_OUTPUT) {
        return GPIO_ERR_DIR;
    }
    pins[pin].level = level;
    return GPIO_OK;
}

static GpioStatus fake_read(GpioPin pin, GpioLevel *level)
{
    if (!pin_valid(pin) || (level == NULL)) {
        return GPIO_ERR_ARG;
    }
    if (!pins[pin].configured) {
        return GPIO_ERR_HAL;
    }
    *level = pins[pin].level;
    return GPIO_OK;
}

static GpioStatus fake_toggle(GpioPin pin)
{
    if (!pin_valid(pin)) {
        return GPIO_ERR_ARG;
    }
    if (!pins[pin].configured) {
        return GPIO_ERR_HAL;
    }
    if (pins[pin].dir != GPIO_DIR_OUTPUT) {
        return GPIO_ERR_DIR;
    }
    pins[pin].level = (pins[pin].level == GPIO_LOW) ? GPIO_HIGH : GPIO_LOW;
    return GPIO_OK;
}

const GpioIf gpio_fake = {
    .init   = fake_init,
    .write  = fake_write,
    .read   = fake_read,
    .toggle = fake_toggle,
};

void gpio_fake_reset(void)
{
    size_t i;

    for (i = 0U; i < (size_t)GPIO_PIN_COUNT; i++) {
        pins[i].configured = false;
        pins[i].dir        = GPIO_DIR_INPUT;
        pins[i].pull       = GPIO_PULL_NONE;
        pins[i].level      = GPIO_LOW;
    }
}

bool gpio_fake_is_configured(GpioPin pin)
{
    return pin_valid(pin) && pins[pin].configured;
}

GpioDir gpio_fake_dir(GpioPin pin)
{
    return pin_valid(pin) ? pins[pin].dir : GPIO_DIR_INPUT;
}

GpioLevel gpio_fake_level(GpioPin pin)
{
    return pin_valid(pin) ? pins[pin].level : GPIO_LOW;
}

void gpio_fake_drive_input(GpioPin pin, GpioLevel level)
{
    /* Simulates an external source. On an output pin the next write()
     * or toggle() overwrites this. */
    if (pin_valid(pin) && level_valid(level)) {
        pins[pin].level = level;
    }
}
