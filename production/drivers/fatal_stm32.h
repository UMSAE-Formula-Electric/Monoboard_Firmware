/**
 * @file fatal_stm32.h
 * @brief FatalIf for the STM32F446: breakpoint if a debugger is attached
 * (debug builds only), otherwise reset the MCU. Composition roots inject
 * this in place of `fatal_fake` on real hardware. Policy: see "Fatal
 * errors" in ARCHITECTURE.md.
 */
#ifndef FATAL_STM32_H
#define FATAL_STM32_H

#include "fatal_if.h"

extern const FatalIf fatal_stm32;

#endif /* FATAL_STM32_H */
