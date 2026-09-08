#include "can_fake.h"

static CanFrame rx_queue[CAN_FAKE_QUEUE_DEPTH];
static size_t   rx_head;
static size_t   rx_tail;
static size_t   rx_count;

static CanFrame tx_queue[CAN_FAKE_QUEUE_DEPTH];
static size_t   tx_head;
static size_t   tx_tail;
static size_t   tx_count;

static bool initialized;

static CanStatus fake_init(const CanConfig *config)
{
    (void)config;
    can_fake_reset();
    initialized = true;
    return CAN_OK;
}

static CanStatus fake_send(const CanFrame *frame)
{
    if (!initialized) {
        return CAN_ERR_HAL;
    }
    if (tx_count >= CAN_FAKE_QUEUE_DEPTH) {
        return CAN_ERR_FULL;
    }
    tx_queue[tx_head] = *frame;
    tx_head = (tx_head + 1U) % CAN_FAKE_QUEUE_DEPTH;
    tx_count++;
    return CAN_OK;
}

static CanStatus fake_receive(CanFrame *frame, uint32_t timeout_ms)
{
    /* The fake is synchronous: there is no bus to wait on, so a
     * timeout can only ever mean "nothing was injected yet". */
    (void)timeout_ms;
    if (!initialized || rx_count == 0U) {
        return CAN_ERR_TIMEOUT;
    }
    *frame = rx_queue[rx_tail];
    rx_tail = (rx_tail + 1U) % CAN_FAKE_QUEUE_DEPTH;
    rx_count--;
    return CAN_OK;
}

const CanIf can_fake = {
    .init    = fake_init,
    .send    = fake_send,
    .receive = fake_receive,
};

void can_fake_reset(void)
{
    rx_head = 0U;
    rx_tail = 0U;
    rx_count = 0U;
    tx_head = 0U;
    tx_tail = 0U;
    tx_count = 0U;
    initialized = false;
}

void can_fake_inject_rx(const CanFrame *frame)
{
    if (rx_count >= CAN_FAKE_QUEUE_DEPTH) {
        return; /* test injected more than the fake can hold; drop */
    }
    rx_queue[rx_head] = *frame;
    rx_head = (rx_head + 1U) % CAN_FAKE_QUEUE_DEPTH;
    rx_count++;
}

bool can_fake_pop_tx(CanFrame *frame)
{
    if (tx_count == 0U) {
        return false;
    }
    *frame = tx_queue[tx_tail];
    tx_tail = (tx_tail + 1U) % CAN_FAKE_QUEUE_DEPTH;
    tx_count--;
    return true;
}

size_t can_fake_tx_count(void)
{
    return tx_count;
}
