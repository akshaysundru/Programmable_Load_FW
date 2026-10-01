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
#include "stm32g4xx_hal.h"

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
#define ENC1_PUSH_Pin GPIO_PIN_13
#define ENC1_PUSH_GPIO_Port GPIOC
#define ENC2_PUSH_Pin GPIO_PIN_14
#define ENC2_PUSH_GPIO_Port GPIOC
#define ENC1_A_Pin GPIO_PIN_0
#define ENC1_A_GPIO_Port GPIOA
#define ENC1_B_Pin GPIO_PIN_1
#define ENC1_B_GPIO_Port GPIOA
#define VSENSE_Pin GPIO_PIN_2
#define VSENSE_GPIO_Port GPIOA
#define ISENSE_Pin GPIO_PIN_3
#define ISENSE_GPIO_Port GPIOA
#define CONTROL_REF_Pin GPIO_PIN_4
#define CONTROL_REF_GPIO_Port GPIOA
#define FAULT_RESET_IN_Pin GPIO_PIN_5
#define FAULT_RESET_IN_GPIO_Port GPIOA
#define TEMP1_Pin GPIO_PIN_6
#define TEMP1_GPIO_Port GPIOA
#define TEMP2_Pin GPIO_PIN_7
#define TEMP2_GPIO_Port GPIOA
#define TEMP3_Pin GPIO_PIN_0
#define TEMP3_GPIO_Port GPIOB
#define TEMP4_Pin GPIO_PIN_1
#define TEMP4_GPIO_Port GPIOB
#define SD_CS_Pin GPIO_PIN_2
#define SD_CS_GPIO_Port GPIOB
#define LCD_RESET_Pin GPIO_PIN_10
#define LCD_RESET_GPIO_Port GPIOB
#define TEMP5_Pin GPIO_PIN_11
#define TEMP5_GPIO_Port GPIOB
#define TEMP6_Pin GPIO_PIN_12
#define TEMP6_GPIO_Port GPIOB
#define FAULT_ACTIVE_Pin GPIO_PIN_10
#define FAULT_ACTIVE_GPIO_Port GPIOA
#define LCD_DC_Pin GPIO_PIN_11
#define LCD_DC_GPIO_Port GPIOA
#define LCD_CS_Pin GPIO_PIN_12
#define LCD_CS_GPIO_Port GPIOA
#define SWD_IO_Pin GPIO_PIN_13
#define SWD_IO_GPIO_Port GPIOA
#define SW_CLK_Pin GPIO_PIN_14
#define SW_CLK_GPIO_Port GPIOA
#define BUTTON1_Pin GPIO_PIN_15
#define BUTTON1_GPIO_Port GPIOA
#define BUTTON2_Pin GPIO_PIN_3
#define BUTTON2_GPIO_Port GPIOB
#define BUTTON3_Pin GPIO_PIN_4
#define BUTTON3_GPIO_Port GPIOB
#define BUTTON4_Pin GPIO_PIN_5
#define BUTTON4_GPIO_Port GPIOB
#define BUTTON5_Pin GPIO_PIN_6
#define BUTTON5_GPIO_Port GPIOB
#define BUTTON6_Pin GPIO_PIN_7
#define BUTTON6_GPIO_Port GPIOB
#define ENC2_A_Pin GPIO_PIN_8
#define ENC2_A_GPIO_Port GPIOB
#define ENC2_B_Pin GPIO_PIN_9
#define ENC2_B_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
