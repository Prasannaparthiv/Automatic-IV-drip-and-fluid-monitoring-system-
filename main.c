/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : IV Drip Monitor - HX711 + Relay + LED
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "stdio.h"
#include "usart.h"
#include <stdlib.h>

/* Calibration factor */
static int32_t calibration_factor = 214;

/* Redirect printf to UART */
int _write(int file, char *ptr, int len) {
    HAL_UART_Transmit(&huart2, (uint8_t*)ptr, len, HAL_MAX_DELAY);
    return len;
}

/* HX711 small delay */
static void HX711_delay(void) {
    for (volatile int i = 0; i < 100; i++);
}

/* HX711 read raw */
int32_t HX711_ReadRaw(void) {
    uint32_t count = 0;
    while (HAL_GPIO_ReadPin(HX711_DOUT_GPIO_Port, HX711_DOUT_Pin) == GPIO_PIN_SET) { }

    for (int i = 0; i < 24; i++) {
        HAL_GPIO_WritePin(HX711_SCK_GPIO_Port, HX711_SCK_Pin, GPIO_PIN_SET);
        HX711_delay();
        count = (count << 1);
        HAL_GPIO_WritePin(HX711_SCK_GPIO_Port, HX711_SCK_Pin, GPIO_PIN_RESET);
        HX711_delay();
        if (HAL_GPIO_ReadPin(HX711_DOUT_GPIO_Port, HX711_DOUT_Pin) == GPIO_PIN_SET) {
            count |= 1;
        }
    }

    HAL_GPIO_WritePin(HX711_SCK_GPIO_Port, HX711_SCK_Pin, GPIO_PIN_SET);
    HX711_delay();
    HAL_GPIO_WritePin(HX711_SCK_GPIO_Port, HX711_SCK_Pin, GPIO_PIN_RESET);
    HX711_delay();

    if (count & 0x800000) { count |= 0xFF000000; }
    return (int32_t)count;
}

/* raw → centigrams */
int32_t raw_to_centigrams(int32_t raw, int32_t offset, int32_t calib) {
    int64_t diff = (int64_t)raw - (int64_t)offset;
    int64_t cg = (diff * 100) / (calib == 0 ? 1 : calib);
    if (cg > INT32_MAX) return INT32_MAX;
    if (cg < INT32_MIN) return INT32_MIN;
    return (int32_t)cg;
}

/* Error handler */
void Error_Handler(void) {
  __disable_irq();
  while (1) {}
}

/* Clock config */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { Error_Handler(); }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) { Error_Handler(); }
}

/* GPIO Init */
static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* Relay on PA5 */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* LED */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /* HX711 SCK */
  HAL_GPIO_WritePin(HX711_SCK_GPIO_Port, HX711_SCK_Pin, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = HX711_SCK_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(HX711_SCK_GPIO_Port, &GPIO_InitStruct);

  /* HX711 DOUT */
  GPIO_InitStruct.Pin = HX711_DOUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(HX711_DOUT_GPIO_Port, &GPIO_InitStruct);
}

int main(void) {
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_USART2_UART_Init();

  printf("=== IV Drip Monitor (Relay PA5) ===\r\n");

  HAL_Delay(200);
  int32_t offset_raw = HX711_ReadRaw();
  printf("Tare offset = %ld\r\n", (long)offset_raw);

  int32_t initial_raw = HX711_ReadRaw();
  int32_t initial_cg = raw_to_centigrams(initial_raw, offset_raw, calibration_factor);
  printf("Initial weight = %ld.%02ld g\r\n",
         (long)(initial_cg/100), (long)abs(initial_cg % 100));

  while (1) {
      int32_t raw = HX711_ReadRaw();
      int32_t cg = raw_to_centigrams(raw, offset_raw, calibration_factor);

      printf("Weight: %ld.%02ld g\r\n", (long)(cg/100), (long)abs(cg%100));

      if (cg <= 3500) {
          HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET); // Relay OFF
          HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
          printf("Solenoid OFF (<20g)\r\n");
      }
      if(cg >= 3501){
          HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);   // Relay ON
          HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
          printf("Solenoid ON (>20g)\r\n");
      }

      HAL_Delay(500);
  }
}
