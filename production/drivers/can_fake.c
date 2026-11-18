/**
 * @file can_fake.c
 * @brief Programmable fake CAN driver -- the CanIf test double.
 */
#include "can_fake.h"

typedef struct {
    CanFrame rx_queue[CAN_FAKE_QUEUE_DEPTH];
    size_t   rx_head;
    size_t   rx_tail;
    size_t   rx_count;

    CanFrame tx_queue[CAN_FAKE_QUEUE_DEPTH];
    size_t   tx_head;
    size_t   tx_tail;
    size_t   tx_count;

    bool initialized;
} CanFakeBus;

static CanFakeBus main_bus;
static CanFakeBus sensor_bus;

static CanFakeBus *bus_from_if(const CanIf *bus)
{
    if (bus == &main_can_fake) {
        return &main_bus;
    }
    if (bus == &sensor_can_fake) {
        return &sensor_bus;
    }
    return NULL;
}

static void bus_reset(CanFakeBus *bus)
{
    bus->rx_head     = 0U;
    bus->rx_tail     = 0U;
    bus->rx_count    = 0U;
    bus->tx_head     = 0U;
    bus->tx_tail     = 0U;
    bus->tx_count    = 0U;
    bus->initialized = false;
}

static IfStatus bus_init(CanFakeBus *bus, const CanConfig *config)
{
    (void)config;
    bus_reset(bus);
    bus->initialized = true;
    return IF_OK;
}

static IfStatus bus_send(CanFakeBus *bus, const CanFrame *frame)
{
    if (!bus->initialized) {
        return IF_HW_FAULT;
    }
    if (bus->tx_count >= CAN_FAKE_QUEUE_DEPTH) {
        return IF_BUSY;
    }
    bus->tx_queue[bus->tx_head] = *frame;
    bus->tx_head                = (bus->tx_head + 1U) % CAN_FAKE_QUEUE_DEPTH;
    bus->tx_count++;
    return IF_OK;
}

static IfStatus bus_receive(CanFakeBus *bus, CanFrame *frame, uint32_t timeout_ms)
{
    /* The fake is synchronous: there is no bus to wait on, so a
     * timeout can only ever mean "nothing was injected yet". */
    (void)timeout_ms;
    if (!bus->initialized || bus->rx_count == 0U) {
        return IF_TIMEOUT;
    }
    *frame       = bus->rx_queue[bus->rx_tail];
    bus->rx_tail = (bus->rx_tail + 1U) % CAN_FAKE_QUEUE_DEPTH;
    bus->rx_count--;
    return IF_OK;
}

/* --- main bus --- */

static IfStatus main_init(const CanConfig *config)
{
    return bus_init(&main_bus, config);
}

static IfStatus main_send(const CanFrame *frame)
{
    return bus_send(&main_bus, frame);
}

static IfStatus main_receive(CanFrame *frame, uint32_t timeout_ms)
{
    return bus_receive(&main_bus, frame, timeout_ms);
}

const CanIf main_can_fake = {
    .init    = main_init,
    .send    = main_send,
    .receive = main_receive,
};

/* --- sensor bus --- */

static IfStatus sensor_init(const CanConfig *config)
{
    return bus_init(&sensor_bus, config);
}

static IfStatus sensor_send(const CanFrame *frame)
{
    return bus_send(&sensor_bus, frame);
}

static IfStatus sensor_receive(CanFrame *frame, uint32_t timeout_ms)
{
    return bus_receive(&sensor_bus, frame, timeout_ms);
}

const CanIf sensor_can_fake = {
    .init    = sensor_init,
    .send    = sensor_send,
    .receive = sensor_receive,
};

/* --- test control surface --- */

void can_fake_reset(const CanIf *bus)
{
    CanFakeBus *state = bus_from_if(bus);

    if (state != NULL) {
        bus_reset(state);
    }
}

void can_fake_inject_rx(const CanIf *bus, const CanFrame *frame)
{
    CanFakeBus *state = bus_from_if(bus);

    if (state == NULL || state->rx_count >= CAN_FAKE_QUEUE_DEPTH) {
        return; /* unknown bus, or test injected more than the fake can hold; drop */
    }
    state->rx_queue[state->rx_head] = *frame;
    state->rx_head                  = (state->rx_head + 1U) % CAN_FAKE_QUEUE_DEPTH;
    state->rx_count++;
}

bool can_fake_pop_tx(const CanIf *bus, CanFrame *frame)
{
    CanFakeBus *state = bus_from_if(bus);

    if (state == NULL || state->tx_count == 0U) {
        return false;
    }
    *frame         = state->tx_queue[state->tx_tail];
    state->tx_tail = (state->tx_tail + 1U) % CAN_FAKE_QUEUE_DEPTH;
    state->tx_count--;
    return true;
}

size_t can_fake_tx_count(const CanIf *bus)
{
    const CanFakeBus *state = bus_from_if(bus);

    return (state != NULL) ? state->tx_count : 0U;
}
