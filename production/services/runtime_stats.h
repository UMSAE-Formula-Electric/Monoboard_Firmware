/**
 * @file runtime_stats.h
 * @brief Time base for FreeRTOS run-time stats, and a per-task CPU report
 * for Healthrun (#38).
 *
 * The time base is dwt_cycles() widened to 64 bits. The raw counter wraps
 * every ~24 s at 180 MHz, so FreeRTOSConfig.h points the kernel at
 * runtime_stats_counter(), which adds up 32-bit deltas. It is read on every
 * context switch, so a wrap is only missed if the scheduler stops for a
 * whole wrap period.
 *
 * Units are timebase ticks: core cycles on the STM32, nanoseconds on the
 * desktop. Convert with dwt_cycles_per_us(), or just compare ratios.
 *
 * vTaskGetRunTimeStats() is not usable here: it allocates with
 * pvPortMalloc(), which does not exist with dynamic allocation off, so it
 * prints nothing. runtime_stats_snapshot() and runtime_stats_format() do the
 * same job from static storage.
 */
#ifndef RUNTIME_STATS_H
#define RUNTIME_STATS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Most tasks a snapshot can hold (application tasks plus idle and timer). */
#define RUNTIME_STATS_MAX_TASKS 16U

/** A 32-bit counter that wraps, widened to 64 bits. Zero-initialise it. */
typedef struct {
    uint64_t total;  /**< ticks accumulated since the first update */
    uint32_t last;   /**< raw reading at the previous update */
    bool     primed; /**< false until the first update */
} RuntimeCounter;

/** One task's cumulative CPU time. */
typedef struct {
    char     name[configMAX_TASK_NAME_LEN];
    uint64_t run_ticks; /**< time spent running, in timebase ticks */
} RuntimeStatsEntry;

/**
 * Add a new raw reading to the 64-bit total. Valid as long as less than one
 * full wrap passes between calls. Not thread-safe; runtime_stats_counter()
 * does the locking.
 * @param c    counter state
 * @param raw  current raw reading
 * @return the new total (0 on the first call)
 */
uint64_t runtime_counter_update(RuntimeCounter *c, uint32_t raw);

/**
 * The run-time stats time base the kernel calls. Safe from a task or from
 * the context-switch path; never blocks.
 * @return 64-bit tick count, 0 at the first call
 */
/* FreeRTOSConfig.h declares it too, for tasks.c. Kept here for the docs and callers. */
/* NOLINTNEXTLINE(readability-redundant-declaration) */
uint64_t runtime_stats_counter(void);

/**
 * Copy every task's cumulative run time into @p out. Percentages over a
 * window are up to the caller: keep the last snapshot and subtract. Uses one
 * static scratch array, so only one task (Healthrun) may call it.
 * @param out        array to fill
 * @param max        capacity of @p out
 * @param total_out  receives the time base total at the snapshot; may be NULL
 * @return entries written, or 0 if the scheduler has not started, @p max is
 *         too small, or there are more than #RUNTIME_STATS_MAX_TASKS tasks
 */
size_t runtime_stats_snapshot(RuntimeStatsEntry *out, size_t max, uint64_t *total_out);

/**
 * Write a per-task table (name, run time in ms, percent) of the CPU use
 * since boot. Same single-caller rule as runtime_stats_snapshot().
 * @param buf  output; always NUL-terminated
 * @param len  size of @p buf
 * @return characters written, not counting the NUL
 */
size_t runtime_stats_format(char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_STATS_H */
