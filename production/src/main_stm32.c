/**
 * @file main_stm32.c
 * @brief Composition root stub (see ARCHITECTURE.md).
 *
 * Real wiring -- drivers into services, task/queue creation, log sink
 * install, scheduler start -- lands here once those layers exist. No
 * stm32f4xx_it.c: FreeRTOSConfig.h renames the ARM_CM4F port's handlers
 * onto the vector table's names.
 */
#include "fatal_stm32.h"
#include "rtos_hooks.h"

int main(void)
{
    /* First: from here on a configASSERT / stack overflow resets the MCU
     * through fatal_stm32 instead of spinning until the watchdog bites. */
    rtos_hooks_install(&fatal_stm32);

    for (;;) {
    }

    return 0;
}
