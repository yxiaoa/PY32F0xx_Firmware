/**
 ******************************************************************************
 * @file    main.c
 * @author  MCU Application Team
 * @brief   Main program body
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2023 Puya Semiconductor Co.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed by Puya under BSD 3-Clause license,
 * the "License"; You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                        opensource.org/licenses/BSD-3-Clause
 *
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2016 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed by ST under BSD 3-Clause license,
 * the "License"; You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                        opensource.org/licenses/BSD-3-Clause
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "target_config.h"

/* Private define ------------------------------------------------------------*/

#define RX_BUF_SIZE (256)

typedef enum {
	UART_STATE_IDLE,
	UART_STATE_BUSY_TX,
	UART_STATE_BUSY_RX,
	UART_STATE_RX_COMPLETE,
	UART_STATE_ERROR
} uart_state_t;

typedef struct {
	uart_state_t state;
	uint8_t rx_cnt;
	uint8_t *rx_buf;
	UART_HandleTypeDef *handle;
} uart_ctx_t;

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef uart_upper_handle;  /* Used for communication with PC */
UART_HandleTypeDef uart_module_handle; /* Used for communication with Airtouch module */
static uint8_t uart_upper_rx_buf[RX_BUF_SIZE];
static uint8_t uart_module_rx_buf[RX_BUF_SIZE];
static volatile uart_ctx_t uart_upper;
static volatile uart_ctx_t uart_module;
static volatile uint8_t m_radar_ready_flag = 0; /* Flag to indicate whether radar_wakeup_host is high */

/* Private user code ---------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/

static void APP_SystemClockConfig(void);
static void APP_IntPinConfig(void);
static void APP_RadarOutPinConfig(void);
static void APP_WakeupPinConfig(void);
static void APP_UartInit(void);
static void app_process_radar_rata(void);

/**
 * @brief  应用程序入口函数.
 * @retval int
 */
int main(void)
{
	/* systick初始化 */
	HAL_Init();

	/* 配置系统时钟为HSI */
	APP_SystemClockConfig();

	APP_IntPinConfig();
	APP_RadarOutPinConfig();
	APP_WakeupPinConfig();

	APP_UartInit();

	for (;;) {
		if (uart_upper.state == UART_STATE_RX_COMPLETE) {
			HAL_GPIO_WritePin(PIN_INT_PORT, PIN_INT_PIN, GPIO_PIN_SET);
			HAL_Delay(5U);
			(void)HAL_UART_Transmit(uart_module.handle, uart_upper.rx_buf, uart_upper.rx_cnt, 0xFFFFU);
			HAL_Delay(5U);
			HAL_GPIO_WritePin(PIN_INT_PORT, PIN_INT_PIN, GPIO_PIN_RESET);
			uart_upper.rx_cnt = 0;
			uart_upper.state = UART_STATE_IDLE;
		}
		if (uart_module.state == UART_STATE_RX_COMPLETE) {
			app_process_radar_rata();
		}
	}
}

static void app_process_radar_rata(void)
{
    if (uart_module.rx_cnt > 0) {
        (void)HAL_UART_Transmit(uart_upper.handle, uart_module.rx_buf, uart_module.rx_cnt, 0xFFFFU);
        uart_module.rx_cnt = 0;
    }
    uart_module.state = UART_STATE_IDLE;
}

/**
 * @brief  USART错误回调执行函数
 * @param  huart：USART句柄
 * @retval 无
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	printf("Uart Error, ErrorCode = %d\r\n", huart->ErrorCode);
}

void hal_gpio_exti_callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == RADAR_WAKEUP_PIN) {
        if (HAL_GPIO_ReadPin(RADAR_WAKEUP_PORT, RADAR_WAKEUP_PIN) == GPIO_PIN_SET) {
            /* Rising edge detected - Radar has data to send */
            m_radar_ready_flag = 1;
            LL_USART_EnableIT_RXNE(USART2);  /* Enable USART2 RXNE interrupt */
        } else {
            /* Falling edge detected - Radar data transmission completed */
            m_radar_ready_flag = 0;
            LL_USART_DisableIT_RXNE(USART2); /* Disable USART2 RXNE interrupt */
            
            /* If there is data to process, handle it immediately */
            if (uart_module.state == UART_STATE_BUSY_RX && uart_module.rx_cnt > 0) {
                uart_module.state = UART_STATE_RX_COMPLETE;
                app_process_radar_rata();
            }
        }
    }
}

/**
 * @brief   系统时钟配置函数
 * @param   无
 * @retval  无
 */
static void APP_SystemClockConfig(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

	/* 配置时钟源HSE/HSI/LSE/LSI */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_LSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;                          /* 开启HSI */
	RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;                          /* 不分频 */
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_24MHz; /* 配置HSI输出时钟为24MHz */
	RCC_OscInitStruct.HSEState = RCC_HSE_OFF;                         /* 关闭HSE */
	/* RCC_OscInitStruct.HSEFreq = RCC_HSE_16_32MHz; */               /* HSE工作频率范围16M~32M */
	RCC_OscInitStruct.LSIState = RCC_LSI_OFF;                         /* 关闭LSI */

	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) /* 初始化RCC振荡器 */
	{
		APP_ErrorHandler();
	}

	/* 初始化CPU,AHB,APB总线时钟 */
	RCC_ClkInitStruct.ClockType =
	        RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1; /* RCC系统时钟类型 */
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;                   /* SYSCLK的源选择为HSI */
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;                       /* APH时钟不分频 */
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;                        /* APB时钟不分频 */

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) !=
	    HAL_OK) /* 初始化RCC系统时钟(FLASH_LATENCY_0=24M以下;FLASH_LATENCY_1=48M) */
	{
		APP_ErrorHandler();
	}
}

static void APP_IntPinConfig(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	PIN_INT_CLK_ENABLE();

	GPIO_InitStruct.Pin = PIN_INT_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

	HAL_GPIO_Init(PIN_INT_PORT, &GPIO_InitStruct);
	HAL_GPIO_WritePin(PIN_INT_PORT, PIN_INT_PIN, GPIO_PIN_RESET);
}

static void APP_RadarOutPinConfig(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	__HAL_RCC_GPIOB_CLK_ENABLE();

	GPIO_InitStruct.Pin = GPIO_PIN_7;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

static void APP_WakeupPinConfig(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	__HAL_RCC_GPIOF_CLK_ENABLE();

	GPIO_InitStruct.Pin = GPIO_PIN_0;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

	HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

	/* Configure PF1 (radar_wakeup_host) as an interrupt pin */
	GPIO_InitStruct.Pin = RADAR_WAKEUP_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING; /* Detect rising and falling edges */
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(RADAR_WAKEUP_PORT, &GPIO_InitStruct);
	
	/* Enable and set EXTI interrupt priority */
	HAL_NVIC_SetPriority(EXTI0_1_IRQn, 0, 0);
	HAL_NVIC_EnableIRQ(EXTI0_1_IRQn);
	
	/* Initialize m_radar_ready_flag based on current pin state */
	m_radar_ready_flag = (HAL_GPIO_ReadPin(RADAR_WAKEUP_PORT, RADAR_WAKEUP_PIN) == GPIO_PIN_SET) ? 1 : 0;
}

static void uart_upper_init(void)
{
	uart_upper.handle = &uart_upper_handle;
	uart_upper.rx_buf = uart_upper_rx_buf;
	uart_upper.state = UART_STATE_IDLE;
	uart_upper.rx_cnt = 0;

	uart_upper_handle.Instance = USART1;
	uart_upper_handle.Init.BaudRate = 921600;
	uart_upper_handle.Init.WordLength = UART_WORDLENGTH_8B;
	uart_upper_handle.Init.StopBits = UART_STOPBITS_1;
	uart_upper_handle.Init.Parity = UART_PARITY_NONE;
	uart_upper_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	uart_upper_handle.Init.Mode = UART_MODE_TX_RX;
	uart_upper_handle.Init.OverSampling = UART_OVERSAMPLING_16;
	uart_upper_handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
	if (HAL_UART_Init(&uart_upper_handle) != HAL_OK) {
		APP_ErrorHandler();
	}

	LL_USART_EnableIT_RXNE(USART1);
	LL_USART_EnableIT_IDLE(USART1);
}

static void uart_module_init(void)
{
	uart_module.handle = &uart_module_handle;
	uart_module.rx_buf = uart_module_rx_buf;
	uart_module.state = UART_STATE_IDLE;
	uart_module.rx_cnt = 0;

	uart_module_handle.Instance = USART2;
	uart_module_handle.Init.BaudRate = 921600;
	uart_module_handle.Init.WordLength = UART_WORDLENGTH_8B;
	uart_module_handle.Init.StopBits = UART_STOPBITS_1;
	uart_module_handle.Init.Parity = UART_PARITY_NONE;
	uart_module_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	uart_module_handle.Init.Mode = UART_MODE_TX_RX;
	uart_module_handle.Init.OverSampling = UART_OVERSAMPLING_16;
	uart_module_handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
	if (HAL_UART_Init(&uart_module_handle) != HAL_OK) {
		APP_ErrorHandler();
	}

	/* Disable the RXNE interrupt for USART2 during initialization, until m_radar_ready_flag is high */
    LL_USART_EnableIT_IDLE(USART2);
    if (m_radar_ready_flag) {
        LL_USART_EnableIT_RXNE(USART2);
    } else {
        LL_USART_DisableIT_RXNE(USART2);
    }
}

static void APP_UartInit(void)
{
	uart_upper_init();
	uart_module_init();
}

/**
 * @brief  USART中断处理函数
 * @param  USARTx：USART模块，可以是USART1、USART2
 * @retval 无
 */
void APP_Usart1IRQCallback(USART_TypeDef *USARTx)
{
	if ((LL_USART_IsActiveFlag_RXNE(USARTx) != RESET) && (LL_USART_IsEnabledIT_RXNE(USARTx) != RESET)) {
		uart_upper_rx_buf[uart_upper.rx_cnt] = LL_USART_ReceiveData8(USARTx);
		uart_upper.rx_cnt++;
		uart_upper.state = UART_STATE_BUSY_RX;
	}
	if ((LL_USART_IsActiveFlag_IDLE(USARTx) != RESET) && (LL_USART_IsEnabledIT_IDLE(USARTx) != RESET)) {
		LL_USART_ClearFlag_IDLE(USARTx);
		uart_upper.state = UART_STATE_RX_COMPLETE;
	}
}

void APP_Usart2IRQCallback(USART_TypeDef *USARTx)
{
	if ((LL_USART_IsActiveFlag_RXNE(USARTx) != RESET) && (LL_USART_IsEnabledIT_RXNE(USARTx) != RESET) && m_radar_ready_flag) {
		uart_module.rx_buf[uart_module.rx_cnt] = LL_USART_ReceiveData8(USARTx);
		uart_module.rx_cnt++;
		uart_module.state = UART_STATE_BUSY_RX;
	}
	if ((LL_USART_IsActiveFlag_IDLE(USARTx) != RESET) && (LL_USART_IsEnabledIT_IDLE(USARTx) != RESET)) {
		LL_USART_ClearFlag_IDLE(USARTx);
		if (uart_module.rx_cnt > 0) {
			uart_module.state = UART_STATE_RX_COMPLETE;
        	}
	}
}

/**
 * @brief  错误执行函数
 * @param  无
 * @retval 无
 */
void APP_ErrorHandler(void)
{
	/* 无限循环 */
	while (1) {}
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  输出产生断言错误的源文件名及行号
 * @param  file：源文件名指针
 * @param  line：发生断言错误的行号
 * @retval 无
 */
void assert_failed(uint8_t *file, uint32_t line)
{
	/* 用户可以根据需要添加自己的打印信息,
	   例如: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
	/* 无限循环 */
	while (1) {}
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
