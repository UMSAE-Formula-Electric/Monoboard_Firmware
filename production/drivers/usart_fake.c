/**
 * @file usart_fake.c
 * @brief Programmable fake UART driver -- the uart_if test double.
 */
#include "usart_fake.h"

typedef struct {
    UartTxCompleteCb cb;
    void            *ctx;
    size_t           size;
} PendingCompletion;

static uint8_t tx_bytes[USART_FAKE_TX_BYTES_DEPTH];
static size_t  tx_bytes_head;
static size_t  tx_bytes_tail;
static size_t  tx_bytes_count;

static UsartFakeCall call_log[USART_FAKE_CALL_LOG_DEPTH];
static size_t        call_log_head;
static size_t        call_log_tail;
static size_t        call_log_count;

static PendingCompletion pending[USART_FAKE_PENDING_DEPTH];
static size_t            pending_head;
static size_t            pending_tail;
static size_t            pending_count;

static bool initialized;
static bool force_busy_next;
static bool force_hw_fault_next;

static IfStatus fake_init(const UartConfig *config)
{
    if (config == NULL || config->baud_rate_bps == 0U) {
        return IF_HW_FAULT;
    }
    usart_fake_reset();
    initialized = true;
    return IF_OK;
}

static IfStatus
fake_transmit(const uint8_t *data, size_t size, UartTxCompleteCb on_complete, void *ctx)
{
    size_t i;

    if (!initialized || data == NULL || size == 0U) {
        return IF_HW_FAULT;
    }
    if (force_busy_next) {
        force_busy_next = false;
        return IF_BUSY;
    }
    if (force_hw_fault_next) {
        force_hw_fault_next = false;
        return IF_HW_FAULT;
    }
    if (pending_count >= USART_FAKE_PENDING_DEPTH) {
        return IF_BUSY; /* the pending-buffer queue is genuinely full */
    }

    for (i = 0U; i < size && tx_bytes_count < USART_FAKE_TX_BYTES_DEPTH; i++) {
        tx_bytes[tx_bytes_head] = data[i];
        tx_bytes_head           = (tx_bytes_head + 1U) % USART_FAKE_TX_BYTES_DEPTH;
        tx_bytes_count++;
    }

    if (call_log_count < USART_FAKE_CALL_LOG_DEPTH) {
        call_log[call_log_head].size = size;
        call_log[call_log_head].ctx  = ctx;
        call_log_head                = (call_log_head + 1U) % USART_FAKE_CALL_LOG_DEPTH;
        call_log_count++;
    }

    pending[pending_head].cb   = on_complete;
    pending[pending_head].ctx  = ctx;
    pending[pending_head].size = size;
    pending_head               = (pending_head + 1U) % USART_FAKE_PENDING_DEPTH;
    pending_count++;

    return IF_OK;
}

const UartIf usart_fake = {
    .init     = fake_init,
    .transmit = fake_transmit,
};

void usart_fake_reset(void)
{
    tx_bytes_head       = 0U;
    tx_bytes_tail       = 0U;
    tx_bytes_count      = 0U;
    call_log_head       = 0U;
    call_log_tail       = 0U;
    call_log_count      = 0U;
    pending_head        = 0U;
    pending_tail        = 0U;
    pending_count       = 0U;
    initialized         = false;
    force_busy_next     = false;
    force_hw_fault_next = false;
}

void usart_fake_force_busy(void)
{
    force_busy_next = true;
}

void usart_fake_force_hw_fault(void)
{
    force_hw_fault_next = true;
}

bool usart_fake_complete_next_tx(void)
{
    PendingCompletion completed;

    if (pending_count == 0U) {
        return false;
    }
    completed    = pending[pending_tail];
    pending_tail = (pending_tail + 1U) % USART_FAKE_PENDING_DEPTH;
    pending_count--;

    if (completed.cb != NULL) {
        completed.cb(completed.ctx, completed.size);
    }
    return true;
}

size_t usart_fake_pending_tx_count(void)
{
    return pending_count;
}

bool usart_fake_pop_call(UsartFakeCall *out)
{
    if (call_log_count == 0U) {
        return false;
    }
    *out          = call_log[call_log_tail];
    call_log_tail = (call_log_tail + 1U) % USART_FAKE_CALL_LOG_DEPTH;
    call_log_count--;
    return true;
}

size_t usart_fake_call_count(void)
{
    return call_log_count;
}

size_t usart_fake_pop_tx(uint8_t *out, size_t max_size)
{
    size_t popped = 0U;

    while (popped < max_size && tx_bytes_count > 0U) {
        out[popped]   = tx_bytes[tx_bytes_tail];
        tx_bytes_tail = (tx_bytes_tail + 1U) % USART_FAKE_TX_BYTES_DEPTH;
        tx_bytes_count--;
        popped++;
    }
    return popped;
}

size_t usart_fake_tx_byte_count(void)
{
    return tx_bytes_count;
}
