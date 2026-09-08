/**
 * @file can_fake.h
 * @brief Programmable fake CAN driver: implements CanIf with no hardware and
 * no FreeRTOS dependency, so it builds and runs anywhere. Composition
 * roots wire `can_fake` in exactly where `can_stm32` would go; tests
 * additionally use the functions below to act as "the bus".
 */
#ifndef CAN_FAKE_H
#define CAN_FAKE_H

#include <stddef.h>

#include "can_if.h"

extern const CanIf can_fake;

/* Capacity of both the RX-injection and TX-capture rings; a send/inject
 * beyond this depth is dropped/rejected. Exposed so tests can exercise
 * the "full" boundary without hardcoding the number. */
#define CAN_FAKE_QUEUE_DEPTH 16U

/* Test-only control surface -- not part of the CanIf contract. */
void   can_fake_reset(void);
void   can_fake_inject_rx(const CanFrame *frame);
bool   can_fake_pop_tx(CanFrame *frame);
size_t can_fake_tx_count(void);

#endif /* CAN_FAKE_H */
