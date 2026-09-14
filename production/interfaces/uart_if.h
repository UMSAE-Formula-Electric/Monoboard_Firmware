/**
 * @file uart_if.h
 * @brief UART contract shared by every UART driver (real or fake) -- see
 * issue #16. This is our wired debug/datalogging path, so transmit must be
 * genuinely non-blocking: it queues @c data for DMA and returns immediately,
 * never busy-waiting on the wire.
 *
 * RX is deliberately not part of this contract yet -- it is deferred until
 * the debug CLI needs it (issue #16), and when it lands it should be DMA +
 * idle-line detection, not byte-at-a-time interrupts.
 *
 * Header-only, standard-library types only -- see ARCHITECTURE.md,
 * Interface Layer rules. UARTs are named logically (UART_1); the concrete
 * driver owns the pin-to-pad and DMA-stream mapping for its board, exactly
 * as can_stm32 owns "CAN1 is PA11/PA12".
 */
#ifndef MONOBOARD_UART_IF_H
#define MONOBOARD_UART_IF_H

#include <stddef.h>
#include <stdint.h>

#include "if_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Prefixed UART_, not the HAL's USART_: ST's HAL headers (stm32f4xx_hal_
 * usart.h / stm32f4xx_hal_uart.h) #define USART_PARITY_NONE/EVEN/ODD as raw
 * register-bit constants. Colliding with a #define instead of an enum
 * shadows silently rather than erroring, so the names must not match. */
typedef enum {
    UART_PARITY_NONE = 0,
    UART_PARITY_EVEN,
    UART_PARITY_ODD,
} UartParity;

typedef enum {
    UART_STOP_BITS_1 = 0,
    UART_STOP_BITS_2,
} UartStopBits;

typedef struct {
    uint32_t     baud_rate_bps; /* e.g. 115200 */
    uint8_t      data_bits;     /* bits per frame, excluding parity; typically 8 */
    UartParity   parity;
    UartStopBits stop_bits;
} UartConfig;

/**
 * Fired when a transmit() buffer has been fully clocked out. Invoked from
 * ISR context -- FromISR APIs only, no blocking, minimal work. Fires
 * exactly once per accepted transmit() call, and only on success: a
 * transmit() that returns anything other than #IF_OK never gets a callback.
 * @param ctx                opaque pointer, exactly what was passed to transmit()
 * @param bytes_transmitted  always equal to the @p size passed to transmit()
 */
typedef void (*UartTxCompleteCb)(void *ctx, size_t bytes_transmitted);

typedef struct {
    /**
     * Bring the UART peripheral up and make it ready to transmit.
     * @param config  frame format and baud rate
     * @return #IF_OK on success, or #IF_HW_FAULT if @p config is invalid
     *         (NULL, zero baud rate, rate unreachable from the peripheral
     *         clock) or the underlying peripheral/HAL rejected the setup
     */
    IfStatus (*init)(const UartConfig *config);

    /**
     * Queue @p data for DMA transmission and return immediately -- never
     * blocks and never silently drops. The caller must not read, write, or
     * free @p data until @p on_complete fires for this exact call; ownership
     * only returns to the caller at that point. Buffers from multiple calls
     * queue up and are sent in order, one DMA transfer at a time.
     * @param      data         bytes to transmit; must stay valid and
     *                          unmodified until @p on_complete fires
     * @param      size         number of bytes in @p data
     * @param      on_complete  called from ISR context when @p data has
     *                          been fully sent; may be NULL to ignore
     * @param      ctx          opaque pointer passed back to @p on_complete
     * @return #IF_OK if queued, #IF_BUSY if the internal pending-buffer
     *         queue is full (the caller should retry, not drop), or
     *         #IF_HW_FAULT on bad arguments or a peripheral fault
     */
    IfStatus (*transmit)(const uint8_t *data, size_t size, UartTxCompleteCb on_complete, void *ctx);
} UartIf;

#ifdef __cplusplus
}
#endif

#endif  // MONOBOARD_UART_IF_H
