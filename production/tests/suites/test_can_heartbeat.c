/**
 * @file test_can_heartbeat.c
 * @brief Exercises the CAN heartbeat codec (production/proto/can_heartbeat.h).
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "can_heartbeat.h"
#include "can_ids.h"
#include "test_framework.h"

TEST(can_heartbeat, encode_writes_the_documented_layout)
{
    CanFrame frame;

    (void)memset(&frame, 0xAA, sizeof(frame)); /* every field must be overwritten */
    can_heartbeat_encode(CAN_ID_SENS_HEARTBEAT, 0x42U, 0x11223344U, &frame);

    CHECK(frame.id == CAN_ID_SENS_HEARTBEAT);
    CHECK(!frame.extended_id);
    CHECK(!frame.rtr);
    CHECK(frame.dlc == CAN_HEARTBEAT_DLC);
    CHECK(frame.data[0] == 0x42U);
    CHECK(frame.data[1] == 0x44U); /* little-endian uptime */
    CHECK(frame.data[2] == 0x33U);
    CHECK(frame.data[3] == 0x22U);
    CHECK(frame.data[4] == 0x11U);
    CHECK(frame.data[5] == 0U && frame.data[6] == 0U && frame.data[7] == 0U);
}

TEST(can_heartbeat, decode_round_trips)
{
    CanFrame frame;
    uint8_t  sequence = 0U;
    uint32_t uptime   = 0U;

    can_heartbeat_encode(0x123U, 255U, UINT32_MAX, &frame);
    CHECK(can_heartbeat_decode(&frame, &sequence, &uptime));
    CHECK(sequence == 255U);
    CHECK(uptime == UINT32_MAX);

    /* Either output may be skipped. */
    CHECK(can_heartbeat_decode(&frame, NULL, NULL));
}

TEST(can_heartbeat, decode_rejects_other_frames)
{
    CanFrame frame;
    uint8_t  sequence = 7U;

    CHECK(!can_heartbeat_decode(NULL, &sequence, NULL));

    can_heartbeat_encode(0x123U, 1U, 2U, &frame);
    frame.dlc = 4U;
    CHECK(!can_heartbeat_decode(&frame, &sequence, NULL));

    can_heartbeat_encode(0x123U, 1U, 2U, &frame);
    frame.extended_id = true;
    CHECK(!can_heartbeat_decode(&frame, &sequence, NULL));

    can_heartbeat_encode(0x123U, 1U, 2U, &frame);
    frame.rtr = true;
    CHECK(!can_heartbeat_decode(&frame, &sequence, NULL));

    CHECK(sequence == 7U); /* untouched on failure */
}

TEST(can_heartbeat, encode_ignores_null_frame)
{
    can_heartbeat_encode(0x123U, 1U, 2U, NULL); /* must not crash */
    CHECK(true);
}
