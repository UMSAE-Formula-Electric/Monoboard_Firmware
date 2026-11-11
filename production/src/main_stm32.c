/**
 * @file main_stm32.c
 * @brief Firmware composition root (see ARCHITECTURE.md).
 *
 * Selects the STM32 drivers for rtos_system.c, which owns the shared queues
 * and tasks, then starts the scheduler. The boot task brings the drivers up
 * and creates the shared tasks, so no service runs before its drivers do.
 * Log sink install lands here once that layer exists.
 *
 * No stm32f4xx_it.c: FreeRTOSConfig.h renames the ARM_CM4F port's SVC and
 * PendSV handlers onto the vector table's names, SysTick_Handler is below,
 * and fault_stm32.c owns the fault vectors.
 */
#include "FreeRTOS.h"
#include "task.h"

#include "adc_stm32.h"
#include "can_stm32.h"
#include "cli.h"
#include "fatal_stm32.h"
#include "fault_stm32.h"
#include "fw_info.h"
#include "gpio_stm32.h"
#include "main.h"
#include "pwm_stm32.h"
#include "rtos_hooks.h"
#include "rtos_system.h"
#include "time_stm32.h"
#include "usart_stm32.h"
#include "wdg_stm32.h"

void SystemClock_Config(void);
void SysTick_Handler(void);
void xPortSysTickHandler(void); /* ARM_CM4F port.c; no public header */

/* Highest application priority: nothing it creates can preempt it, so the
 * lower-priority services all start once it has deleted itself. The stack
 * size is a PLACEHOLDER until #55 sets a measured value. */
#define BOOT_TASK_PRIORITY    ((UBaseType_t)(configMAX_PRIORITIES - 2U))
#define BOOT_TASK_STACK_WORDS 256U

/* Debug CLI link (issue #51): USART2, the ST-LINK virtual COM port on a
 * Nucleo. PLACEHOLDER baud rate until the team confirms one. */
#define DEBUG_UART_BAUD_BPS 115200U

/* Boot banner + crash dump from the previous run (issue #45). Kept in RAM
 * so it can be read with a debugger (`print boot_report`) and by the debug
 * CLI (`boot`), until the logging service (issue #23) exists to send it over
 * the log sink and CAN. */
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

static StaticTask_t boot_task_tcb;
static StackType_t  boot_task_stack[BOOT_TASK_STACK_WORDS];

/* SysTick runs from HAL_Init() on, but the kernel's tick handler reads the
 * current task, so it must wait for the scheduler. The HAL needs its own tick
 * throughout: without it HAL timeouts never expire and a stuck peripheral
 * hangs instead of returning an error. */
void SysTick_Handler(void)
{
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

/* Runs once, with interrupts live and the scheduler running, which driver
 * init needs: the HAL's timeouts count ticks.
 *
 * SEAM: driver init goes first in this function, asserting on each result.
 * CAN and the UART wait on a confirmed bitrate and baud rate. The watchdog
 * waits on a service that kicks it (#38), and belongs in main() because its
 * init must run before the scheduler starts. */
/* cppcheck-suppress constParameterCallback ; signature fixed by TaskFunction_t */
static void boot_task(void *param)
{
    (void)param;

    {
        const UartConfig uart_config = {
            .baud_rate_bps = DEBUG_UART_BAUD_BPS,
            .data_bits     = 8U,
            .parity        = UART_IF_PARITY_NONE,
            .stop_bits     = UART_STOP_BITS_1,
        };
        /* The pin the debug-only `set` command drives. */
        const GpioConfig test_pin_config = {
            .dir     = GPIO_DIR_OUTPUT,
            .pull    = GPIO_PULL_NONE,
            .initial = GPIO_LOW,
        };
        const IfStatus uart_status = usart_stm32.init(&uart_config);
        const IfStatus gpio_status = gpio_stm32.init(GPIO_PIN_TEST, &test_pin_config);

        configASSERT(uart_status == IF_OK);
        configASSERT(gpio_status == IF_OK);
    }

    {
        const bool created = rtos_system_create(&stm32_drivers);

        configASSERT(created);
    }

    vTaskDelete(NULL);
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* The core clock is final now; now_us() only reads the enabled counter. */
    time_stm32_init();

    /* First: from here on a configASSERT / stack overflow resets the MCU
     * through fatal_stm32 instead of spinning until the watchdog bites. */
    rtos_hooks_install(&fatal_stm32);
    fault_stm32_boot();
    (void)fault_stm32_format_boot_report(boot_report, sizeof(boot_report), fw_info_get()->version);
    cli_set_boot_report(boot_report);

    {
        const TaskHandle_t boot = xTaskCreateStatic(
            boot_task, "boot", (configSTACK_DEPTH_TYPE)BOOT_TASK_STACK_WORDS, NULL,
            BOOT_TASK_PRIORITY, boot_task_stack, &boot_task_tcb);

        configASSERT(boot != NULL);
    }

    vTaskStartScheduler();

    /* Only reachable if the idle or timer task could not be created. */
    configASSERT(false);

    return 0;
}
