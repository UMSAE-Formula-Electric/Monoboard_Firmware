/**
 * @file rtos_system.h
 * @brief The RTOS object graph shared by every executable that runs the
 * scheduler (see ARCHITECTURE.md, Composition Root).
 *
 * main_stm32.c and main_desktop.c each pick concrete drivers and pass them
 * in here. Everything else -- queues, tasks, priorities, stack sizes -- is
 * defined once in rtos_system.c, so the target and the simulation cannot
 * drift apart.
 */
#ifndef RTOS_SYSTEM_H
#define RTOS_SYSTEM_H

#include <stdbool.h>

#include "adc_if.h"
#include "can_if.h"
#include "gpio_if.h"
#include "pwm_if.h"
#include "uart_if.h"
#include "wdg_if.h"

/** The concrete driver behind each interface, chosen by the calling main. */
typedef struct {
    const AdcIf  *adc;
    const CanIf  *can;
    const GpioIf *gpio;
    const PwmIf  *pwm;
    const UartIf *uart;
    const WdgIf  *wdg;
} RtosSystemDrivers;

/**
 * Create every shared queue and task. Does not start the scheduler; the
 * caller does that, because only it knows what must run first.
 * @param drivers  driver set to inject into the services; must outlive the
 *                 tasks
 * @return true if every object was created
 */
bool rtos_system_create(const RtosSystemDrivers *drivers);

#endif /* RTOS_SYSTEM_H */
