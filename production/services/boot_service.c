/**
 * @file boot_service.c
 * @brief Boot service (see boot_service.h).
 */
#include "boot_service.h"

#include <stddef.h>

IfStatus boot_service_init(void *ctx)
{
    BootService *svc = ctx;

    if (svc == NULL) {
        return IF_HW_FAULT;
    }
    svc->ready = true;
    return IF_OK;
}

/* cppcheck-suppress constParameterPointer ; signature fixed by PeriodicTask.step */
void vBootServiceStep(void *ctx)
{
    (void)ctx; /* SEAM: heartbeat on a DEBUG_LED PWM channel (boot_service.h) */
}
