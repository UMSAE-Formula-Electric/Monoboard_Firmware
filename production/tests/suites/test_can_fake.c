/**
 * @file test_can_fake.c
 * @brief Exercises the CanIf contract (production/interfaces/can_if.h) through
 * main_can_fake / sensor_can_fake, the dependency-free doubles for can_stm32 (ARCHITECTURE.md:
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
    for (i = 0; i < dlc; i++) {
        frame.data[i] = (uint8_t)(id + i);
    }
    return frame;
}

static bool frames_equal(const CanFrame *a, const CanFrame *b)
{
    uint8_t i;

    if ((a->id != b->id) || (a->extended_id != b->extended_id) || (a->rtr != b->rtr)
        || (a->dlc != b->dlc)) {
        return false;
    }
    for (i = 0; i < a->dlc; i++) {
        if (a->data[i] != b->data[i]) {
            return false;
        }
    }
    return true;
}

TEST(can_fake, send_before_init_fails)
{
    CanFrame frame = make_frame(0x100, 8);

    can_fake_reset(&main_can_fake);
    CHECK(main_can_fake.send(&frame) == IF_HW_FAULT);
}

TEST(can_fake, init_then_send_is_captured)
{
    CanConfig config = {.bitrate_bps = 500000, .loopback = false};
    CanFrame  sent;
    CanFrame  captured;

    can_fake_reset(&main_can_fake);
    CHECK(main_can_fake.init(&config) == IF_OK);

    sent = make_frame(0x123, 4);
    CHECK(main_can_fake.send(&sent) == IF_OK);
    CHECK(can_fake_tx_count(&main_can_fake) == 1U);

    CHECK(can_fake_pop_tx(&main_can_fake, &captured) == true);
    CHECK(frames_equal(&captured, &sent));
    CHECK(can_fake_tx_count(&main_can_fake) == 0U);
}

TEST(can_fake, send_fails_when_tx_queue_full)
{
    CanConfig config = {.bitrate_bps = 500000, .loopback = false};
    CanFrame  frame  = make_frame(0x200, 1);
    size_t    i;

    can_fake_reset(&main_can_fake);
    CHECK(main_can_fake.init(&config) == IF_OK);

    for (i = 0; i < CAN_FAKE_QUEUE_DEPTH; i++) {
        CHECK(main_can_fake.send(&frame) == IF_OK);
    }
    CHECK(main_can_fake.send(&frame) == IF_BUSY);
}

TEST(can_fake, receive_without_data_times_out)
{
    CanConfig config = {.bitrate_bps = 500000, .loopback = false};
    CanFrame  frame;

    can_fake_reset(&main_can_fake);
    CHECK(main_can_fake.init(&config) == IF_OK);
    CHECK(main_can_fake.receive(&frame, 0) == IF_TIMEOUT);
}

TEST(can_fake, injected_rx_is_received)
{
    CanConfig config = {.bitrate_bps = 500000, .loopback = false};
    CanFrame  injected;
    CanFrame  received;

    can_fake_reset(&main_can_fake);
    CHECK(main_can_fake.init(&config) == IF_OK);

    injected             = make_frame(0x321, 8);
    injected.extended_id = true;
    can_fake_inject_rx(&main_can_fake, &injected);

    CHECK(main_can_fake.receive(&received, 0) == IF_OK);
    CHECK(frames_equal(&received, &injected));
    CHECK(main_can_fake.receive(&received, 0) == IF_TIMEOUT);
}

TEST(can_fake, reset_clears_rx_tx_and_init_state)
{
    CanConfig config = {.bitrate_bps = 500000, .loopback = false};
    CanFrame  frame  = make_frame(0x1, 1);

    can_fake_reset(&main_can_fake);
    CHECK(main_can_fake.init(&config) == IF_OK);
    can_fake_inject_rx(&main_can_fake, &frame);
    CHECK(main_can_fake.send(&frame) == IF_OK);

    can_fake_reset(&main_can_fake);

    CHECK(can_fake_tx_count(&main_can_fake) == 0U);
    CHECK(main_can_fake.receive(&frame, 0) == IF_TIMEOUT);
    CHECK(main_can_fake.send(&frame) == IF_HW_FAULT); /* reset also un-initializes */
}

TEST(can_fake, buses_are_independent)
{
    CanConfig config       = {.bitrate_bps = 500000, .loopback = false};
    CanFrame  main_frame   = make_frame(0x100, 2);
    CanFrame  sensor_frame = make_frame(0x200, 3);
    CanFrame  received;

    can_fake_reset(&main_can_fake);
    can_fake_reset(&sensor_can_fake);

    /* Initialising one bus does not initialise the other. */
    CHECK(main_can_fake.init(&config) == IF_OK);
    CHECK(sensor_can_fake.send(&sensor_frame) == IF_HW_FAULT);
    CHECK(sensor_can_fake.init(&config) == IF_OK);

    /* TX is captured per bus. */
    CHECK(main_can_fake.send(&main_frame) == IF_OK);
    CHECK(can_fake_tx_count(&main_can_fake) == 1U);
    CHECK(can_fake_tx_count(&sensor_can_fake) == 0U);
    CHECK(sensor_can_fake.send(&sensor_frame) == IF_OK);
    CHECK(can_fake_pop_tx(&sensor_can_fake, &received) == true);
    CHECK(frames_equal(&received, &sensor_frame));
    CHECK(can_fake_pop_tx(&main_can_fake, &received) == true);
    CHECK(frames_equal(&received, &main_frame));

    /* RX is delivered only on the bus it was injected into. */
    can_fake_inject_rx(&sensor_can_fake, &sensor_frame);
    CHECK(main_can_fake.receive(&received, 0) == IF_TIMEOUT);
    CHECK(sensor_can_fake.receive(&received, 0) == IF_OK);
    CHECK(frames_equal(&received, &sensor_frame));

    /* Resetting one bus leaves the other initialised. */
    can_fake_reset(&sensor_can_fake);
    CHECK(sensor_can_fake.send(&sensor_frame) == IF_HW_FAULT);
    CHECK(main_can_fake.send(&main_frame) == IF_OK);
}

TEST(can_fake, control_surface_ignores_unknown_bus)
{
    CanFrame frame = make_frame(0x1, 1);

    can_fake_inject_rx(NULL, &frame);
    can_fake_reset(NULL);
    CHECK(can_fake_tx_count(NULL) == 0U);
    CHECK(can_fake_pop_tx(NULL, &frame) == false);
}
