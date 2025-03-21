
#ifndef __TARGET_CONFIG_H
#define __TARGET_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Select module */
#if defined(USE_AT6010_HTM)
#define PIN_INT_PORT         GPIOF
#define PIN_INT_PIN          GPIO_PIN_1
#define PIN_INT_CLK_ENABLE() __HAL_RCC_GPIOF_CLK_ENABLE()
#elif defined(USE_AT2410)
#define PIN_INT_PORT         GPIOB
#define PIN_INT_PIN          GPIO_PIN_6
#define PIN_INT_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#else
#error "Please select module using Target!"
#endif

#ifdef __cplusplus
}
#endif

#endif /* __TARGET_CONFIG_H */

