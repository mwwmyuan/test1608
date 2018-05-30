#ifndef __HAL_GPIO_H__
#define __HAL_GPIO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "plat_types.h"
#include "hal_iomux.h"

enum HAL_GPIO_PIN_T {
    HAL_GPIO_PIN_P0_0 = HAL_IOMUX_PIN_P0_0,
    HAL_GPIO_PIN_P0_1 = HAL_IOMUX_PIN_P0_1,
    HAL_GPIO_PIN_P0_2 = HAL_IOMUX_PIN_P0_2,
    HAL_GPIO_PIN_P0_3 = HAL_IOMUX_PIN_P0_3,
    HAL_GPIO_PIN_P0_4 = HAL_IOMUX_PIN_P0_4,
    HAL_GPIO_PIN_P0_5 = HAL_IOMUX_PIN_P0_5,
    HAL_GPIO_PIN_P0_6 = HAL_IOMUX_PIN_P0_6,
    HAL_GPIO_PIN_P0_7 = HAL_IOMUX_PIN_P0_7,

    HAL_GPIO_PIN_P1_0 = HAL_IOMUX_PIN_P1_0,
    HAL_GPIO_PIN_P1_1 = HAL_IOMUX_PIN_P1_1,
    HAL_GPIO_PIN_P1_2 = HAL_IOMUX_PIN_P1_2,
    HAL_GPIO_PIN_P1_3 = HAL_IOMUX_PIN_P1_3,
    HAL_GPIO_PIN_P1_4 = HAL_IOMUX_PIN_P1_4,
    HAL_GPIO_PIN_P1_5 = HAL_IOMUX_PIN_P1_5,
    HAL_GPIO_PIN_P1_6 = HAL_IOMUX_PIN_P1_6,
    HAL_GPIO_PIN_P1_7 = HAL_IOMUX_PIN_P1_7,

    HAL_GPIO_PIN_P2_0 = HAL_IOMUX_PIN_P2_0,
    HAL_GPIO_PIN_P2_1 = HAL_IOMUX_PIN_P2_1,
    HAL_GPIO_PIN_P2_2 = HAL_IOMUX_PIN_P2_2,
    HAL_GPIO_PIN_P2_3 = HAL_IOMUX_PIN_P2_3,
    HAL_GPIO_PIN_P2_4 = HAL_IOMUX_PIN_P2_4,
    HAL_GPIO_PIN_P2_5 = HAL_IOMUX_PIN_P2_5,
    HAL_GPIO_PIN_P2_6 = HAL_IOMUX_PIN_P2_6,
    HAL_GPIO_PIN_P2_7 = HAL_IOMUX_PIN_P2_7,

    HAL_GPIO_PIN_P3_0 = HAL_IOMUX_PIN_P3_0,
    HAL_GPIO_PIN_P3_1 = HAL_IOMUX_PIN_P3_1,
    HAL_GPIO_PIN_P3_2 = HAL_IOMUX_PIN_P3_2,
    HAL_GPIO_PIN_P3_3 = HAL_IOMUX_PIN_P3_3,
    HAL_GPIO_PIN_P3_4 = HAL_IOMUX_PIN_P3_4,
    HAL_GPIO_PIN_P3_5 = HAL_IOMUX_PIN_P3_5,
    HAL_GPIO_PIN_P3_6 = HAL_IOMUX_PIN_P3_6,
    HAL_GPIO_PIN_P3_7 = HAL_IOMUX_PIN_P3_7,
    
    HAL_GPIO_PIN_NUM = HAL_IOMUX_PIN_NUM,
};

enum HAL_GPIO_DIR_T {
    HAL_GPIO_DIR_IN = 0,
    HAL_GPIO_DIR_OUT = 1,
};

enum HAL_GPIO_IRQ_TYPE_T {
    HAL_GPIO_IRQ_TYPE_LEVEL_SENSITIVE = 0,
    HAL_GPIO_IRQ_TYPE_EDGE_SENSITIVE,
};

enum HAL_GPIO_IRQ_POLARITY_T {
    HAL_GPIO_IRQ_POLARITY_LOW_FALLING = 0,
    HAL_GPIO_IRQ_POLARITY_HIGH_RISING,
};

typedef void (* HAL_GPIO_PIN_IRQ_HANDLER)(enum HAL_GPIO_PIN_T pin);

struct HAL_GPIO_IRQ_CFG_T {
    uint8_t irq_enable:1;
    uint8_t irq_debounce:1;
    enum HAL_GPIO_IRQ_TYPE_T irq_type;
    enum HAL_GPIO_IRQ_POLARITY_T irq_porlariry;
    HAL_GPIO_PIN_IRQ_HANDLER irq_handler;
};

enum HAL_GPIO_DIR_T hal_gpio_pin_get_dir(enum HAL_GPIO_PIN_T pin);
void hal_gpio_pin_set_dir(enum HAL_GPIO_PIN_T pin, enum HAL_GPIO_DIR_T dir, uint8_t val_for_out);
void hal_gpio_pin_set(enum HAL_GPIO_PIN_T pin);
void hal_gpio_pin_clr(enum HAL_GPIO_PIN_T pin);
uint8_t hal_gpio_pin_get_val(enum HAL_GPIO_PIN_T pin);
uint8_t hal_gpio_setup_irq(enum HAL_GPIO_PIN_T pin, struct HAL_GPIO_IRQ_CFG_T *cfg);

#ifdef __cplusplus
}
#endif

#endif
