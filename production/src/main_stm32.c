/**
 * @file main_stm32.c
 * @brief Composition root stub (see ARCHITECTURE.md).
 *
 * Real wiring -- drivers into services, task/queue creation, log sink
 * install, scheduler start -- lands here once those layers exist. No
 * stm32f4xx_it.c: FreeRTOSConfig.h renames the ARM_CM4F port's handlers
 * onto the vector table's names, and fault_stm32.c owns the fault vectors.
 */
#include "fault_stm32.h"
#include "fatal_stm32.h"
#include "rtos_hooks.h"

/* Placeholder until build info (issue #49) provides the real version. */
#define FW_VERSION_STRING "unversioned"

/* Boot banner + crash dump from the previous run (issue #45). Kept in RAM
 * so it can be read with a debugger (`print boot_report`) until the logging
 * service (issue #23) exists to send it over the log sink and CAN. */
static char boot_report[512] __attribute__((used));


int main(void)
{
    /* First: from here on a configASSERT / stack overflow resets the MCU
     * through fatal_stm32 instead of spinning until the watchdog bites. */
    rtos_hooks_install(&fatal_stm32);
    fault_stm32_boot();
    (void)fault_stm32_format_boot_report(boot_report, sizeof(boot_report), FW_VERSION_STRING);

    for (;;) {
      
    }

    return 0;
}
