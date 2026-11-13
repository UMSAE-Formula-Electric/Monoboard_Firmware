/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, RTD_LED_OUT_Pin|MC_PWR_CTRL_Pin|PUMP_CTRL_Pin|SDC_CTRL_uC_Pin
                          |FANS_CTRL_Pin|LC_LED_OUT_Pin|TC_LED_OUT_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, AMS_CLR_ERR_uC_Pin|AIR_NEG_CTRL_uC_Pin|AIR_POS_CTRL_uC_Pin|PRECHRG_CTRL_uC_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(BUZR_CTRL_GPIO_Port, BUZR_CTRL_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : RTD_LED_OUT_Pin MC_PWR_CTRL_Pin PUMP_CTRL_Pin SDC_CTRL_uC_Pin
                           FANS_CTRL_Pin LC_LED_OUT_Pin TC_LED_OUT_Pin */
  GPIO_InitStruct.Pin = RTD_LED_OUT_Pin|MC_PWR_CTRL_Pin|PUMP_CTRL_Pin|SDC_CTRL_uC_Pin
                          |FANS_CTRL_Pin|LC_LED_OUT_Pin|TC_LED_OUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : BRAKE_PRESS_uC_Pin FANS_FB_uC_Pin BUZR_FB_uC_Pin PUMP_FB_uC_Pin */
  GPIO_InitStruct.Pin = BRAKE_PRESS_uC_Pin|FANS_FB_uC_Pin|BUZR_FB_uC_Pin|PUMP_FB_uC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : BSPD_FAULT_uC_Pin MC_PWR_FB_uC_Pin */
  GPIO_InitStruct.Pin = BSPD_FAULT_uC_Pin|MC_PWR_FB_uC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : TSA_BTN_uC_Pin RTD_BTN_uC_Pin LAUNCH_BTN_uC_Pin IMD_FAULT_uC_Pin
                           AMS_FAULT_uC_Pin AIR_NEG_FB_uC_Pin AIR_POS_FB_uC_Pin */
  GPIO_InitStruct.Pin = TSA_BTN_uC_Pin|RTD_BTN_uC_Pin|LAUNCH_BTN_uC_Pin|IMD_FAULT_uC_Pin
                          |AMS_FAULT_uC_Pin|AIR_NEG_FB_uC_Pin|AIR_POS_FB_uC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : AMS_STATE_uC_Pin IMD_STATE_uC_Pin BSPD_STATE_uC_Pin */
  GPIO_InitStruct.Pin = AMS_STATE_uC_Pin|IMD_STATE_uC_Pin|BSPD_STATE_uC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : AMS_CLR_ERR_uC_Pin AIR_NEG_CTRL_uC_Pin AIR_POS_CTRL_uC_Pin PRECHRG_CTRL_uC_Pin */
  GPIO_InitStruct.Pin = AMS_CLR_ERR_uC_Pin|AIR_NEG_CTRL_uC_Pin|AIR_POS_CTRL_uC_Pin|PRECHRG_CTRL_uC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : BUZR_CTRL_Pin */
  GPIO_InitStruct.Pin = BUZR_CTRL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BUZR_CTRL_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : TRACTION_BTN_uC_Pin SDC_STATUS_uC_Pin */
  GPIO_InitStruct.Pin = TRACTION_BTN_uC_Pin|SDC_STATUS_uC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
