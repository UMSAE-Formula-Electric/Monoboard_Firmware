/**
 * @file fault_stm32.h
 * @brief Cortex-M4 fault handlers, persistent crash dump and reset-cause
 * latch for the STM32F446 (issue #45).
 *
 * Owns HardFault_Handler, MemManage_Handler, BusFault_Handler and
 * UsageFault_Handler (overriding the startup file's weak Default_Handler
 * aliases). On a fault they capture the stacked frame and fault-status
 * registers into a CrashRecord in the `.noinit` RAM section, seal it, and
 * reset. The next boot's fault_stm32_boot() latches RCC_CSR, picks up the
 * record if it is valid, and invalidates it so it is reported exactly once.
 *
 * Composition roots only (ARCHITECTURE.md): main_stm32.c calls
 * fault_stm32_boot() first thing, and FreeRTOS hooks wired there may call
 * fault_stm32_record_and_reset().
 */
#ifndef MONOBOARD_FAULT_STM32_H
#define MONOBOARD_FAULT_STM32_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "crash_record.h"

/**
 * Call first in main(), before clocks, peripherals or anything else that
 * could overwrite RAM or reset again. Latches and clears RCC_CSR, copies a
 * reportable crash record out of `.noinit` and invalidates the original,
 * and enables the MemManage/BusFault/UsageFault handlers (SHCSR) plus the
 * divide-by-zero trap (CCR.DIV_0_TRP) so faults report as themselves
 * instead of escalating straight to HardFault. Idempotent per boot: only
 * the first call reads the hardware.
 */
void fault_stm32_boot(void);

/** Reset cause latched by fault_stm32_boot(). */
ResetCause fault_stm32_reset_cause(void);

/** Raw RCC_CSR latched by fault_stm32_boot() (before clearing). */
uint32_t fault_stm32_reset_flags(void);

/**
 * Copy out the crash record found at boot.
 * @param out  destination; untouched when there was none
 * @return true iff the previous run left a reportable record
 */
bool fault_stm32_take_crash(CrashRecord *out);

/**
 * Render the boot banner (and the crash dump, if any) into @p buf -- see
 * crash_format_report(). Safe to call any time after fault_stm32_boot().
 * @param buf         destination, NUL-terminated when @p buf_len > 0
 * @param buf_len     size of @p buf
 * @param fw_version  firmware version string for the banner
 * @return the full report length, as crash_format_report()
 */
size_t fault_stm32_format_boot_report(char *buf, size_t buf_len, const char *fw_version);

/**
 * Persist a software-detected crash and reset the MCU. Intended target for
 * the FreeRTOS hooks (issue #46): vApplicationStackOverflowHook passes
 * CRASH_CAUSE_STACK_OVERFLOW and the offending task's name, the
 * malloc-failed / assert hooks their own causes. No blocking, no
 * allocation; safe before and after the scheduler starts and from any
 * context. The caller's return address is stored as the record's PC.
 * @param cause      what went wrong (not CRASH_CAUSE_NONE)
 * @param task_name  task to blame, or NULL for the running task
 */
void fault_stm32_record_and_reset(CrashCause cause, const char *task_name)
    __attribute__((noreturn));

#endif  // MONOBOARD_FAULT_STM32_H
