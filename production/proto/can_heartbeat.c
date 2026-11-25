/**
 * @file can_heartbeat.c
 * @brief CAN heartbeat frame codec. See can_heartbeat.h.
 */
#include "can_heartbeat.h"

#include <stddef.h>
#include <string.h>

void can_heartbeat_encode(uint32_t can_id, uint8_t sequence, uint32_t uptime_ms, CanFrame *frame)
{
    if (frame == NULL) {
        return;
    }
    (void)memset(frame, 0, sizeof(*frame));
    frame->id          = can_id;
    frame->extended_id = false;
    frame->rtr         = false;
    frame->dlc         = CAN_HEARTBEAT_DLC;
    frame->data[0]     = sequence;
    /* Little-endian, byte by byte, so the layout does not depend on the
     * target's own byte order. */
    frame->data[1] = (uint8_t)(uptime_ms & 0xFFU);
    frame->data[2] = (uint8_t)((uptime_ms >> 8U) & 0xFFU);
    frame->data[3] = (uint8_t)((uptime_ms >> 16U) & 0xFFU);
    frame->data[4] = (uint8_t)((uptime_ms >> 24U) & 0xFFU);
}

bool can_heartbeat_decode(const CanFrame *frame, uint8_t *sequence, uint32_t *uptime_ms)
{
    if (frame == NULL || frame->extended_id || frame->rtr || frame->dlc != CAN_HEARTBEAT_DLC) {
        return false;
    }
    if (sequence != NULL) {
        *sequence = frame->data[0];
    }
    if (uptime_ms != NULL) {
        *uptime_ms = (uint32_t)frame->data[1] | ((uint32_t)frame->data[2] << 8U)
                     | ((uint32_t)frame->data[3] << 16U) | ((uint32_t)frame->data[4] << 24U);
    }
    return true;
}
