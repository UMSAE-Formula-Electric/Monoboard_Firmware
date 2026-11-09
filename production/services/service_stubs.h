/**
 * @file service_stubs.h
 * @brief Step functions rtos_system.c schedules, until each service has a
 * real header (#32). Move a prototype to its service's header as it lands.
 */
#ifndef SERVICE_STUBS_H
#define SERVICE_STUBS_H

void vPedalServiceStep(void);
void vMotorcontrollerServiceStep(void);
void vBMSServiceStep(void);
void vLoggingServiceStep(void);
void vIMDServiceStep(void);
void vCANRouterServiceStep(void);
void vHealthrunServiceStep(void);

#endif /* SERVICE_STUBS_H */
