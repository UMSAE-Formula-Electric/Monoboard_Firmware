/**
 * @file fatal_if.h
 * @brief Fatal-error contract: what the platform does once the firmware
 * has decided it cannot continue -- see issue #46 and the "Fatal errors"
 * policy in ARCHITECTURE.md.
 *
 * The RTOS hooks (services/rtos_hooks.c) fill in a FaultRecord saying
 * what went wrong and where, then hand it to the injected FatalIf. The
 * driver behind it decides how the image stops: fatal_stm32 resets the
 * MCU (or breakpoints under a debugger), fatal_fake prints the record and
 * aborts the host process so a test run fails loudly.
 *
 * Header-only, standard-library types only -- see ARCHITECTURE.md,
 * Interface Layer rules.
 */
#ifndef MONOBOARD_FATAL_IF_H
#define MONOBOARD_FATAL_IF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Task-name capacity, terminator included. Must be at least FreeRTOS's
 * configMAX_TASK_NAME_LEN -- rtos_hooks.c refuses to build otherwise. */
#define FAULT_TASK_NAME_LEN 16U

/* Which safety net fired. */
typedef enum {
    FAULT_KIND_NONE = 0,       /* no fault recorded */
    FAULT_KIND_ASSERT,         /* configASSERT() failed: file/line valid */
    FAULT_KIND_STACK_OVERFLOW, /* FreeRTOS stack check tripped: task_name valid */
    FAULT_KIND_MALLOC_FAILED,  /* pvPortMalloc() failed -- static-only build bug */
} FaultKind;

/* Everything known about a fatal error at the moment it is detected.
 * Plain data, no pointers into RAM, so a crash-dump store (#45) can copy
 * it verbatim. @c file points at a string literal in the image (flash on
 * target), so it stays meaningful across a warm reset of the same image. */
typedef struct {
    FaultKind   kind;
    const char *file;                           /* source file of a failed assert, else NULL */
    uint32_t    line;                           /* line of a failed assert, else 0 */
    char        task_name[FAULT_TASK_NAME_LEN]; /* offending task, "" if none */
} FaultRecord;

typedef struct {
    /**
     * Stop the image. Called with interrupts already disabled; must not
     * block, allocate, or use the RTOS. Never returns.
     * @param record  what went wrong; valid only for the duration of the
     *                call (copy it if it must outlive the call)
     */
    void (*halt)(const FaultRecord *record);
} FatalIf;

#ifdef __cplusplus
}
#endif

#endif /* MONOBOARD_FATAL_IF_H */
