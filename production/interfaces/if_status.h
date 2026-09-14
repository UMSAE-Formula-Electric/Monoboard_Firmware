/**
 * @file if_status.h
 * @brief Status enum shared by every driver contract in production/interfaces/
 * (see issue #6, "Contracts"). Every contract entry point returns one of
 * these unless it is physically impossible to fail. Introduced with
 * uart_if.h; can_if.h and gpio_if.h originally predated this convention
 * and returned their own per-driver enum (CanStatus, GpioStatus) -- both
 * have since been migrated onto IfStatus, so every contract in this
 * directory is now consistent.
 *
 * Conventions shared by every contract in this directory (adc_if.h,
 * can_if.h, gpio_if.h, pwm_if.h, uart_if.h, wdg_if.h):
 *
 * - A contract is a `const` struct of function pointers (e.g. `AdcIf`,
 *   `GpioIf`). A driver, real or fake, exposes one instance:
 *   `extern const XxxIf xxx_stm32;` / `extern const XxxIf xxx_fake;`.
 *   `const` so the vtable lives in flash, not RAM.
 * - Every callback type's doc comment states its execution context
 *   explicitly, e.g. "invoked from ISR context -- FromISR APIs only, no
 *   blocking, minimal work." A callback with no such note runs in the
 *   caller's own context.
 * - Contract structs may only ever grow: new function pointers are
 *   appended at the end. Reordering or removing an existing entry is a
 *   breaking change to every driver and fake that initializes the struct
 *   positionally, and is forbidden.
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
