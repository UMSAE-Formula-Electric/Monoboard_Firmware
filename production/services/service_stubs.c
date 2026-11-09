/**
 * @file service_stubs.c
 * @brief Empty step functions so rtos_system.c links before the services
 * exist (#32). Delete each stub as its service lands: a real definition
 * alongside its stub is a duplicate-symbol link error.
 */

#include "service_stubs.h"

void vPedalServiceStep(void)
{
}

void vMotorcontrollerServiceStep(void)
{
}

void vBMSServiceStep(void)
{
}

void vLoggingServiceStep(void)
{
}

void vIMDServiceStep(void)
{
}

void vCANRouterServiceStep(void)
{
}

void vHealthrunServiceStep(void)
{
}
