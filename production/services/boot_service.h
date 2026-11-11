/**
 * @file boot_service.h
 * @brief Boot service: first to run after the scheduler starts, then a
 * heartbeat on GPIO_PIN_TEST (PC0 on the board) for the rest of the run.
 *
 * The heartbeat proves on hardware that the kernel tick and the clock tree
 * agree: rtos_system.c steps this every 500 ms, so the pin is a 1 Hz square
 * wave, and any other period means the tick or the clocks are wrong.
 */
#ifndef BOOT_SERVICE_H
#define BOOT_SERVICE_H

#include <stdbool.h>

#include "gpio_if.h"
#include "if_status.h"

/** Boot service context. The composition root owns the storage and sets
 *  @c gpio before init; @c ready is the service's own. */
typedef struct {
    const GpioIf *gpio;  /**< GPIO driver; must outlive the service */
    bool          ready; /**< set by a successful boot_service_init() */
} BootService;

/* Both functions take @c void *ctx so rtos_system.c's PeriodicTask can call
 * them directly; @p ctx must point to a BootService. */

/**
 * Configure GPIO_PIN_TEST as a push-pull output, driven low. Call once,
 * from the boot task, before the first vBootServiceStep().
 * @param ctx  BootService with @c gpio set
 * @return #IF_OK, #IF_HW_FAULT if @p ctx or its @c gpio is NULL, or the
 *         driver's error
 */
IfStatus boot_service_init(void *ctx);

/** Toggle the heartbeat pin. Does nothing before a successful init.
 *  @param ctx  the BootService passed to boot_service_init() */
void vBootServiceStep(void *ctx);

#endif /* BOOT_SERVICE_H */
