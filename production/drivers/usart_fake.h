/**
 * @file usart_fake.h
 * @brief Programmable fake UART driver: implements uart_if with no
 * hardware and no FreeRTOS dependency, so it builds and runs anywhere.
 * Composition roots wire `usart_fake` in exactly where `usart_stm32`
 * would go. Follows the fake driver conventions from issue #16: every
 * accepted transmit() is recorded (args + payload) in ring buffers a test
 * can inspect, completion is always deferred until the test fires it, and
 * every failure mode the real hardware can produce (busy, HW fault) can be
 * forced ahead of time. Zero `#ifdef TEST` -- this is ordinary code in
 * every build.
 */
#ifndef MONOBOARD_USART_FAKE_H
#define MONOBOARD_USART_FAKE_H

#include <stdbool.h>
#include <stddef.h>

#include "uart_if.h"

extern const UartIf usart_fake;

/* Capacity of the pending-completion queue, the call log, and the
 * captured-payload byte ring. A transmit() beyond these depths behaves
 * exactly as the real driver's pending-buffer queue would: IF_BUSY. */
#define USART_FAKE_PENDING_DEPTH  8U
#define USART_FAKE_CALL_LOG_DEPTH 16U
#define USART_FAKE_TX_BYTES_DEPTH 256U

typedef struct {
    size_t size;
    void  *ctx;
} UsartFakeCall;

/* Test-only control surface -- not part of the uart_if contract. */
void usart_fake_reset(void);

/* Fault injection: each applies to exactly the next transmit() call, then
 * clears itself, mirroring a one-shot hardware condition. */
void usart_fake_force_busy(void);
void usart_fake_force_hw_fault(void);

/* Completion control: every accepted transmit() queues its on_complete
 * rather than firing it inline, so a test can assert "queued but not yet
 * delivered" as a state distinct from "delivered" -- this is what lets a
 * test simulate a delayed completion. FIFO order, one callback per call. */
bool   usart_fake_complete_next_tx(void);
size_t usart_fake_pending_tx_count(void);

/* Call log: every transmit() call that returned IF_OK, FIFO, oldest first. */
bool   usart_fake_pop_call(UsartFakeCall *out);
size_t usart_fake_call_count(void);

/* Captured payload bytes from every accepted transmit(), concatenated FIFO. */
size_t usart_fake_pop_tx(uint8_t *out, size_t max_size);
size_t usart_fake_tx_byte_count(void);

#endif  // MONOBOARD_USART_FAKE_H
