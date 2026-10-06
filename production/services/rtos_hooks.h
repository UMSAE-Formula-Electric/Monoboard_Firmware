/**
 * @file rtos_hooks.h
 * @brief The application callbacks FreeRTOS itself calls (issue #46):
 * configASSERT, stack overflow, malloc failed, idle, and the static
 * memory for the idle and timer tasks.
 *
 * Every fatal hook fills in a FaultRecord and hands it to the FatalIf the
 * composition root installed with rtos_hooks_install(). Before that (or if
 * none is installed) the fallback is "interrupts off, spin" -- the
 * independent watchdog turns that into a reset.
 *
 * The kernel finds the vApplication* hooks by name; they are declared in
 * FreeRTOS's own task.h / timers.h, not here.
 */
#ifndef RTOS_HOOKS_H
#define RTOS_HOOKS_H

#include <stdint.h>

#include "fatal_if.h"

/**
 * Install the platform's fatal-error handler. Call first thing in main(),
 * before anything that could trip a configASSERT.
 * @param fatal  handler to receive every FaultRecord; NULL restores the
 *               interrupts-off spin fallback
 */
void rtos_hooks_install(const FatalIf *fatal);

/* rtos_assert_failed(file, line) -- the configASSERT() target -- is also
 * implemented here, but declared in FreeRTOSConfig.h: the kernel needs the
 * prototype in every translation unit that can assert. */

/**
 * Number of times the idle hook has run since boot (wraps at 2^32). A
 * single aligned 32-bit read -- safe from any task. Healthrun (#38) turns
 * the rate of change into a CPU-idle figure.
 */
uint32_t rtos_hooks_idle_count(void);

#endif /* RTOS_HOOKS_H */
