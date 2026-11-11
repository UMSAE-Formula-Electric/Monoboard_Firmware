/**
 * @file service_stubs.c
 * @brief Empty step functions so rtos_system.c links before the services
 * exist (#32). Delete each stub as its service lands: a real definition
 * alongside its stub is a duplicate-symbol link error.
 */

#include "service_stubs.h"

void vPedalServiceStep(void *ctx)
{
    (void)ctx;
}

void vMotorcontrollerServiceStep(void *ctx)
{
    (void)ctx;
}

void vBMSServiceStep(void *ctx)
{
    (void)ctx;
}

void vLoggingServiceStep(void *ctx)
{
    (void)ctx;
}

void vIMDServiceStep(void *ctx)
{
    (void)ctx;
}

void vCANRouterServiceStep(void *ctx)
{
    (void)ctx;
}

void vHealthrunServiceStep(void *ctx)
{
    (void)ctx;
}
