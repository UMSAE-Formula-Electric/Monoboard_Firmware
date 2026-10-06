/**
 * @file fatal_stm32.c
 * @brief FatalIf implementation for the STM32F446 (Cortex-M4).
 *
 * Runs with interrupts already disabled (rtos_hooks.c does that first).
 * No HAL calls, no RTOS, no allocation: only CMSIS core register access,
 * so it works before the scheduler starts and from any context.
 */
#include "fatal_stm32.h"

#include "stm32f4xx.h" /* CMSIS core: CoreDebug, __BKPT, NVIC_SystemReset */

/* Seam for issue #45 (HardFault handler / crash dump): copy *record into
 * the reset-surviving crash-dump area so the next boot can report it.
 * Intentionally empty until #45 defines that area. */
static void crash_dump_store(const FaultRecord *record)
{
    (void)record;
}

static void stm32_halt(const FaultRecord *record)
{
    crash_dump_store(record);

#ifndef NDEBUG
    /* Debug builds with a debugger attached: stop here so the fault can be
     * inspected in place (`record` is live in the caller's frame). If the
     * debugger has since detached, C_DEBUGEN can stay latched; the halted
     * core then stops kicking the IWDG, which resets the MCU anyway. */
    if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U) {
        __BKPT(0);
    }
#endif

    NVIC_SystemReset();
}

const FatalIf fatal_stm32 = {
    .halt = stm32_halt,
};
