/**
 * @file fatal_fake.c
 * @brief Host FatalIf -- print the FaultRecord, then abort().
 */
#include "fatal_fake.h"

#include <stdio.h>
#include <stdlib.h>

static void fake_halt(const FaultRecord *record)
{
    if (record == NULL) {
        (void)fprintf(stderr, "FATAL: (no fault record)\n");
    } else {
        switch (record->kind) {
            case FAULT_KIND_ASSERT:
                (void)fprintf(stderr, "FATAL: configASSERT failed at %s:%lu\n",
                              (record->file != NULL) ? record->file : "?",
                              (unsigned long)record->line);
                break;
            case FAULT_KIND_STACK_OVERFLOW:
                (void)fprintf(stderr, "FATAL: stack overflow in task '%s'\n", record->task_name);
                break;
            case FAULT_KIND_MALLOC_FAILED:
                (void)fprintf(stderr, "FATAL: malloc failed (static-allocation-only build)\n");
                break;
            case FAULT_KIND_NONE:
            default:
                (void)fprintf(stderr, "FATAL: unknown fault kind %d\n", (int)record->kind);
                break;
        }
    }
    (void)fflush(stderr);
    abort();
}

const FatalIf fatal_fake = {
    .halt = fake_halt,
};
