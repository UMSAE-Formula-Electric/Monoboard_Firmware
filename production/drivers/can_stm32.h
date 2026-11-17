/**
 * @file can_stm32.h
 * @brief bxCAN driver: implements CanIf on top of ST HAL's CAN1 and CAN2
 * (HAL_CAN_*), one CanIf per bus:
 *   - main_can_stm32:   CAN1, PA11 (RX) / PA12 (TX), AF9
 *   - sensor_can_stm32: CAN2, PB5 (RX) / PB6 (TX), AF9
 * See can_stm32.c for the MSP/GPIO/NVIC setup and the CAN1/CAN2 shared
 * clock and filter-bank handling. Composition roots inject these in place
 * of `main_can_fake` / `sensor_can_fake` to run on real hardware.
 */
#ifndef CAN_STM32_H
#define CAN_STM32_H

#include "can_if.h"

extern const CanIf main_can_stm32;
extern const CanIf sensor_can_stm32;

#endif /* CAN_STM32_H */
