/**
 * @file main_stm32.c
 * @brief Firmware composition root (see ARCHITECTURE.md).
 *
 * Brings the board up, selects the STM32 drivers for rtos_system.c, which
 * owns every queue and task (the boot service and its PC0 heartbeat among
 * them), then starts the scheduler. Driver init runs in each service's task,
 * not here. Log sink install lands here once that layer exists.
 *
 * No stm32f4xx_it.c: FreeRTOSConfig.h renames the ARM_CM4F port's SVC,
 * PendSV and SysTick handlers onto the vector table's names, board_stm32.c
 * owns the TIM10 HAL-tick vector, and fault_stm32.c the fault vectors.
 */
#include "FreeRTOS.h"
#include "task.h"

#include "adc_stm32.h"
#include "board_stm32.h"
#include "can_stm32.h"
#include "fatal_stm32.h"
#include "fault_stm32.h"
#include "gpio_stm32.h"
#include "pwm_stm32.h"
#include "rtos_hooks.h"
#include "rtos_system.h"
#include "time_stm32.h"
#include "usart_stm32.h"
#include "wdg_stm32.h"

/* Placeholder until build info (issue #49) provides the real version. */
#define FW_VERSION_STRING "unversioned"

/* Boot banner + crash dump from the previous run (issue #45). Kept in RAM
 * so it can be read with a debugger (`print boot_report`) until the logging
 * service (issue #23) exists to send it over the log sink and CAN. */
static char boot_report[512] __attribute__((used));

/* Driver selection: the one place the firmware names concrete drivers. */
static const RtosSystemDrivers stm32_drivers = {
    .adc  = &adc_stm32,
    .can  = &can_stm32,
    .gpio = &gpio_stm32,
    .pwm  = &pwm_stm32,
    .uart = &usart_stm32,
    .wdg  = &wdg_stm32,
};

int main(void)
{
    board_stm32_init();

    /* The core clock is final now; now_us() only reads the enabled counter. */
    time_stm32_init();

    /* First: from here on a configASSERT / stack overflow resets the MCU
     * through fatal_stm32 instead of spinning until the watchdog bites. */
    rtos_hooks_install(&fatal_stm32);
    fault_stm32_boot();
    (void)fault_stm32_format_boot_report(boot_report, sizeof(boot_report), FW_VERSION_STRING);

    /* SEAM: watchdog init goes here once a service kicks it (#38): it must
     * run before the scheduler starts. Every other driver is brought up by
     * its service's init hook in rtos_system.c, inside the service's task. */
    const bool created = rtos_system_create(&stm32_drivers);
    configASSERT(created);

    vTaskStartScheduler();

    /* Only reachable if the idle or timer task could not be created. */
    configASSERT(false);

    return 0;
}
