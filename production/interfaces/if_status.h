/**
 * @file if_status.h
 * @brief Status enum shared by every driver contract in production/interfaces/
 * (see issue #6, "Contracts"). Every contract entry point returns one of
 * these unless it is physically impossible to fail. Introduced with
 * uart_if.h; can_if.h/gpio_if.h predate this convention and still return
 * their own per-driver enum -- migrating them is issue #6's job, not this
 * one's.
 */
#ifndef MONOBOARD_IF_STATUS_H
#define MONOBOARD_IF_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IF_OK = 0,
    IF_BUSY,     /* call rejected, nothing started: try again later */
    IF_TIMEOUT,  /* no data/event arrived within the caller's deadline */
    IF_HW_FAULT, /* peripheral/HAL reported a failure, or bad arguments */
} IfStatus;

#ifdef __cplusplus
}
#endif

#endif  // MONOBOARD_IF_STATUS_H
