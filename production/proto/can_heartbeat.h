/**
 * @file can_heartbeat.h
 * @brief Encoder/decoder for the monoboard's CAN heartbeat frame.
 *
 * The heartbeat proves on a bench, with a CAN analyser, that the CAN
 * router's task really transmits: a frame that appears at a steady rate
 * with a sequence that counts up without gaps is the router TX path
 * working end to end. The router sends it (see CanRouterService::heartbeat);
 * this file only knows the layout.
 *
 * Layout (standard 11-bit ID, DLC 5, little-endian):
 *
 *     byte 0     sequence   rolling counter, +1 per frame, wraps 255 -> 0;
 *                           a gap in it on the bus log means a lost frame
 *     bytes 1-4  uptime_ms  sender's now_ms() when the frame was built
 *
 * Pure: no RTOS, no clock. The caller supplies the sequence and uptime.
 */
#ifndef CAN_HEARTBEAT_H
#define CAN_HEARTBEAT_H

#include <stdbool.h>
#include <stdint.h>

#include "can_if.h"

#ifdef __cplusplus
extern "C" {
#endif

/** DLC of a heartbeat frame: see the layout above. */
#define CAN_HEARTBEAT_DLC 5U

/**
 * Build a heartbeat frame.
 * @param can_id     standard 11-bit ID to send it on (e.g. #CAN_ID_SENS_HEARTBEAT)
 * @param sequence   byte 0
 * @param uptime_ms  bytes 1-4
 * @param frame      [out] the frame; every field is written
 */
void can_heartbeat_encode(uint32_t can_id, uint8_t sequence, uint32_t uptime_ms, CanFrame *frame);

/**
 * Read a heartbeat frame back (bench tools, tests).
 * @param frame      frame to read
 * @param sequence   [out] byte 0; may be NULL
 * @param uptime_ms  [out] bytes 1-4; may be NULL
 * @return true if @p frame is a standard-ID data frame with DLC
 *         #CAN_HEARTBEAT_DLC (the ID is not checked); the outputs are only
 *         written on true
 */
bool can_heartbeat_decode(const CanFrame *frame, uint8_t *sequence, uint32_t *uptime_ms);

#ifdef __cplusplus
}
#endif

#endif /* CAN_HEARTBEAT_H */
