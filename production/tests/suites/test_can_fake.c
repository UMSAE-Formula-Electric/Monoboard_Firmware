/* Exercises the CanIf contract (production/interfaces/can_if.h) through
 * can_fake, the dependency-free double for can_stm32 (ARCHITECTURE.md:
 * every driver has a programmable fake sibling so both stay warm).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_fake.h"
#include "test_framework.h"

static CanFrame make_frame(uint32_t id, uint8_t dlc)
{
    CanFrame frame = {0};
    uint8_t  i;

    frame.id  = id;
    frame.dlc = dlc;
    for (i = 0; i < dlc; i++)
    {
        frame.data[i] = (uint8_t) (id + i);
    }
    return frame;
}

static bool frames_equal(const CanFrame *a, const CanFrame *b)
{
    uint8_t i;

    if ((a->id != b->id) || (a->extended_id != b->extended_id) || (a->rtr != b->rtr) ||
        (a->dlc != b->dlc))
    {
        return false;
    }
    for (i = 0; i < a->dlc; i++)
    {
        if (a->data[i] != b->data[i])
        {
            return false;
        }
    }
    return true;
}

TEST(can_fake, send_before_init_fails)
{
    CanFrame frame = make_frame(0x100, 8);

    can_fake_reset();
    CHECK(can_fake.send(&frame) == CAN_ERR_HAL);
}

TEST(can_fake, init_then_send_is_captured)
{
    CanConfig config = {.bitrate_bps = 500000, .loopback = false};
    CanFrame  sent;
    CanFrame  captured;

    can_fake_reset();
    CHECK(can_fake.init(&config) == CAN_OK);

    sent = make_frame(0x123, 4);
    CHECK(can_fake.send(&sent) == CAN_OK);
    CHECK(can_fake_tx_count() == 1U);

    CHECK(can_fake_pop_tx(&captured) == true);
    CHECK(frames_equal(&captured, &sent));
    CHECK(can_fake_tx_count() == 0U);
}

TEST(can_fake, send_fails_when_tx_queue_full)
{
    CanConfig config = {.bitrate_bps = 500000, .loopback = false};
    CanFrame  frame  = make_frame(0x200, 1);
    size_t    i;

    can_fake_reset();
    CHECK(can_fake.init(&config) == CAN_OK);

    for (i = 0; i < CAN_FAKE_QUEUE_DEPTH; i++)
    {
        CHECK(can_fake.send(&frame) == CAN_OK);
    }
    CHECK(can_fake.send(&frame) == CAN_ERR_FULL);
}

TEST(can_fake, receive_without_data_times_out)
{
    CanConfig config = {.bitrate_bps = 500000, .loopback = false};
    CanFrame  frame;

    can_fake_reset();
    CHECK(can_fake.init(&config) == CAN_OK);
    CHECK(can_fake.receive(&frame, 0) == CAN_ERR_TIMEOUT);
}

TEST(can_fake, injected_rx_is_received)
{
    CanConfig config = {.bitrate_bps = 500000, .loopback = false};
    CanFrame  injected;
    CanFrame  received;

    can_fake_reset();
    CHECK(can_fake.init(&config) == CAN_OK);

    injected             = make_frame(0x321, 8);
    injected.extended_id = true;
    can_fake_inject_rx(&injected);

    CHECK(can_fake.receive(&received, 0) == CAN_OK);
    CHECK(frames_equal(&received, &injected));
    CHECK(can_fake.receive(&received, 0) == CAN_ERR_TIMEOUT);
}

TEST(can_fake, reset_clears_rx_tx_and_init_state)
{
    CanConfig config = {.bitrate_bps = 500000, .loopback = false};
    CanFrame  frame  = make_frame(0x1, 1);

    can_fake_reset();
    CHECK(can_fake.init(&config) == CAN_OK);
    can_fake_inject_rx(&frame);
    CHECK(can_fake.send(&frame) == CAN_OK);

    can_fake_reset();

    CHECK(can_fake_tx_count() == 0U);
    CHECK(can_fake.receive(&frame, 0) == CAN_ERR_TIMEOUT);
    CHECK(can_fake.send(&frame) == CAN_ERR_HAL); /* reset also un-initializes */
}
