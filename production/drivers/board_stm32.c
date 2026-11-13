/**
 * @file board_stm32.c
 * @brief STM32F446 board bring-up (see board_stm32.h).
 */
#include "board_stm32.h"

#include "stm32f4xx_hal.h"

/* CubeMX-generated, in the vendor tree (stm32_hal): */
void                     SystemClock_Config(void); /* Core/Src/main.c */
extern TIM_HandleTypeDef htim6;                    /* Core/Src/stm32f4xx_hal_timebase_tim.c */

void TIM6_DAC_IRQHandler(void);

void board_stm32_init(void)
{
    /* HAL_Init() calls HAL_InitTick(), which the vendor timebase file
     * overrides to start TIM6 at 1 kHz. HAL_RCC_ClockConfig() inside
     * SystemClock_Config() calls it again, re-deriving TIM6's prescaler
     * from the final PCLK1. */
    (void)HAL_Init();
    SystemClock_Config();
}

/**
 * @brief TIM6 / DAC underrun vector. Only TIM6 (the HAL timebase) is in
 * use -- there is no DAC on this board. HAL_TIM_IRQHandler dispatches to
 * HAL_TIM_PeriodElapsedCallback in the CubeMX main.c, which calls
 * HAL_IncTick().
 *
 * This reference to htim6 is also what pulls stm32f4xx_hal_timebase_tim.o
 * out of the stm32_hal archive: its strong HAL_InitTick replaces the weak
 * SysTick one in stm32f4xx_hal.c only because the object gets linked.
 */
void TIM6_DAC_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim6);
}
