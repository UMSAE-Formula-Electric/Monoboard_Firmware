/**
 * @file rtos_system.c
 * @brief The one place queues, tasks, priorities and stack sizes are set.
 *
 * Compiled into both the firmware and the desktop simulation, so a change
 * here reaches both. Keep it platform-neutral: no *_stm32.h, no *_fake.h,
 * no HAL. Drivers only arrive through RtosSystemDrivers; the desktop build
 * fails if a target-only header gets included here.
 *
 * Executable-specific tasks stay in their own main (the sim's control and
 * probe tasks).
 *
 * Adding a periodic service:
 *   1. Priority, period and stack: add XXX_* below.
 *   2. Storage: add a file-scope StaticTask_t and StackType_t array, the
 *      service's context struct, and a StaticQueue_t and item buffer for
 *      each queue it owns.
 *   3. Wiring: in rtos_system_create(), set the context's drivers (and
 *      queues) from @p drivers, then add a PeriodicTask below pointing at
 *      the context, and its xTaskCreateStatic() call. Create any queues at
 *      the top of that function, before the tasks using them. If the
 *      service needs its drivers brought up, give it an init hook returning
 *      IfStatus (see boot_service_init()): hooks run in the task, with the
 *      scheduler live, so HAL timeouts tick, and a failure asserts.
 */
#include "rtos_system.h"

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "boot_service.h"
#include "service_stubs.h"
#include "task.h"

/* -------------------------------------------------------------------------
 * Priorities and stack sizes: all of them, for schedulability review.
 * Highest application priority is configMAX_PRIORITIES - 2; the timer
 * daemon sits above it.
 *
 * Priorities follow the starting order in #24. Equal priorities time-slice.
 * Periods come from #19 (pedal) and #20 (motor controller); the rest, and
 * every stack size, are PLACEHOLDERS until #24 and #55 set measured values.
 *
 * Boot sits at the highest application priority, so its init hook runs
 * before any other service's first step, and its 500 ms heartbeat (a 1 Hz
 * square wave on PC0) is never delayed by them.
 * ---------------------------------------------------------------------- */

#define BOOT_PRIORITY       ((UBaseType_t)(configMAX_PRIORITIES - 2U))
#define CAN_ROUTER_PRIORITY ((UBaseType_t)(tskIDLE_PRIORITY + 5U))
#define PEDAL_PRIORITY      ((UBaseType_t)(tskIDLE_PRIORITY + 4U))
#define MC_PRIORITY         ((UBaseType_t)(tskIDLE_PRIORITY + 3U))
#define BMS_PRIORITY        ((UBaseType_t)(tskIDLE_PRIORITY + 2U))
#define IMD_PRIORITY        ((UBaseType_t)(tskIDLE_PRIORITY + 2U))
#define LOGGING_PRIORITY    ((UBaseType_t)(tskIDLE_PRIORITY + 1U))
#define HEALTHRUN_PRIORITY  ((UBaseType_t)(tskIDLE_PRIORITY + 1U))

#define CAN_ROUTER_PERIOD_MS 5U
#define PEDAL_PERIOD_MS      1U
#define MC_PERIOD_MS         15U
#define BMS_PERIOD_MS        100U
#define IMD_PERIOD_MS        100U
#define LOGGING_PERIOD_MS    10U
#define HEALTHRUN_PERIOD_MS  1000U
#define BOOT_PERIOD_MS       500U

#define CAN_ROUTER_STACK_WORDS 256U
#define PEDAL_STACK_WORDS      256U
#define MC_STACK_WORDS         256U
#define BMS_STACK_WORDS        256U
#define IMD_STACK_WORDS        256U
#define LOGGING_STACK_WORDS    256U
#define HEALTHRUN_STACK_WORDS  256U
#define BOOT_STACK_WORDS       256U

/* -------------------------------------------------------------------------
 * Static storage
 * ---------------------------------------------------------------------- */
static StaticTask_t can_router_tcb;
static StackType_t  can_router_stack[CAN_ROUTER_STACK_WORDS];

static StaticTask_t pedal_tcb;
static StackType_t  pedal_stack[PEDAL_STACK_WORDS];

static StaticTask_t mc_tcb;
static StackType_t  mc_stack[MC_STACK_WORDS];

static StaticTask_t bms_tcb;
static StackType_t  bms_stack[BMS_STACK_WORDS];

static StaticTask_t imd_tcb;
static StackType_t  imd_stack[IMD_STACK_WORDS];

static StaticTask_t logging_tcb;
static StackType_t  logging_stack[LOGGING_STACK_WORDS];

static StaticTask_t healthrun_tcb;
static StackType_t  healthrun_stack[HEALTHRUN_STACK_WORDS];

static StaticTask_t boot_tcb;
static StackType_t  boot_stack[BOOT_STACK_WORDS];

/* Service contexts. rtos_system_create() fills in each one's drivers (and,
 * later, its queues) before creating the task that uses it. */
static BootService boot_svc;

/* -------------------------------------------------------------------------
 * Wiring
 * ---------------------------------------------------------------------- */

/* A service's init and step take its context as void * so they slot in here
 * directly, with no adapter per service.
 *
 * SEAM: CAN and the UART get init hooks like the boot service's, in the
 * services that own them, once their bitrate and baud rate are confirmed;
 * PWM once it has a channel plan. The ADC needs none (its service calls
 * start_scan). */
typedef struct {
    void *ctx;                   /* the service's context, handed to init and step */
    IfStatus (*init)(void *ctx); /* once, in the task, before the first step; may be NULL */
    void (*step)(void *ctx);
    uint32_t period_ms;
} PeriodicTask;

/* Stub services take no context yet: ctx and init stay NULL. */
static const PeriodicTask can_router_task = {
    .step      = vCANRouterServiceStep,
    .period_ms = CAN_ROUTER_PERIOD_MS,
};
static const PeriodicTask pedal_task = {
    .step      = vPedalServiceStep,
    .period_ms = PEDAL_PERIOD_MS,
};
static const PeriodicTask mc_task = {
    .step      = vMotorcontrollerServiceStep,
    .period_ms = MC_PERIOD_MS,
};
static const PeriodicTask bms_task = {
    .step      = vBMSServiceStep,
    .period_ms = BMS_PERIOD_MS,
};
static const PeriodicTask imd_task = {
    .step      = vIMDServiceStep,
    .period_ms = IMD_PERIOD_MS,
};
static const PeriodicTask logging_task = {
    .step      = vLoggingServiceStep,
    .period_ms = LOGGING_PERIOD_MS,
};
static const PeriodicTask healthrun_task = {
    .step      = vHealthrunServiceStep,
    .period_ms = HEALTHRUN_PERIOD_MS,
};
static const PeriodicTask boot_task = {
    .ctx       = &boot_svc,
    .init      = boot_service_init,
    .step      = vBootServiceStep,
    .period_ms = BOOT_PERIOD_MS,
};

/* Body shared by every periodic service. vTaskDelayUntil keeps the period
 * fixed: the step's own run time does not stretch it. */
/* cppcheck-suppress constParameterCallback ; signature fixed by TaskFunction_t */
static void periodic_task(void *param)
{
    const PeriodicTask *task  = (const PeriodicTask *)param;
    const TickType_t    delay = pdMS_TO_TICKS(task->period_ms);
    TickType_t          last_wake;

    /* A failed init is a wiring bug, not a runtime condition: assert (and
     * reset the MCU) rather than run the service on a dead driver. */
    if (task->init != NULL) {
        const IfStatus status = task->init(task->ctx);

        configASSERT(status == IF_OK);
    }

    last_wake = xTaskGetTickCount();
    for (;;) {
        task->step(task->ctx);
        vTaskDelayUntil(&last_wake, delay);
    }
}

bool rtos_system_create(const RtosSystemDrivers *drivers)
{
    if (drivers == NULL) {
        return false;
    }

    /* Inject each service's drivers before its task can run. */
    boot_svc.gpio = drivers->gpio;

    if (xTaskCreateStatic(periodic_task, "boot", (configSTACK_DEPTH_TYPE)BOOT_STACK_WORDS,
                          (void *)&boot_task, BOOT_PRIORITY, boot_stack, &boot_tcb)
        == NULL) {
        return false;
    }

    if (xTaskCreateStatic(periodic_task, "can_router",
                          (configSTACK_DEPTH_TYPE)CAN_ROUTER_STACK_WORDS, (void *)&can_router_task,
                          CAN_ROUTER_PRIORITY, can_router_stack, &can_router_tcb)
        == NULL) {
        return false;
    }

    if (xTaskCreateStatic(periodic_task, "pedal", (configSTACK_DEPTH_TYPE)PEDAL_STACK_WORDS,
                          (void *)&pedal_task, PEDAL_PRIORITY, pedal_stack, &pedal_tcb)
        == NULL) {
        return false;
    }

    if (xTaskCreateStatic(periodic_task, "mc", (configSTACK_DEPTH_TYPE)MC_STACK_WORDS,
                          (void *)&mc_task, MC_PRIORITY, mc_stack, &mc_tcb)
        == NULL) {
        return false;
    }

    if (xTaskCreateStatic(periodic_task, "bms", (configSTACK_DEPTH_TYPE)BMS_STACK_WORDS,
                          (void *)&bms_task, BMS_PRIORITY, bms_stack, &bms_tcb)
        == NULL) {
        return false;
    }

    if (xTaskCreateStatic(periodic_task, "imd", (configSTACK_DEPTH_TYPE)IMD_STACK_WORDS,
                          (void *)&imd_task, IMD_PRIORITY, imd_stack, &imd_tcb)
        == NULL) {
        return false;
    }

    if (xTaskCreateStatic(periodic_task, "logging", (configSTACK_DEPTH_TYPE)LOGGING_STACK_WORDS,
                          (void *)&logging_task, LOGGING_PRIORITY, logging_stack, &logging_tcb)
        == NULL) {
        return false;
    }

    if (xTaskCreateStatic(periodic_task, "healthrun", (configSTACK_DEPTH_TYPE)HEALTHRUN_STACK_WORDS,
                          (void *)&healthrun_task, HEALTHRUN_PRIORITY, healthrun_stack,
                          &healthrun_tcb)
        == NULL) {
        return false;
    }

    return true;
}
