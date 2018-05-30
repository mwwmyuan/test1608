#ifndef __HAL_TIMER_H__
#define __HAL_TIMER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "plat_types.h"

#define CONFIG_SYSTICK_HZ           (16000)

#define MS_TO_TICKS64(ms)           ((uint64_t)(ms) * CONFIG_SYSTICK_HZ / 1000)

#define TICKS_TO_MS64(tick)         ((uint64_t)(tick) * 1000 / CONFIG_SYSTICK_HZ)

#define MS_TO_TICKS(ms)             ((uint32_t)MS_TO_TICKS64(ms))

#define TICKS_TO_MS(tick)           ((uint32_t)TICKS_TO_MS64(tick))

void hal_sys_timer_open(void);

uint32_t hal_sys_timer_get(void);

uint32_t hal_sys_timer_get_max(void);

void hal_sys_timer_delay(uint32_t ticks);
void hal_sys_timer_delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif

