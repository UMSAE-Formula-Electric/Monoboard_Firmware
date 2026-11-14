/**
 * @file cli_service.h
 * @brief Debug CLI service (issue #51): moves bytes between the UART and
 * cli_core.
 *
 * A periodic service like the others in rtos_system.c: a context struct, an
 * init hook that brings up the UART it owns, and a step function. There is
 * one CLI, so the context only carries the wiring and the state lives in
 * cli_service.c.
 *
 * The receive callback runs in interrupt context and only copies bytes into
 * a stream buffer. The step takes them out, feeds cli_core and transmits the
 * reply. Nothing here blocks.
 *
 * Memory, all static: a 512-byte receive stream buffer, two 1024-byte
 * transmit buffers and one CliCore (about 0.8 KB). Timing: one step handles
 * at most one command line.
 */
#ifndef CLI_SERVICE_H
#define CLI_SERVICE_H

#include <stdbool.h>

#include "cli_core.h"
#include "if_status.h"
#include "uart_if.h"

#ifdef __cplusplus
extern "C" {
#endif

/** CLI service context. The composition root owns the storage and fills in
 *  every field before the task that runs cli_service_init() is created. */
typedef struct {
    const UartIf *uart;        /**< UART the CLI owns; must outlive it */
    UartConfig    uart_config; /**< frame format and baud rate for it */
    CliPorts      ports;       /**< what the CLI may reach */
} CliService;

/**
 * Init hook for rtos_system.c's PeriodicTask: bring up the UART, then start
 * the CLI on it. Runs once, in the CLI task, before the first step.
 * @param ctx  CliService with every field set
 * @return #IF_OK, the UART driver's error, cli_init()'s error, or
 *         #IF_HW_FAULT if @p ctx or its @c uart is NULL
 */
IfStatus cli_service_init(void *ctx);

/**
 * Set up the CLI and start receiving. Call once, before the first step and
 * after the UART driver's init(). cli_service_init() does both.
 * @param uart   UART to talk on; must outlive the CLI
 * @param ports  what the CLI may reach; @c is_safe_state is mandatory
 * @return #IF_OK, or #IF_HW_FAULT if @p uart, @p ports or
 *         @c ports->is_safe_state is NULL, or the receive stream buffer
 *         could not be created. A UART that cannot receive is not an error:
 *         the CLI then only prints (boot report and prompt).
 */
IfStatus cli_init(const UartIf *uart, const CliPorts *ports);

/**
 * Text printed once at start and by the `boot` command. May be called
 * before cli_init().
 * @param text  NUL-terminated text that stays valid forever, or NULL
 */
void cli_set_boot_report(const char *text);

/** One period of the CLI: run from the periodic task in rtos_system.c.
 *  @param ctx  unused: the CLI is a single instance (see cli_init()) */
void vCliServiceStep(void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* CLI_SERVICE_H */
