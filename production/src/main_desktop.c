/**
 * @file main_desktop.c
 * @brief Desktop simulation composition root (see ARCHITECTURE.md).
 *
 * The host-side sibling of main_stm32.c: the same layers, wired to the
 * *_fake drivers instead of the *_stm32 ones, running on the FreeRTOS
 * POSIX port. It creates real tasks with real (static) allocation and
 * starts the real scheduler, so ordering, blocking and liveness bugs show
 * up on a laptop under gdb / ASan instead of on the car.
 *
 * Usage:
 *   monoboard_sim [--duration-ms N]
 *
 *   --duration-ms N  Run the scheduler for N ms of simulated (tick) time,
 *                    then end it and exit. 0 (the default) runs until
 *                    killed. CI uses a short duration as a smoke test.
 *
 * Exit status: 0 if the scheduler ran and every liveness probe saw CPU
 * time; 1 on a bad command line or a liveness failure.
 *
 * Output: human-readable lines on stdout, plus one machine-readable
 * "SIM_RESULT key=value ..." line at exit for scripts / CTest to match.
 *
 * KNOWN LIMITATION -- read before quoting any number from this program:
 * the POSIX port is NOT a timing model. Tasks are host threads, the tick is
 * a SIGALRM, and the host OS schedules everything underneath. The
 * simulation proves ordering, logic and liveness. It proves nothing about
 * WCET, jitter or interrupt latency.
 *
 * What is NOT here yet (seams marked below):
 *   - services: the service layer is still a stub (#32). Each service gets
 *     its init call + xTaskCreateStatic in sim_create_service_tasks(),
 *     injected with the fakes from sim_drivers.
 *   - priorities.h (#24): the two sim-only priorities below move there.
 *   - scenario scripting / trace output: drive the fakes' test-control
 *     surfaces (can_fake_inject_rx, adc_fake_push_sample, ...) from a
 *     stimulus task once there are services to observe.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "adc_fake.h"
#include "can_fake.h"
#include "gpio_fake.h"
#include "pwm_fake.h"
#include "usart_fake.h"
#include "wdg_fake.h"

/* -------------------------------------------------------------------------
 * Priorities and stacks. Sim-only tasks; move to priorities.h with #24.
 * ---------------------------------------------------------------------- */

/* Control task: highest application priority (below the timer daemon) so
 * a stuck application task can never keep it from ending the run. */
#define SIM_CTRL_PRIORITY ((UBaseType_t)(configMAX_PRIORITIES - 2U))

/* Liveness probe: lowest priority above idle. If it ever stops running,
 * something higher is hogging the CPU -- that is the failure it reports. */
#define SIM_PROBE_PRIORITY ((UBaseType_t)(tskIDLE_PRIORITY + 1U))

/* On the POSIX port each task runs on a host pthread with its own host
 * stack; this buffer only holds port bookkeeping, so the minimum is fine. */
#define SIM_STACK_WORDS ((configSTACK_DEPTH_TYPE)configMINIMAL_STACK_SIZE)

#define SIM_PROBE_PERIOD_MS 10U
#define SIM_CTRL_REPORT_MS  1000U

/* -------------------------------------------------------------------------
 * Driver selection: the one place the desktop build names concrete drivers.
 * ---------------------------------------------------------------------- */

typedef struct {
    const AdcIf  *adc;
    const CanIf  *can;
    const GpioIf *gpio;
    const PwmIf  *pwm;
    const UartIf *uart;
    const WdgIf  *wdg;
} SimDrivers;

static const SimDrivers sim_drivers = {
    .adc  = &adc_fake,
    .can  = &can_fake,
    .gpio = &gpio_fake,
    .pwm  = &pwm_fake,
    .uart = &usart_fake,
    .wdg  = &wdg_fake,
};

/* -------------------------------------------------------------------------
 * Run state. Written by tasks, read by main() only after the scheduler
 * has ended (vTaskEndScheduler hands control back to the main thread).
 * ---------------------------------------------------------------------- */

static uint32_t          sim_duration_ms;
static volatile uint32_t sim_probe_count;

static StaticTask_t sim_ctrl_tcb;
static StackType_t  sim_ctrl_stack[SIM_STACK_WORDS];
static StaticTask_t sim_probe_tcb;
static StackType_t  sim_probe_stack[SIM_STACK_WORDS];

/* -------------------------------------------------------------------------
 * Tasks
 * ---------------------------------------------------------------------- */

static void sim_probe_task(void *param)
{
    TickType_t wake = xTaskGetTickCount();

    (void)param;
    for (;;) {
        sim_probe_count = sim_probe_count + 1U;
        (void)xTaskDelayUntil(&wake, pdMS_TO_TICKS(SIM_PROBE_PERIOD_MS));
    }
}

static void sim_ctrl_task(void *param)
{
    TickType_t wake       = xTaskGetTickCount();
    uint32_t   elapsed_ms = 0U;

    (void)param;
    for (;;) {
        uint32_t step_ms = SIM_CTRL_REPORT_MS;

        if ((sim_duration_ms != 0U) && ((sim_duration_ms - elapsed_ms) < step_ms)) {
            step_ms = sim_duration_ms - elapsed_ms;
        }
        (void)xTaskDelayUntil(&wake, pdMS_TO_TICKS(step_ms));
        elapsed_ms += step_ms;

        (void)printf("[sim] t=%lu ms probe=%lu\n", (unsigned long)elapsed_ms,
                     (unsigned long)sim_probe_count);
        (void)fflush(stdout);

        if ((sim_duration_ms != 0U) && (elapsed_ms >= sim_duration_ms)) {
            vTaskEndScheduler(); /* returns control to main() */
        }
    }
}

/* -------------------------------------------------------------------------
 * Composition
 * ---------------------------------------------------------------------- */

/* Put every driver into its power-on state so a run is reproducible. */
static void sim_reset_drivers(void)
{
    adc_fake_reset();
    can_fake_reset();
    gpio_fake_reset();
    pwm_fake_reset();
    usart_fake_reset();
    wdg_fake_reset();
}

/* SEAM: service wiring. Each service lands here as
 *     xxx_init(&ctx, drivers->can, ...);
 *     xTaskCreateStatic(xxx_task, "xxx", XXX_STACK, &ctx, XXX_PRIO, ...);
 * with priorities/stacks from priorities.h. Nothing to wire yet (#32). */
static bool sim_create_service_tasks(const SimDrivers *drivers)
{
    (void)drivers;
    return true;
}

static bool sim_create_sim_tasks(void)
{
    TaskHandle_t ctrl  = xTaskCreateStatic(sim_ctrl_task, "sim_ctrl", SIM_STACK_WORDS, NULL,
                                           SIM_CTRL_PRIORITY, sim_ctrl_stack, &sim_ctrl_tcb);
    TaskHandle_t probe = xTaskCreateStatic(sim_probe_task, "sim_probe", SIM_STACK_WORDS, NULL,
                                           SIM_PROBE_PRIORITY, sim_probe_stack, &sim_probe_tcb);

    return (ctrl != NULL) && (probe != NULL);
}

static void sim_usage(const char *prog)
{
    (void)fprintf(stderr, "usage: %s [--duration-ms N]\n", prog);
}

/* Returns false on a malformed command line. */
static bool sim_parse_args(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--duration-ms") == 0) && ((i + 1) < argc)) {
            char         *end = NULL;
            unsigned long val;

            i++;
            val = strtoul(argv[i], &end, 10);
            if ((end == argv[i]) || (*end != '\0') || (argv[i][0] == '-') || (val > UINT32_MAX)) {
                return false;
            }
            sim_duration_ms = (uint32_t)val;
        } else {
            return false;
        }
    }
    return true;
}

/* -------------------------------------------------------------------------
 * FreeRTOS hooks required by FreeRTOSConfig.h (configCHECK_FOR_STACK_OVERFLOW).
 * Desktop-only and deliberately minimal; the shared hook set is #46.
 * ---------------------------------------------------------------------- */

/* Prototype comes from task.h; the non-const `name` is FreeRTOS's signature. */
/* cppcheck-suppress constParameterPointer */
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)fprintf(stderr, "[sim] FATAL: stack overflow in task '%s'\n", name);
    abort();
}

int main(int argc, char **argv)
{
    if (!sim_parse_args(argc, argv)) {
        sim_usage(argv[0]);
        return 1;
    }

    sim_reset_drivers();

    if (!sim_create_service_tasks(&sim_drivers) || !sim_create_sim_tasks()) {
        (void)fprintf(stderr, "[sim] FATAL: task creation failed\n");
        return 1;
    }

    (void)printf("[sim] starting scheduler (duration=%lu ms%s)\n", (unsigned long)sim_duration_ms,
                 (sim_duration_ms == 0U) ? ", run until killed" : "");
    (void)fflush(stdout);

    vTaskStartScheduler(); /* returns only after vTaskEndScheduler() */

    {
        const bool ok = (sim_probe_count > 0U);

        (void)printf("SIM_RESULT status=%s duration_ms=%lu probe=%lu\n", ok ? "pass" : "fail",
                     (unsigned long)sim_duration_ms, (unsigned long)sim_probe_count);
        return ok ? 0 : 1;
    }
}
