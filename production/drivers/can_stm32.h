/**
 * @file can_stm32.h
 * @brief bxCAN driver: implements CanIf on top of ST HAL's CAN1 (HAL_CAN_*).
 * Wires CAN1 to PA11 (RX) / PA12 (TX), AF9 -- see can_stm32.c for the
 * MSP/GPIO/NVIC setup. Composition roots inject this in place of
 * `can_fake` to run on real hardware.
 */
#ifndef CAN_STM32_H
#define CAN_STM32_H

#include "can_if.h"

extern const CanIf can_stm32;

#endif /* CAN_STM32_H */
