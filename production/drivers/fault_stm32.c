/**
 * @file fault_stm32.c
 * @brief Fault handlers, `.noinit` crash dump and RCC_CSR reset-cause latch
 * for the STM32F446 (issue #45). See fault_stm32.h.
 *
 * Fault path constraints: no blocking, no allocation, no libc, no HAL --
 * the handler may run with a corrupt heap, a dead scheduler or a smashed
 * task stack. It writes straight into the no-init record (no large locals)
 * and resets. The C part runs on the MSP (handler mode); the faulting
 * stack is only read, and only after its address is range-checked.
 */
#include "fault_stm32.h"

#include "FreeRTOS.h"
#include "stm32f4xx.h"
#include "task.h"

/* Weak references: the task name is a nice-to-have, and the fault module
 * must not be what drags the kernel (and its mandatory application hooks)
 * into an image that does not otherwise run FreeRTOS. Once the composition
 * root starts the scheduler, tasks.c is linked and these resolve normally;
 * until then they are NULL and no task name is recorded. */
#pragma weak xTaskGetSchedulerState
#pragma weak pcTaskGetName

/* Survives a warm reset: the linker script places .noinit outside the
 * startup code's .bss zero-fill, and as NOLOAD it is never initialised from
 * flash. Validity is entirely down to the magic + version + CRC seal. */
static CrashRecord crash_noinit __attribute__((section(".noinit")));

/* Latched by fault_stm32_boot(); ordinary .bss, zeroed by startup. */
static bool        boot_done;
static uint32_t    boot_csr;
static ResetCause  boot_cause;
static bool        boot_has_crash;
static CrashRecord boot_crash;

/* Top of RAM from the linker script (initial MSP). SRAM1 + SRAM2 are
 * contiguous on the F446, so [SRAM1_BASE, _estack) is all of RAM. */
extern uint32_t _estack;

/* Vector-table names (startup_stm32f446xx.s); there is no stm32f4xx_it.h. */
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);

/* Reached only from the naked entry stub below, by name -- hence external
 * linkage and `used`, so neither --gc-sections nor LTO drops or renames it. */
void fault_stm32_capture(const uint32_t *frame, uint32_t exc_return)
    __attribute__((used, noreturn));

static uint32_t ram_start(void)
{
    return (uint32_t)SRAM1_BASE;
}

static uint32_t ram_end(void)
{
    return (uint32_t)&_estack;
}

/* Name of the running task, or NULL before the scheduler starts (no
 * current TCB) or if the TCB pointer is plainly corrupt. */
static const char *running_task_name(void)
{
    const char *name;
    uint32_t    addr;

    if ((xTaskGetSchedulerState == NULL) || (pcTaskGetName == NULL)
        || (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED)) {
        return NULL;
    }
    name = pcTaskGetName(NULL);
    addr = (uint32_t)name;
    if ((name == NULL) || (addr < ram_start()) || (addr >= (ram_end() - CRASH_TASK_NAME_LEN))) {
        return NULL;
    }
    return name;
}

/* Halt in the debugger if one is attached (bench), otherwise reset (car). */
static void __attribute__((noreturn)) stop_and_reset(void)
{
    __DSB();
    if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U) {
        __BKPT(0);
    }
    NVIC_SystemReset();
}

/**
 * Entry stub shared by all four fault vectors. Naked: no prologue may touch
 * the stack before we know which stack was live. EXC_RETURN (LR) bit 2 says
 * whether the faulting context was on PSP (a task) or MSP (pre-scheduler,
 * an ISR, or the kernel); that stack's pointer is the stacked frame.
 */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile("tst   lr, #4                \n"
                   "ite   eq                    \n"
                   "mrseq r0, msp               \n"
                   "mrsne r0, psp               \n"
                   "mov   r1, lr                \n"
                   "b     fault_stm32_capture   \n");
}

/* Same stub: the C side tells the four apart from IPSR. */
void MemManage_Handler(void) __attribute__((alias("HardFault_Handler")));
void BusFault_Handler(void) __attribute__((alias("HardFault_Handler")));
void UsageFault_Handler(void) __attribute__((alias("HardFault_Handler")));

void fault_stm32_capture(const uint32_t *frame, uint32_t exc_return)
{
    const uint32_t sp       = (uint32_t)frame;
    const bool     frame_ok = crash_frame_address_ok(sp, ram_start(), ram_end());

    __disable_irq();

    crash_noinit.cause = (uint32_t)crash_cause_from_exception(__get_IPSR() & IPSR_ISR_Msk);
    crash_record_set_frame(&crash_noinit, frame_ok ? frame : NULL);
    crash_noinit.exc_return = exc_return;
    crash_noinit.sp         = sp;
    crash_noinit.cfsr       = SCB->CFSR;
    crash_noinit.hfsr       = SCB->HFSR;
    crash_noinit.mmfar      = SCB->MMFAR;
    crash_noinit.bfar       = SCB->BFAR;
    crash_record_set_task_name(&crash_noinit, running_task_name());
    crash_record_seal(&crash_noinit);

    stop_and_reset();
}

void fault_stm32_record_and_reset(CrashCause cause, const char *task_name)
{
    const uint32_t caller = (uint32_t)__builtin_return_address(0);

    __disable_irq();

    crash_record_set_frame(&crash_noinit, NULL);
    crash_noinit.cause      = (uint32_t)cause;
    crash_noinit.pc         = caller;
    crash_noinit.exc_return = 0U;
    crash_noinit.sp         = __get_MSP();
    crash_noinit.cfsr       = 0U;
    crash_noinit.hfsr       = 0U;
    crash_noinit.mmfar      = 0U;
    crash_noinit.bfar       = 0U;
    crash_record_set_task_name(&crash_noinit,
                               (task_name != NULL) ? task_name : running_task_name());
    crash_record_seal(&crash_noinit);

    stop_and_reset();
}

void fault_stm32_boot(void)
{
    if (boot_done) {
        return;
    }
    boot_done = true;

    /* Latch, then clear, so the next reset reports only its own cause. */
    boot_csr = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;
    boot_cause = reset_cause_decode(boot_csr);

    if (crash_record_is_reportable(&crash_noinit, boot_cause)) {
        boot_crash     = crash_noinit;
        boot_has_crash = true;
    }
    /* Report-once: whatever was there (dump or power-on garbage) is gone. */
    crash_record_invalidate(&crash_noinit);

    /* Let MemManage/BusFault/UsageFault fire as themselves rather than
     * escalate to HardFault, and make integer divide-by-zero trap instead
     * of silently yielding 0. Unaligned-access trapping stays off: unaligned
     * LDR/STR is architecturally legal and newlib's memcpy relies on it. */
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
    __DSB();
    __ISB();
}

ResetCause fault_stm32_reset_cause(void)
{
    return boot_cause;
}

uint32_t fault_stm32_reset_flags(void)
{
    return boot_csr;
}

bool fault_stm32_take_crash(CrashRecord *out)
{
    if (!boot_has_crash || (out == NULL)) {
        return false;
    }
    *out = boot_crash;
    return true;
}

size_t fault_stm32_format_boot_report(char *buf, size_t buf_len, const char *fw_version)
{
    return crash_format_report(buf, buf_len, fw_version, boot_cause, boot_csr,
                               boot_has_crash ? &boot_crash : NULL);
}
