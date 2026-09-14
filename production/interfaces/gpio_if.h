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

#include "if_status.h"

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

typedef struct {
    GpioDir   dir;
    GpioPull  pull;    /* input: bias resistor; output: normally GPIO_PULL_NONE */
    GpioLevel initial; /* output only: level driven immediately at init (ignored for inputs) */
} GpioConfig;

typedef enum {
    GPIO_EDGE_RISING = 0,
    GPIO_EDGE_FALLING,
    GPIO_EDGE_BOTH,
} GpioEdge;

/**
 * Fired when a pin armed by on_edge() sees the edge it was armed for (e.g.
 * the RTD start button). Invoked from ISR context -- FromISR APIs only,
 * no blocking, minimal work.
 * @param ctx    opaque pointer, exactly what was passed to on_edge()
 * @param pin    pin that triggered
 * @param level  level the pin settled to immediately after the edge
 */
typedef void (*GpioEdgeCb)(void *ctx, GpioPin pin, GpioLevel level);

typedef struct {
    /**
     * @return #IF_OK on success, or #IF_HW_FAULT on bad arguments
     *         (unknown pin, NULL @p config, or an out-of-range enum in it)
     */
    IfStatus (*init)(GpioPin pin, const GpioConfig *config);

    /**
     * Output pins only.
     * @return #IF_OK on success, or #IF_HW_FAULT on bad arguments
     *         (unknown pin, out-of-range @p level), @p pin not configured
     *         as an output, or @p pin never init()'d
     */
    IfStatus (*write)(GpioPin pin, GpioLevel level);

    /**
     * Input or output.
     * @return #IF_OK on success, or #IF_HW_FAULT on bad arguments
     *         (unknown pin, NULL @p level) or @p pin never init()'d
     */
    IfStatus (*read)(GpioPin pin, GpioLevel *level);

    /**
     * Output pins only.
     * @return #IF_OK on success, or #IF_HW_FAULT if @p pin is unknown,
     *         not configured as an output, or never init()'d
     */
    IfStatus (*toggle)(GpioPin pin);

    /**
     * Arm (or disarm) an edge-triggered callback on an input pin.
     * Replaces whatever callback was previously armed on @p pin.
     * @param pin   input pin to watch
     * @param edge  which transition(s) to fire on
     * @param cb    called from ISR context when the armed edge occurs;
     *              NULL disarms @p pin
     * @param ctx   opaque pointer passed back to @p cb
     * @return #IF_OK on success, or #IF_HW_FAULT on bad arguments
     *         (unknown pin, unknown edge, or pin never init()'d) or if
     *         @p pin is not configured as an input
     */
    IfStatus (*on_edge)(GpioPin pin, GpioEdge edge, GpioEdgeCb cb, void *ctx);
} GpioIf;

#ifdef __cplusplus
}
#endif

#endif /* GPIO_IF_H */
