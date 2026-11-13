/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define RTD_LED_OUT_Pin GPIO_PIN_4
#define RTD_LED_OUT_GPIO_Port GPIOE
#define DEBUG_LED_R2_Pin GPIO_PIN_6
#define DEBUG_LED_R2_GPIO_Port GPIOE
#define APPS_1_uC_Pin GPIO_PIN_0
#define APPS_1_uC_GPIO_Port GPIOC
#define COOL_SYS_TEMP3_uC_Pin GPIO_PIN_1
#define COOL_SYS_TEMP3_uC_GPIO_Port GPIOC
#define BRAKE_PRESS_uC_Pin GPIO_PIN_2
#define BRAKE_PRESS_uC_GPIO_Port GPIOC
#define EXTRA_INPUT2_uC_Pin GPIO_PIN_3
#define EXTRA_INPUT2_uC_GPIO_Port GPIOC
#define COOL_SYS_TEMP1_uC_Pin GPIO_PIN_0
#define COOL_SYS_TEMP1_uC_GPIO_Port GPIOA
#define COOL_SYS_TEMP2_uC_Pin GPIO_PIN_1
#define COOL_SYS_TEMP2_uC_GPIO_Port GPIOA
#define SERIAL_TX_Pin GPIO_PIN_2
#define SERIAL_TX_GPIO_Port GPIOA
#define SERIAL_RX_Pin GPIO_PIN_3
#define SERIAL_RX_GPIO_Port GPIOA
#define BSPD_FAULT_uC_Pin GPIO_PIN_4
#define BSPD_FAULT_uC_GPIO_Port GPIOA
#define APPS_2_uC_Pin GPIO_PIN_5
#define APPS_2_uC_GPIO_Port GPIOA
#define MC_PWR_FB_uC_Pin GPIO_PIN_6
#define MC_PWR_FB_uC_GPIO_Port GPIOA
#define EXTRA_INPUT1_uC_Pin GPIO_PIN_7
#define EXTRA_INPUT1_uC_GPIO_Port GPIOA
#define HCP_H_CURR_SENS_uC_Pin GPIO_PIN_4
#define HCP_H_CURR_SENS_uC_GPIO_Port GPIOC
#define HCP_L_CURR_SENS_uC_Pin GPIO_PIN_5
#define HCP_L_CURR_SENS_uC_GPIO_Port GPIOC
#define TSA_BTN_uC_Pin GPIO_PIN_0
#define TSA_BTN_uC_GPIO_Port GPIOB
#define RTD_BTN_uC_Pin GPIO_PIN_1
#define RTD_BTN_uC_GPIO_Port GPIOB
#define LAUNCH_BTN_uC_Pin GPIO_PIN_2
#define LAUNCH_BTN_uC_GPIO_Port GPIOB
#define MC_PWR_CTRL_Pin GPIO_PIN_7
#define MC_PWR_CTRL_GPIO_Port GPIOE
#define AMS_STATE_uC_Pin GPIO_PIN_8
#define AMS_STATE_uC_GPIO_Port GPIOE
#define IMD_STATE_uC_Pin GPIO_PIN_9
#define IMD_STATE_uC_GPIO_Port GPIOE
#define BSPD_STATE_uC_Pin GPIO_PIN_10
#define BSPD_STATE_uC_GPIO_Port GPIOE
#define PUMP_CTRL_Pin GPIO_PIN_11
#define PUMP_CTRL_GPIO_Port GPIOE
#define SDC_CTRL_uC_Pin GPIO_PIN_12
#define SDC_CTRL_uC_GPIO_Port GPIOE
#define FANS_CTRL_Pin GPIO_PIN_13
#define FANS_CTRL_GPIO_Port GPIOE
#define LC_LED_OUT_Pin GPIO_PIN_14
#define LC_LED_OUT_GPIO_Port GPIOE
#define TC_LED_OUT_Pin GPIO_PIN_15
#define TC_LED_OUT_GPIO_Port GPIOE
#define DEBUG_LED_G_Pin GPIO_PIN_10
#define DEBUG_LED_G_GPIO_Port GPIOB
#define IMD_FAULT_uC_Pin GPIO_PIN_12
#define IMD_FAULT_uC_GPIO_Port GPIOB
#define AMS_FAULT_uC_Pin GPIO_PIN_13
#define AMS_FAULT_uC_GPIO_Port GPIOB
#define AIR_NEG_FB_uC_Pin GPIO_PIN_14
#define AIR_NEG_FB_uC_GPIO_Port GPIOB
#define AIR_POS_FB_uC_Pin GPIO_PIN_15
#define AIR_POS_FB_uC_GPIO_Port GPIOB
#define AMS_CLR_ERR_uC_Pin GPIO_PIN_8
#define AMS_CLR_ERR_uC_GPIO_Port GPIOD
#define AIR_NEG_CTRL_uC_Pin GPIO_PIN_9
#define AIR_NEG_CTRL_uC_GPIO_Port GPIOD
#define AIR_POS_CTRL_uC_Pin GPIO_PIN_10
#define AIR_POS_CTRL_uC_GPIO_Port GPIOD
#define PRECHRG_CTRL_uC_Pin GPIO_PIN_13
#define PRECHRG_CTRL_uC_GPIO_Port GPIOD
#define FANS_FB_uC_Pin GPIO_PIN_7
#define FANS_FB_uC_GPIO_Port GPIOC
#define BUZR_FB_uC_Pin GPIO_PIN_8
#define BUZR_FB_uC_GPIO_Port GPIOC
#define PUMP_FB_uC_Pin GPIO_PIN_9
#define PUMP_FB_uC_GPIO_Port GPIOC
#define BUZR_CTRL_Pin GPIO_PIN_8
#define BUZR_CTRL_GPIO_Port GPIOA
#define MAIN_CAN_RX_Pin GPIO_PIN_11
#define MAIN_CAN_RX_GPIO_Port GPIOA
#define MAIN_CAN_TX_Pin GPIO_PIN_12
#define MAIN_CAN_TX_GPIO_Port GPIOA
#define TRACTION_BTN_uC_Pin GPIO_PIN_3
#define TRACTION_BTN_uC_GPIO_Port GPIOD
#define SDC_STATUS_uC_Pin GPIO_PIN_6
#define SDC_STATUS_uC_GPIO_Port GPIOD
#define DEBUG_LED_B_Pin GPIO_PIN_4
#define DEBUG_LED_B_GPIO_Port GPIOB
#define SENS_CAN_RX_Pin GPIO_PIN_5
#define SENS_CAN_RX_GPIO_Port GPIOB
#define SENS_CAN_TX_Pin GPIO_PIN_6
#define SENS_CAN_TX_GPIO_Port GPIOB
#define IMD_RESIST_uC_Pin GPIO_PIN_7
#define IMD_RESIST_uC_GPIO_Port GPIOB
#define DEBUG_LED_B2_Pin GPIO_PIN_8
#define DEBUG_LED_B2_GPIO_Port GPIOB
#define DEBUG_LED_G2_Pin GPIO_PIN_9
#define DEBUG_LED_G2_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
