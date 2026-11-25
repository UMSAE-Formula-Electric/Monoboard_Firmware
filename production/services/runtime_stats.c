/**
 * @file runtime_stats.c
 * @brief Run-time stats time base and per-task CPU report (see runtime_stats.h).
 */
#include "runtime_stats.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "timing_if.h"

static RuntimeCounter kernel_counter;
static TaskStatus_t   scratch[RUNTIME_STATS_MAX_TASKS];

uint64_t runtime_counter_update(RuntimeCounter *c, uint32_t raw)
{
    if (!c->primed) {
        c->primed = true;
        c->total  = 0U;
    } else {
        /* Unsigned subtraction survives the wrap. */
        c->total += (uint32_t)(raw - c->last);
    }
    c->last = raw;
    return c->total;
}

uint64_t runtime_stats_counter(void)
{
    uint64_t total;

    /* A context switch can call this while a task is mid-update, so mask
     * interrupts. The FROM_ISR form is valid from a task and from PendSV. */
    const UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();

    total = runtime_counter_update(&kernel_counter, dwt_cycles());
    taskEXIT_CRITICAL_FROM_ISR(saved);
    return total;
}

size_t runtime_stats_snapshot(RuntimeStatsEntry *out, size_t max, uint64_t *total_out)
{
    configRUN_TIME_COUNTER_TYPE total = 0U;
    const UBaseType_t           tasks = uxTaskGetNumberOfTasks();
    UBaseType_t                 count;
    UBaseType_t                 i;

    if ((out == NULL) || (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED)
        || (tasks > RUNTIME_STATS_MAX_TASKS) || (tasks > max)) {
        return 0U;
    }

    count = uxTaskGetSystemState(scratch, RUNTIME_STATS_MAX_TASKS, &total);
    for (i = 0U; i < count; i++) {
        (void)strncpy(out[i].name, scratch[i].pcTaskName, sizeof(out[i].name) - 1U);
        out[i].name[sizeof(out[i].name) - 1U] = '\0';
        out[i].run_ticks                      = scratch[i].ulRunTimeCounter;
    }
    if (total_out != NULL) {
        *total_out = total;
    }
    return (size_t)count;
}

size_t runtime_stats_format(char *buf, size_t len)
{
    RuntimeStatsEntry entries[RUNTIME_STATS_MAX_TASKS];
    const uint64_t    ticks_per_ms = (uint64_t)dwt_cycles_per_us() * 1000U;
    uint64_t          total        = 0U;
    size_t            used         = 0U;
    size_t            count;
    size_t            i;

    if ((buf == NULL) || (len == 0U)) {
        return 0U;
    }
    buf[0] = '\0';

    count = runtime_stats_snapshot(entries, RUNTIME_STATS_MAX_TASKS, &total);
    if ((count == 0U) || (total == 0U)) {
        return 0U;
    }

    for (i = 0U; (i < count) && (used < (len - 1U)); i++) {
        /* Tenths of a percent, so there is one decimal place without floats. */
        const uint32_t permille = (uint32_t)((entries[i].run_ticks * 1000U) / total);
        const uint32_t run_ms   = (uint32_t)(entries[i].run_ticks / ticks_per_ms);
        const int      wrote    = snprintf(&buf[used], len - used, "%-*s %9lu ms %3lu.%lu%%\n",
                                           (int)(configMAX_TASK_NAME_LEN - 1), entries[i].name,
                                           (unsigned long)run_ms, (unsigned long)(permille / 10U),
                                           (unsigned long)(permille % 10U));

        if (wrote < 0) {
            break;
        }
        used += (size_t)wrote;
    }
    return (used < len) ? used : (len - 1U);
}
