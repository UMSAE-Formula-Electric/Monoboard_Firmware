/**
 * @file test_rtos_hooks.c
 * @brief FreeRTOS hooks (production/services/rtos_hooks.c) on the POSIX
 * port, with fatal_fake installed as the FatalIf.
 *
 * Every fatal path ends in abort(), and the scheduler cannot be restarted
 * once started, so those cases run in a forked child: the parent checks
 * that the child died of SIGABRT and that its stderr names the file and
 * line / task. That is also the proof that on the desktop a fault fails
 * the run rather than printing and carrying on.
 */
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "fatal_fake.h"
#include "rtos_hooks.h"
#include "test_framework.h"

#define CHILD_TIMEOUT_MS 5000L

typedef struct {
    bool exited;        /* child returned / _exit()ed: status is valid */
    int  status;        /* exit status when exited */
    bool signalled;     /* child was killed by a signal: signal is valid */
    int  signal;        /* the killing signal */
    char err_text[512]; /* everything the child wrote to stderr */
} ChildResult;

/* Run fn() in a forked child with fatal_fake installed (unless
 * install_fatal is false) and stderr captured. A child that has not
 * finished after timeout_ms is SIGKILLed -- that is how a hang shows up. */
static ChildResult run_in_child(void (*fn)(void), bool install_fatal, long timeout_ms)
{
    ChildResult result;
    int         fds[2];
    pid_t       pid;
    int         wstatus = 0;
    long        waited  = 0;
    size_t      used    = 0U;

    (void)memset(&result, 0, sizeof(result));
    if (pipe(fds) != 0) {
        return result;
    }
    (void)fflush(NULL);
    pid = fork();
    if (pid == 0) {
        /* The child is meant to abort(): no core dump (slow -- seconds
         * through systemd-coredump -- and clutter on a dev box). */
        (void)prctl(PR_SET_DUMPABLE, 0L, 0L, 0L, 0L);
        (void)dup2(fds[1], STDERR_FILENO);
        (void)close(fds[0]);
        (void)close(fds[1]);
        if (install_fatal) {
            rtos_hooks_install(&fatal_fake);
        }
        fn();
        _exit(0);
    }
    (void)close(fds[1]);
    if (pid < 0) {
        (void)close(fds[0]);
        return result;
    }

    while (waitpid(pid, &wstatus, WNOHANG) == 0) {
        const struct timespec tick = {.tv_sec = 0, .tv_nsec = 10L * 1000L * 1000L};

        if (waited >= timeout_ms) {
            (void)kill(pid, SIGKILL);
            (void)waitpid(pid, &wstatus, 0);
            break;
        }
        (void)nanosleep(&tick, NULL);
        waited += 10L;
    }

    for (;;) {
        ssize_t got = read(fds[0], &result.err_text[used], sizeof(result.err_text) - 1U - used);

        if ((got < 0) && (errno == EINTR)) {
            continue;
        }
        if (got <= 0) {
            break;
        }
        used += (size_t)got;
        if (used >= (sizeof(result.err_text) - 1U)) {
            break;
        }
    }
    result.err_text[used] = '\0';
    (void)close(fds[0]);

    result.exited    = WIFEXITED(wstatus);
    result.status    = result.exited ? WEXITSTATUS(wstatus) : 0;
    result.signalled = WIFSIGNALED(wstatus);
    result.signal    = result.signalled ? WTERMSIG(wstatus) : 0;
    return result;
}

static bool aborted_with(const ChildResult *result, const char *expected)
{
    return result->signalled && (result->signal == SIGABRT)
           && (strstr(result->err_text, expected) != NULL);
}

/* -- configASSERT ---------------------------------------------------------- */

/* trip_assert_line must name the configASSERT line below: 5 lines down. */
static const int trip_assert_line = __LINE__ + 5;
static void      trip_assert(void)
{
    volatile int zero = 0;

    configASSERT(zero != 0);
}

TEST(rtos_hooks, assert_aborts_with_file_and_line)
{
    char        expected[256];
    ChildResult result = run_in_child(trip_assert, true, CHILD_TIMEOUT_MS);

    (void)snprintf(expected, sizeof(expected), "configASSERT failed at %s:%d", __FILE__,
                   trip_assert_line);
    CHECK(aborted_with(&result, expected));
}

TEST(rtos_hooks, assert_without_fatal_handler_halts_rather_than_continues)
{
    /* No FatalIf installed: the fallback spins with interrupts off (the
     * watchdog's job on target), so the child must still be running --
     * never exit normally -- when the parent gives up on it. */
    ChildResult result = run_in_child(trip_assert, false, 200L);

    CHECK(!result.exited);
    CHECK(result.signalled && (result.signal == SIGKILL));
}

TEST(rtos_hooks, passing_assert_continues)
{
    volatile int one = 1;

    configASSERT(one == 1);
    CHECK(one == 1);
}

/* -- Stack overflow ------------------------------------------------------ */

#define OVERFLOW_STACK_DEPTH 512U

static StackType_t  overflow_stack[OVERFLOW_STACK_DEPTH];
static StaticTask_t overflow_tcb;

/* POSIX-port tasks execute on pthread stacks, not on the buffer handed to
 * xTaskCreateStatic(), so a real overflow cannot land in it. Instead the
 * task stamps over the low end of its buffer -- exactly the footprint an
 * overflow leaves on the MCU -- and yields, so the kernel's method-2
 * check runs on the switch-out and must name this task. */
static void overflow_task(void *params)
{
    (void)params;
    overflow_stack[0] = (StackType_t)0;
    overflow_stack[1] = (StackType_t)0;
    vTaskDelay(1);
    _exit(3); /* reached only if the overflow went unnoticed */
}

static void run_overflowing_task(void)
{
    (void)xTaskCreateStatic(overflow_task, "overflower", OVERFLOW_STACK_DEPTH, NULL,
                            tskIDLE_PRIORITY + 1U, overflow_stack, &overflow_tcb);
    vTaskStartScheduler();
    _exit(4); /* scheduler failed to start */
}

TEST(rtos_hooks, stack_overflow_detected_by_kernel_names_task)
{
    ChildResult result = run_in_child(run_overflowing_task, true, CHILD_TIMEOUT_MS);

    CHECK(aborted_with(&result, "stack overflow in task 'overflower'"));
}

static void call_overflow_hook_with_long_name(void)
{
    char name[] = "a_task_name_well_over_sixteen_chars";

    vApplicationStackOverflowHook(NULL, name);
}

TEST(rtos_hooks, stack_overflow_task_name_is_bounded)
{
    ChildResult result = run_in_child(call_overflow_hook_with_long_name, true, CHILD_TIMEOUT_MS);

    /* FAULT_TASK_NAME_LEN - 1 = 15 characters, then the terminator. */
    CHECK(aborted_with(&result, "stack overflow in task 'a_task_name_wel'"));
}

/* -- Malloc failed ------------------------------------------------------- */

static void call_malloc_failed_hook(void)
{
    vApplicationMallocFailedHook();
}

TEST(rtos_hooks, malloc_failed_aborts)
{
    ChildResult result = run_in_child(call_malloc_failed_hook, true, CHILD_TIMEOUT_MS);

    CHECK(aborted_with(&result, "malloc failed"));
}

/* -- Idle hook ----------------------------------------------------------- */

TEST(rtos_hooks, idle_hook_increments_idle_count)
{
    uint32_t before = rtos_hooks_idle_count();

    vApplicationIdleHook();
    vApplicationIdleHook();
    vApplicationIdleHook();
    CHECK((uint32_t)(rtos_hooks_idle_count() - before) == 3U);
}

#define PROBE_STACK_DEPTH 512U

static StackType_t  probe_stack[PROBE_STACK_DEPTH];
static StaticTask_t probe_tcb;

/* Sleeps so only the idle task is runnable, then reports via exit status
 * whether the kernel's idle task actually called the hook meanwhile. */
static void idle_probe_task(void *params)
{
    uint32_t before = rtos_hooks_idle_count();

    (void)params;
    vTaskDelay(20);
    _exit((rtos_hooks_idle_count() != before) ? 0 : 1);
}

static void run_idle_probe(void)
{
    (void)xTaskCreateStatic(idle_probe_task, "idle_probe", PROBE_STACK_DEPTH, NULL,
                            tskIDLE_PRIORITY + 1U, probe_stack, &probe_tcb);
    vTaskStartScheduler();
    _exit(4);
}

TEST(rtos_hooks, idle_task_runs_idle_hook)
{
    ChildResult result = run_in_child(run_idle_probe, true, CHILD_TIMEOUT_MS);

    CHECK(result.exited && (result.status == 0));
}

/* -- Static memory callbacks --------------------------------------------- */

TEST(rtos_hooks, idle_and_timer_task_memory_provided)
{
    StaticTask_t          *tcb   = NULL;
    StackType_t           *stack = NULL;
    configSTACK_DEPTH_TYPE depth = 0;

    vApplicationGetIdleTaskMemory(&tcb, &stack, &depth);
    CHECK(tcb != NULL);
    CHECK(stack != NULL);
    CHECK(depth == (configSTACK_DEPTH_TYPE)configMINIMAL_STACK_SIZE);

    tcb   = NULL;
    stack = NULL;
    depth = 0;
    vApplicationGetTimerTaskMemory(&tcb, &stack, &depth);
    CHECK(tcb != NULL);
    CHECK(stack != NULL);
    CHECK(depth == (configSTACK_DEPTH_TYPE)configTIMER_TASK_STACK_DEPTH);
}
