/**
 * @file service_stubs.h
 * @brief Step functions rtos_system.c schedules, until each service has a
 * real header (#32). Move a prototype to its service's header as it lands,
 * taking that service's context struct in place of @c void *ctx.
 */
#ifndef SERVICE_STUBS_H
#define SERVICE_STUBS_H

void vPedalServiceStep(void *ctx);
void vMotorcontrollerServiceStep(void *ctx);
void vBMSServiceStep(void *ctx);
void vLoggingServiceStep(void *ctx);
void vIMDServiceStep(void *ctx);
void vCANRouterServiceStep(void *ctx);
void vHealthrunServiceStep(void *ctx);

#endif /* SERVICE_STUBS_H */
