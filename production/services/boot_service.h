/**
 * @file boot_service.h
 * @brief Boot service: the first service to run after the scheduler starts.
 *
 * It used to toggle a heartbeat pin to prove the kernel tick and the clock
 * tree agree on hardware. That pin (TEST, PC0) left the .ioc when the real
 * board pinout landed (issue #29), and every remaining discrete output
 * drives something on the car, so the heartbeat is gone for now. SEAM: it
 * comes back on a DEBUG_LED PWM channel once pwm_if has one.
 */
#ifndef BOOT_SERVICE_H
#define BOOT_SERVICE_H

#include <stdbool.h>

#include "if_status.h"

/** Boot service context. The composition root owns the storage. */
typedef struct {
    bool ready; /**< set by a successful boot_service_init() */
} BootService;

/* Both functions take @c void *ctx so rtos_system.c's PeriodicTask can call
 * them directly; @p ctx must point to a BootService. */

/**
 * Bring the service up. Call once, from the boot task, before the first
 * vBootServiceStep().
 * @param ctx  BootService
 * @return #IF_OK, or #IF_HW_FAULT if @p ctx is NULL
 */
IfStatus boot_service_init(void *ctx);

/** Periodic step. Does nothing today (see the file comment).
 *  @param ctx  the BootService passed to boot_service_init() */
void vBootServiceStep(void *ctx);

#endif /* BOOT_SERVICE_H */
