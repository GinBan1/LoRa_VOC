#ifndef APP_POWER_PROFILE_H
#define APP_POWER_PROFILE_H

#include <stdint.h>

/* 0 档忙等；1 档用 RTOS 等待；2 档另外关闭 OLED。全工程档位应一致。 */
#ifndef APP_POWER_PROFILE
#define APP_POWER_PROFILE 2
#endif
#if APP_POWER_PROFILE < 0 || APP_POWER_PROFILE > 2
#error APP_POWER_PROFILE_must_be_0_1_or_2
#endif

#define PWR_OLED_ON (APP_POWER_PROFILE != 2)

/* OLED 自动熄屏单独配置。 */
#ifndef APP_OLED_AUTO
#define APP_OLED_AUTO 0
#endif
#if APP_OLED_AUTO != 0 && APP_OLED_AUTO != 1
#error APP_OLED_AUTO_must_be_0_or_1
#endif
#define PWR_OLED_AUTO (PWR_OLED_ON && APP_OLED_AUTO)
#define PWR_OLED_SHOW_MS 30000U

typedef struct
{
    uint32_t since_ms;
    uint8_t on, ready_seen, event_active;
} pwr_oled_t;

static inline void pwr_oled_init(pwr_oled_t *screen, uint32_t now)
{
    *screen = (pwr_oled_t){now, 1U, 0U, 0U};
}

/* 无效状态不视为恢复；仅首次就绪或新事件才开启显示计时。 */
static inline uint8_t pwr_oled_update(pwr_oled_t *screen, uint32_t now,
                                      uint8_t ready, uint8_t active, uint8_t alarm)
{
    if (ready)
    {
        if (!screen->ready_seen || (alarm && !screen->event_active))
        {
            screen->since_ms = now;
            screen->on = 1U;
        }
        screen->ready_seen = 1U;
        screen->event_active = active;
    }
    if (screen->on && (uint32_t)(now - screen->since_ms) >= PWR_OLED_SHOW_MS)
        screen->on = 0U;
    return screen->on;
}

#include "main.h"
#if APP_POWER_PROFILE != 0
#include "cmsis_os2.h"
#endif

/* 传感器任务专用。调度器未运行或调用失败时退回 HAL 等待；不可用于阻塞 HAL 时基的中断或临界区。 */
static inline void pwr_wait_ms(uint32_t delay_ms)
{
#if APP_POWER_PROFILE != 0
    if (delay_ms == 0U) return;
    if (__get_IPSR() == 0U && __get_PRIMASK() == 0U &&
        __get_BASEPRI() == 0U && __get_FAULTMASK() == 0U &&
        osKernelGetState() == osKernelRunning)
    {
        uint32_t frequency = osKernelGetTickFreq();
        /* 延时向上取整并多等一拍，避免 osDelay 在节拍边界提前返回。 */
        uint64_t ticks = ((uint64_t)delay_ms * frequency + 999ULL) / 1000ULL + 1ULL;
        if (frequency != 0U && ticks <= 0x7fffffffULL &&
            osDelay((uint32_t)ticks) == osOK)
            return;
    }
#endif
    HAL_Delay(delay_ms);
}

#endif /* APP_POWER_PROFILE_H */
