/**
 * @file can_fake.h
 * @brief Programmable fake CAN driver: implements CanIf with no hardware and
 * no FreeRTOS dependency, so it builds and runs anywhere. Composition
 * roots wire `main_can_fake` / `sensor_can_fake` in exactly where
 * `main_can_stm32` / `sensor_can_stm32` would go; tests additionally use
 * the functions below to act as "the bus".
 *
 * There is one fake per bus, each with its own independent state. The
 * control functions take the CanIf table of the fake to act on (e.g.
 * `can_fake_inject_rx(&sensor_can_fake, &frame)`); passing anything other
 * than one of the fakes below is a no-op (count 0, pop returns false).
 */
#ifndef CAN_FAKE_H
#define CAN_FAKE_H

#include <stddef.h>

#include "can_if.h"

extern const CanIf main_can_fake;   /* main bus */
extern const CanIf sensor_can_fake; /* sensor bus */

/* Capacity of both the RX-injection and TX-capture rings; a send/inject
 * beyond this depth is dropped/rejected. Exposed so tests can exercise
 * the "full" boundary without hardcoding the number. */
#define CAN_FAKE_QUEUE_DEPTH 16U

/* Test-only control surface -- not part of the CanIf contract. */
void   can_fake_reset(const CanIf *bus);
void   can_fake_inject_rx(const CanIf *bus, const CanFrame *frame);
bool   can_fake_pop_tx(const CanIf *bus, CanFrame *frame);
size_t can_fake_tx_count(const CanIf *bus);

#endif /* CAN_FAKE_H */
