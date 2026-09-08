/**
 * @file gpio_stm32.c
 * @brief GpioIf implementation for ST HAL's GPIO block.
 *
 * This driver owns the electrical realisation: init() enables the owning
 * port's clock, applies the direction/pull from the caller's GpioConfig,
 * and for outputs drives the requested initial level before HAL_GPIO_Init
 * connects the pad, so there is no start-up glitch. GpioConfig (chip-neutral,
 * from gpio_if.h) is the single source of truth for a pin's behaviour --
 * CubeMX's MX_GPIO_Init is not on the call path.
 *
 * The .ioc still owns pad *identity*: pin_map[] below is written in terms of
 * the <LABEL>_Pin / <LABEL>_GPIO_Port macros CubeMX generates into main.h
 * from vendor.ioc, so renaming or deleting a pin in the .ioc (and
 * regenerating) is a compile error here, not silent drift. tools/ioc_check.py
 * checks the rest.
 */
#include "gpio_stm32.h"

#include "main.h" /* CubeMX-generated pin-label macros (<LABEL>_Pin, ...) */
#include "stm32f4xx_hal.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pad;
} PinMap;

/* GpioPin -> silicon pad. Adding a pin: give the pad a User Label in CubeMX
 * matching the enumerator (GPIO_PIN_<LABEL> <-> label "<LABEL>"), regenerate,
 * add the row here, and bump the _Static_assert. */
static const PinMap pin_map[GPIO_PIN_COUNT] = {
    [GPIO_PIN_TEST] = {TEST_GPIO_Port, TEST_Pin},
};

_Static_assert(GPIO_PIN_COUNT == 1,
               "GpioPin catalogue changed -- add/remove the matching pin_map[] row above");

static bool enable_port_clock(const GPIO_TypeDef *port)
{
    if (port == GPIOA) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    } else if (port == GPIOB) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    } else if (port == GPIOC) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    } else if (port == GPIOD) {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    } else if (port == GPIOH) {
        __HAL_RCC_GPIOH_CLK_ENABLE();
    } else {
        return false;
    }
    return true;
}

static uint32_t hal_pull(GpioPull pull)
{
    switch (pull) {
        case GPIO_PULL_UP:
            return GPIO_PULLUP;
        case GPIO_PULL_DOWN:
            return GPIO_PULLDOWN;
        case GPIO_PULL_NONE:
        default:
            return GPIO_NOPULL;
    }
}

static bool pin_valid(GpioPin pin)
{
    return (unsigned int)pin < (unsigned int)GPIO_PIN_COUNT;
}

static GpioStatus stm32_init(GpioPin pin, const GpioConfig *config)
{
    GPIO_InitTypeDef gpio_init = {0};
    const PinMap    *map;

    if (!pin_valid(pin) || (config == NULL)) {
        return GPIO_ERR_ARG;
    }
    if ((config->dir != GPIO_DIR_INPUT) && (config->dir != GPIO_DIR_OUTPUT)) {
        return GPIO_ERR_ARG;
    }

    map = &pin_map[pin];
    if (map->port == NULL) {
        return GPIO_ERR_ARG; /* catalogue entry with no wiring */
    }
    if (!enable_port_clock(map->port)) {
        return GPIO_ERR_HAL;
    }

    if (config->dir == GPIO_DIR_OUTPUT) {
        /* Drive the initial level first so the pad does not glitch when
         * HAL_GPIO_Init switches it to a push-pull output. */
        HAL_GPIO_WritePin(map->port, map->pad,
                          (config->initial == GPIO_HIGH) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    } else {
        gpio_init.Mode = GPIO_MODE_INPUT;
    }
    gpio_init.Pin   = map->pad;
    gpio_init.Pull  = hal_pull(config->pull);
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(map->port, &gpio_init);

    return GPIO_OK;
}

static GpioStatus stm32_write(GpioPin pin, GpioLevel level)
{
    if (!pin_valid(pin)) {
        return GPIO_ERR_ARG;
    }
    if ((level != GPIO_LOW) && (level != GPIO_HIGH)) {
        return GPIO_ERR_ARG;
    }
    HAL_GPIO_WritePin(pin_map[pin].port, pin_map[pin].pad,
                      (level == GPIO_HIGH) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return GPIO_OK;
}

static GpioStatus stm32_read(GpioPin pin, GpioLevel *level)
{
    if (!pin_valid(pin) || (level == NULL)) {
        return GPIO_ERR_ARG;
    }
    *level = (HAL_GPIO_ReadPin(pin_map[pin].port, pin_map[pin].pad) == GPIO_PIN_SET) ? GPIO_HIGH
                                                                                     : GPIO_LOW;
    return GPIO_OK;
}

static GpioStatus stm32_toggle(GpioPin pin)
{
    if (!pin_valid(pin)) {
        return GPIO_ERR_ARG;
    }
    HAL_GPIO_TogglePin(pin_map[pin].port, pin_map[pin].pad);
    return GPIO_OK;
}

const GpioIf gpio_stm32 = {
    .init   = stm32_init,
    .write  = stm32_write,
    .read   = stm32_read,
    .toggle = stm32_toggle,
};
