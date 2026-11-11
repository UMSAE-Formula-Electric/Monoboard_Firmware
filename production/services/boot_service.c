/**
 * @file boot_service.c
 * @brief Boot service (see boot_service.h).
 */
#include "boot_service.h"

#include <stddef.h>

static const GpioConfig heartbeat_config = {
    .dir     = GPIO_DIR_OUTPUT,
    .pull    = GPIO_PULL_NONE,
    .initial = GPIO_LOW,
};

IfStatus boot_service_init(void *ctx)
{
    BootService *svc = ctx;

    if (svc == NULL) {
        return IF_HW_FAULT;
    }

    /* Cleared first so a failed re-init leaves vBootServiceStep() a no-op. */
    svc->ready = false;
    if (svc->gpio == NULL) {
        return IF_HW_FAULT;
    }

    const IfStatus status = svc->gpio->init(GPIO_PIN_TEST, &heartbeat_config);

    svc->ready = (status == IF_OK);
    return status;
}

/* cppcheck-suppress constParameterPointer ; signature fixed by PeriodicTask.step */
void vBootServiceStep(void *ctx)
{
    const BootService *svc = ctx;

    if ((svc != NULL) && svc->ready) {
        (void)svc->gpio->toggle(GPIO_PIN_TEST);
    }
}
