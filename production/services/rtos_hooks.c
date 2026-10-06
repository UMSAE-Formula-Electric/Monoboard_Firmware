/**
 * @file rtos_hooks.c
 * @brief FreeRTOS application hooks -- see rtos_hooks.h and the "Fatal
 * errors" policy in ARCHITECTURE.md.
 *
 * Kept in their own CMake target (rtos_hooks) because the dependency runs
 * both ways: these call FreeRTOS macros, and the kernel calls these by
 * name. Linking freertos_kernel therefore always brings them along.
 */
#include "rtos_hooks.h"

#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#if configMAX_TASK_NAME_LEN > FAULT_TASK_NAME_LEN
#    error "FAULT_TASK_NAME_LEN (fatal_if.h) must hold configMAX_TASK_NAME_LEN characters"
#endif

static const FatalIf    *installed_fatal;
static volatile uint32_t idle_count;
static volatile uint32_t halting; /* set once a fault is being handled */
static FaultRecord       last_fault;

/* Interrupts off, spin. Used before a handler is installed, if a fault is
 * raised while another is already being handled, or if a handler breaks
 * its contract and returns. The watchdog turns this into a reset. */
static void __attribute__((noreturn)) spin_forever(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

static void __attribute__((noreturn)) halt_with(const FaultRecord *record)
{
    const FatalIf *fatal = installed_fatal;

    if ((fatal != NULL) && (fatal->halt != NULL)) {
        fatal->halt(record);
    }
    spin_forever();
}

/* First step of every fatal hook: stop the world and claim the record.
 * Returns false if a fault is already being handled (nested fault). */
static int begin_fault(FaultKind kind)
{
    taskDISABLE_INTERRUPTS();
    if (halting != 0U) {
        return 0;
    }
    halting                 = 1U;
    last_fault.kind         = kind;
    last_fault.file         = NULL;
    last_fault.line         = 0U;
    last_fault.task_name[0] = '\0';
    return 1;
}

void rtos_hooks_install(const FatalIf *fatal)
{
    installed_fatal = fatal;
}

void rtos_assert_failed(const char *file, int line)
{
    if (begin_fault(FAULT_KIND_ASSERT) == 0) {
        spin_forever();
    }
    last_fault.file = file;
    last_fault.line = (line > 0) ? (uint32_t)line : 0U;
    halt_with(&last_fault);
}

uint32_t rtos_hooks_idle_count(void)
{
    return idle_count;
}

/* -- Kernel-called hooks (prototypes in FreeRTOS task.h / timers.h) ------- */

/* Signature is the kernel's (task.h): pcTaskName cannot be made const. */
void vApplicationStackOverflowHook(TaskHandle_t xTask,
                                   // cppcheck-suppress constParameterPointer
                                   char *pcTaskName)  // NOLINT(readability-non-const-parameter)
{
    (void)xTask;
    if (begin_fault(FAULT_KIND_STACK_OVERFLOW) == 0) {
        spin_forever();
    }
    /* The TCB holding the name may itself be what the overflow trampled;
     * the copy is bounded and always terminated regardless. */
    if (pcTaskName != NULL) {
        size_t i;

        for (i = 0U; (i < (FAULT_TASK_NAME_LEN - 1U)) && (pcTaskName[i] != '\0'); i++) {
            last_fault.task_name[i] = pcTaskName[i];
        }
        last_fault.task_name[i] = '\0';
    }
    halt_with(&last_fault);
}

/* Unreachable by construction: no heap_*.c is linked (static allocation
 * only), so pvPortMalloc() does not exist. Kept so that if a heap is ever
 * re-added, a failed allocation is a named fatal error, not a NULL deref. */
void vApplicationMallocFailedHook(void)
{
    if (begin_fault(FAULT_KIND_MALLOC_FAILED) == 0) {
        spin_forever();
    }
    halt_with(&last_fault);
}

/* Runs in the idle task, every pass. Must never block: count and leave. */
void vApplicationIdleHook(void)
{
    idle_count++;
}

/* configSUPPORT_STATIC_ALLOCATION=1 makes the application responsible for
 * the idle and timer tasks' memory. */
void vApplicationGetIdleTaskMemory(StaticTask_t          **ppxIdleTaskTCBBuffer,
                                   StackType_t           **ppxIdleTaskStackBuffer,
                                   configSTACK_DEPTH_TYPE *puxIdleTaskStackSize)
{
    static StaticTask_t idle_tcb;
    static StackType_t  idle_stack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer   = &idle_tcb;
    *ppxIdleTaskStackBuffer = idle_stack;
    *puxIdleTaskStackSize   = (configSTACK_DEPTH_TYPE)configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t          **ppxTimerTaskTCBBuffer,
                                    StackType_t           **ppxTimerTaskStackBuffer,
                                    configSTACK_DEPTH_TYPE *puxTimerTaskStackSize)
{
    static StaticTask_t timer_tcb;
    static StackType_t  timer_stack[configTIMER_TASK_STACK_DEPTH];

    *ppxTimerTaskTCBBuffer   = &timer_tcb;
    *ppxTimerTaskStackBuffer = timer_stack;
    *puxTimerTaskStackSize   = (configSTACK_DEPTH_TYPE)configTIMER_TASK_STACK_DEPTH;
}
