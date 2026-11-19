/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "rtos_app.h"
#include "boot_trace.h"
#if APP_STOP_ENABLE
#include "i2c.h"
#include "spi.h"
#include "usart.h"
#include "lora_app.h"
#endif

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);
void vApplicationIdleHook(void);

/* USER CODE BEGIN 4 */
#if APP_STOP_ENABLE
/* 仅在 RTC 未占用或配置匹配时使用 LSE 唤醒定时器；冲突时保留备份域。 */
#define STOP_RTC_HZ          2048U
#define STOP_RTC_WRAP        (60U * STOP_RTC_HZ)
#define STOP_RTC_PREDIV      ((15U << RTC_PRER_PREDIV_A_Pos) | (STOP_RTC_HZ - 1U))
#define STOP_WUT_EXTI        EXTI_IMR_MR22
#define STOP_MAX_MS          250U
#define STOP_GUARD_MS        10U
#define STOP_MIN_TICKS       25U
#define STOP_BOOT_WAIT_MS    5000U
#define STOP_TEST_WAIT_MS    100U
#define STOP_FAULT_MAGIC     0x53544f00UL

enum { STOP_COLD, STOP_LSE_WAIT, STOP_READY, STOP_UNAVAILABLE, STOP_FAULT, STOP_TEST_WAIT };
enum { STOP_NO_FAULT, STOP_DBP_FAULT, STOP_RTC_CONFLICT, STOP_LSE_FAULT,
       STOP_RTC_INIT_FAULT, STOP_SELFTEST_FAULT, STOP_WUT_FAULT, STOP_CLOCK_FAULT, STOP_STAMP_FAULT };
/* 单独输出 LPSTOP，不改变 RUN 字段。 */
volatile app_stop_diag_t app_stop_diag;

void app_stop_get_diag(app_stop_diag_t *diag)
{
    *diag = app_stop_diag;
}

static uint32_t stop_lse_start, stop_us_remainder, stop_total_us_remainder;
static uint32_t stop_test_start_ms, stop_test_start_rtc;
static volatile uint8_t stop_test_irq;
static uint8_t stop_stamp(uint32_t *stamp);
static uint8_t stop_arm(uint32_t ms);
static uint8_t stop_disarm(void);

static void stop_unavailable(uint32_t why)
{
    app_stop_diag.fault_reason = why;
    app_stop_diag.state = STOP_UNAVAILABLE;
}

static void stop_wfi(void)
{
    __DSB();
    __WFI();
    __ISB();
}

static uint8_t stop_wait(volatile const uint32_t *reg, uint32_t mask,
                         uint32_t value, uint32_t attempts)
{
    do {
        if ((*reg & mask) == value) return 1U;
    } while (--attempts != 0U);
    return 0U;
}

static void stop_rtc_unlock(void)
{
    RTC->WPR = 0xcaU;
    RTC->WPR = 0x53U;
}

static void stop_rtc_clear(void)
{
    /* RTC_ISR 的事件标记写 0 清除，保留其他标记。 */
    RTC->ISR = ~(RTC_ISR_WUTF | RTC_ISR_INIT) | (RTC->ISR & RTC_ISR_INIT);
    EXTI->PR = STOP_WUT_EXTI;
    NVIC_ClearPendingIRQ(RTC_WKUP_IRQn);
}

static void stop_irq_release(void)
{
    EXTI->IMR &= ~STOP_WUT_EXTI;
    NVIC_DisableIRQ(RTC_WKUP_IRQn);
    stop_rtc_clear();
}

void RTC_WKUP_IRQHandler(void)
{
    /* 进入 STOP 后由屏蔽中断的休眠流程处理唤醒标记。 */
    if (app_stop_diag.state == STOP_TEST_WAIT && (RTC->ISR & RTC_ISR_WUTF) &&
        (EXTI->PR & STOP_WUT_EXTI))
        stop_test_irq = 1U;
    stop_rtc_clear();
}

static void stop_prepare(void)
{
    uint32_t bdcr;
    if (app_stop_diag.state == STOP_TEST_WAIT)
    {
        uint32_t stamp, delta;
        uint8_t rtc_ok;
        if (!stop_test_irq && (uint32_t)(HAL_GetTick() - stop_test_start_ms) < STOP_TEST_WAIT_MS)
            return;
        rtc_ok = stop_stamp(&stamp);
        delta = rtc_ok ? (stamp + STOP_RTC_WRAP - stop_test_start_rtc) % STOP_RTC_WRAP : 0U;
        __disable_irq();
        if (stop_disarm() && stop_test_irq && delta >= 8U && delta <= STOP_RTC_HZ)
            app_stop_diag.state = STOP_READY;
        else
        {
            if (app_stop_diag.state != STOP_UNAVAILABLE) stop_unavailable(STOP_SELFTEST_FAULT);
            stop_irq_release();
        }
        __enable_irq();
        return;
    }
    if (app_stop_diag.state == STOP_COLD)
    {
        __HAL_RCC_PWR_CLK_ENABLE();
        PWR->CR |= PWR_CR_DBP;
        if (!stop_wait(&PWR->CR, PWR_CR_DBP, PWR_CR_DBP, 20000U))
        {
            stop_unavailable(STOP_DBP_FAULT);
            return;
        }
        if (RTC->BKP19R != 0U)
        {
            if ((RTC->BKP19R & 0xffffff00UL) == STOP_FAULT_MAGIC)
            {
                app_stop_diag.fault_reason = RTC->BKP19R & 0xffU;
                app_stop_diag.state = STOP_FAULT;
            }
            else stop_unavailable(STOP_RTC_CONFLICT);
            return;
        }
        bdcr = RCC->BDCR;
        if (((bdcr & RCC_BDCR_RTCSEL) != 0U &&
             (bdcr & RCC_BDCR_RTCSEL) != RCC_RTCCLKSOURCE_LSE) ||
            ((bdcr & RCC_BDCR_RTCEN) && (bdcr & RCC_BDCR_RTCSEL) == 0U))
        {
            stop_unavailable(STOP_RTC_CONFLICT);
            return;
        }
        RCC->BDCR |= RCC_BDCR_LSEON;
        stop_lse_start = HAL_GetTick();
        app_stop_diag.state = STOP_LSE_WAIT;
    }
    if (app_stop_diag.state != STOP_LSE_WAIT) return;
    if (!(RCC->BDCR & RCC_BDCR_LSERDY))
    {
        if ((uint32_t)(HAL_GetTick() - stop_lse_start) >= STOP_BOOT_WAIT_MS)
            stop_unavailable(STOP_LSE_FAULT);
        return;
    }

    bdcr = RCC->BDCR;
    if (!(bdcr & RCC_BDCR_RTCEN))
    {
        RCC->BDCR = bdcr | RCC_RTCCLKSOURCE_LSE | RCC_BDCR_RTCEN;
        if (RTC->ISR & RTC_ISR_INITS)
        {
            stop_unavailable(STOP_RTC_CONFLICT);
            return;
        }
        stop_rtc_unlock();
        RTC->ISR |= RTC_ISR_INIT;
        if (!stop_wait(&RTC->ISR, RTC_ISR_INITF, RTC_ISR_INITF, 20000U))
        {
            RTC->ISR &= ~RTC_ISR_INIT;
            RTC->WPR = 0xffU;
            stop_unavailable(STOP_RTC_INIT_FAULT);
            return;
        }
        RTC->PRER = STOP_RTC_HZ - 1U;
        RTC->PRER = STOP_RTC_PREDIV;
        RTC->CR |= RTC_CR_BYPSHAD;
        RTC->ISR &= ~RTC_ISR_INIT;
        RTC->WPR = 0xffU;
    }
    if ((RTC->PRER & (RTC_PRER_PREDIV_A | RTC_PRER_PREDIV_S)) != STOP_RTC_PREDIV ||
        !(RTC->CR & RTC_CR_BYPSHAD) || (RTC->CR & (RTC_CR_WUTE | RTC_CR_WUTIE)) ||
        (EXTI->IMR & STOP_WUT_EXTI))
    {
        stop_unavailable(STOP_RTC_CONFLICT);
        return;
    }
    stop_rtc_clear();
    EXTI->FTSR &= ~STOP_WUT_EXTI;
    EXTI->RTSR |= STOP_WUT_EXTI;
    EXTI->IMR |= STOP_WUT_EXTI;
    NVIC_SetPriority(RTC_WKUP_IRQn, configLIBRARY_LOWEST_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(RTC_WKUP_IRQn);
    /* 启用 STOP 之前，先验证 RTC 走时与实际唤醒中断。 */
    if (!stop_stamp(&stop_test_start_rtc))
    {
        stop_unavailable(STOP_RTC_INIT_FAULT);
        stop_irq_release();
        return;
    }
    stop_test_irq = 0U;
    stop_test_start_ms = HAL_GetTick();
    app_stop_diag.state = STOP_TEST_WAIT;
    if (!stop_arm(8U))
    {
        (void)stop_disarm();
        stop_unavailable(STOP_WUT_FAULT);
        stop_irq_release();
    }
}

static uint8_t stop_stamp(uint32_t *stamp)
{
    /* 直接读取低速时钟域，重复核对秒与亚秒，拒绝跨界读数。 */
    uint32_t attempts = 16U;
    do {
        uint32_t tr = RTC->TR & 0x7fU;
        uint32_t ss = RTC->SSR;
        uint32_t tr2 = RTC->TR & 0x7fU;
        if (tr == tr2 && ss == RTC->SSR && ss < STOP_RTC_HZ &&
            (tr & 0xfU) <= 9U && (tr >> 4U) <= 5U)
        {
            *stamp = ((tr >> 4U) * 10U + (tr & 0xfU)) * STOP_RTC_HZ + (STOP_RTC_HZ - 1U - ss);
            return 1U;
        }
    } while (--attempts);
    return 0U;
}

static uint8_t stop_arm(uint32_t ms)
{
    uint32_t count = (ms * STOP_RTC_HZ) / 1000U;
    if (!stop_wait(&RTC->ISR, RTC_ISR_WUTWF, RTC_ISR_WUTWF, 20000U)) return 0U;
    stop_rtc_clear();
    stop_rtc_unlock();
    RTC->CR &= ~RTC_CR_WUCKSEL; /* 唤醒定时器时钟为 RTCCLK/16。 */
    RTC->WUTR = count - 1U;
    RTC->CR |= RTC_CR_WUTIE | RTC_CR_WUTE;
    RTC->WPR = 0xffU;
    return (uint8_t)((RTC->CR & (RTC_CR_WUTE | RTC_CR_WUTIE)) == (RTC_CR_WUTE | RTC_CR_WUTIE));
}

static uint8_t stop_disarm(void)
{
    uint8_t ok;
    stop_rtc_unlock();
    RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
    RTC->WPR = 0xffU;
    ok = stop_wait(&RTC->ISR, RTC_ISR_WUTWF, RTC_ISR_WUTWF, 20000U);
    stop_rtc_clear();
    if (!ok)
    {
        stop_irq_release();
        stop_unavailable(STOP_WUT_FAULT);
    }
    return ok;
}

static uint8_t stop_restore_clock(uint32_t dividers)
{
    const uint32_t mask = RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2;
    if ((RCC->CFGR & mask) != dividers) return 0U;
    if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL) return 1U;
    /* STOP 保留 HSI 驱动的 PLL 参数；TIM6 暂停时不能用 HAL 超时等待。 */
    RCC->CR |= RCC_CR_HSION;
    if (!stop_wait(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY, 100000U)) return 0U;
    RCC->CR |= RCC_CR_PLLON;
    if (!stop_wait(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY, 100000U)) return 0U;
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    return stop_wait(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL, 100000U);
}

static void stop_restart_safely(uint32_t why)
{
    /* 时钟恢复失败时记入备份域并重启，避免带故障时基继续运行。 */
    app_stop_diag.fault_reason = why;
    app_stop_diag.state = STOP_FAULT;
    RTC->BKP19R = STOP_FAULT_MAGIC | why;
    __DSB();
    if (RTC->BKP19R == (STOP_FAULT_MAGIC | why)) NVIC_SystemReset();
    /* 备份标记写入失败时等待人工重启，避免反复复位。 */
    for (;;) stop_wfi();
}

static uint8_t stop_safe(void)
{
    return (uint8_t)((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) == 0U &&
        !(DBGMCU->CR & (DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_SLEEP)) &&
        (RCC->BDCR & RCC_BDCR_LSERDY) && RTC->BKP19R == 0U &&
        SystemCoreClock == 84000000U && !(RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) &&
        (RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL &&
        hi2c1.State == HAL_I2C_STATE_READY && !(I2C1->SR2 & I2C_SR2_BUSY) &&
        hspi1.State == HAL_SPI_STATE_READY && !(SPI1->SR & SPI_SR_BSY) &&
        huart1.gState == HAL_UART_STATE_READY && huart1.RxState == HAL_UART_STATE_READY &&
        (USART1->SR & USART_SR_TC) && Lora_App_CanStop());
}

void app_stop_sleep(uint32_t expected_ticks)
{
    uint32_t ctrl, dividers, tim_cr1, tim_cnt, sys_rem, since, end, ms, q;
    uint32_t elapsed_us, elapsed_ticks, stepped, reload, total;
    const uint32_t cycles = 84000U; /* 84 MHz 下每毫秒 84000 个周期。 */
    uint8_t entered, rtc_woke;
    if (app_stop_diag.state != STOP_READY || expected_ticks < STOP_MIN_TICKS)
    {
        ++app_stop_diag.bypasses;
        stop_wfi();
        return;
    }
    __disable_irq();
    __DSB();
    __ISB();
    if (eTaskConfirmSleepModeStatus() == eAbortSleep)
    {
        ++app_stop_diag.bypasses;
        __enable_irq();
        return;
    }
    ctrl = SysTick->CTRL;
    tim_cr1 = TIM6->CR1;
    if (!stop_safe() || (ctrl & 7U) != 7U || SysTick->LOAD != cycles - 1U ||
        !(tim_cr1 & TIM_CR1_CEN) || !(TIM6->DIER & TIM_DIER_UIE) ||
        TIM6->PSC != 83U || TIM6->ARR != 999U ||
        uwTickFreq != HAL_TICK_FREQ_1KHZ || (SCB->ICSR & SCB_ICSR_VECTPENDING_Msk))
    {
        ++app_stop_diag.bypasses;
        __enable_irq();
        stop_wfi();
        return;
    }

    dividers = RCC->CFGR & (RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
    SysTick->CTRL = ctrl & (SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_CLKSOURCE_Msk);
    TIM6->CR1 = tim_cr1 & ~TIM_CR1_CEN;
    sys_rem = SysTick->VAL;
    tim_cnt = TIM6->CNT;
    if ((SCB->ICSR & (SCB_ICSR_PENDSTSET_Msk | SCB_ICSR_PENDSVSET_Msk | SCB_ICSR_VECTPENDING_Msk)) ||
        (TIM6->SR & TIM_SR_UIF) || !stop_stamp(&since))
    {
        TIM6->CR1 = tim_cr1;
        SysTick->CTRL = ctrl & 7U;
        ++app_stop_diag.bypasses;
        __enable_irq();
        return;
    }
    if (sys_rem == 0U) sys_rem = cycles;
    ms = expected_ticks - STOP_GUARD_MS;
    if (ms > STOP_MAX_MS) ms = STOP_MAX_MS;
    if (!stop_arm(ms) || !(RCC->BDCR & RCC_BDCR_LSERDY))
    {
        /* 配置 RTC 失败后停用 STOP。 */
        (void)stop_disarm();
        stop_unavailable(STOP_WUT_FAULT);
        stop_irq_release();
        TIM6->CR1 = tim_cr1;
        SysTick->CTRL = ctrl & 7U;
        ++app_stop_diag.bypasses;
        __enable_irq();
        return;
    }

    __DSB();
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
    __ISB();
    entered = (uint8_t)((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
    rtc_woke = (uint8_t)((RTC->ISR & RTC_ISR_WUTF) != 0U);
    if (!stop_restore_clock(dividers)) stop_restart_safely(STOP_CLOCK_FAULT);
    (void)stop_disarm();
    if (!stop_stamp(&end)) stop_restart_safely(STOP_STAMP_FAULT);
    q = (end + STOP_RTC_WRAP - since) % STOP_RTC_WRAP;
    if (q > 2U * STOP_RTC_HZ) stop_restart_safely(STOP_STAMP_FAULT);
    if (entered)
    {
        ++app_stop_diag.entries;
        if (rtc_woke) ++app_stop_diag.rtc_wakes;
        else ++app_stop_diag.early_wakes;
    }
    else ++app_stop_diag.bypasses;

    /* 每个 RTC 计数为 15625/32 µs，余数留到下次补偿。 */
    total = q * 15625U + stop_us_remainder;
    elapsed_us = total / 32U;
    stop_us_remainder = total % 32U;
    if (entered)
    {
        total = elapsed_us + stop_total_us_remainder;
        app_stop_diag.elapsed_ms += total / 1000U;
        stop_total_us_remainder = total % 1000U;
    }

    total = tim_cnt + elapsed_us;
    uwTick += total / 1000U;
    TIM6->CNT = total % 1000U;

    total = cycles - sys_rem + elapsed_us * 84U;
    elapsed_ticks = total / cycles;
    reload = cycles - total % cycles;
    if (reload < 32U) reload = 32U;
    /* 补偿节拍不能直接跨过任务截止点；剩余节拍由内核恢复时处理。 */
    stepped = elapsed_ticks;
    if (stepped >= expected_ticks)
    {
        ++app_stop_diag.deadline_overruns;
        stepped = expected_ticks - 1U;
    }
    if (stepped) vTaskStepTick((TickType_t)stepped);
    while (stepped++ < elapsed_ticks) (void)xTaskIncrementTick();

    SysTick->LOAD = reload - 1U;
    SysTick->VAL = 0U;
    SysTick->CTRL = ctrl & 7U;
    /* 先载入本次剩余节拍的计数，再恢复正常重装值。 */
    for (uint32_t spins = 32U; spins && SysTick->VAL == 0U; --spins) __NOP();
    SysTick->LOAD = cycles - 1U;
    TIM6->CR1 = tim_cr1;
    __DSB();
    __enable_irq();
    __ISB();
}
#endif

void vApplicationIdleHook(void)
{
#if APP_STOP_ENABLE
   stop_prepare();
   /* RTC 就绪后由调度器按下一任务期限选择休眠。 */
   if (app_stop_diag.state == STOP_READY) return;
#endif
   /* STOP 未就绪时用 WFI 浅睡，保留 RTOS 和 HAL 的 1 kHz 时基。 */
   __DSB();
   __WFI();
   __ISB();
}

void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   /* 任务栈溢出时记录故障并停止。 */
   (void)xTask;
   BootTrace_Write("\r\n[RTOS] FATAL: stack overflow in ");
   BootTrace_Write((pcTaskName != NULL) ? (const char *)pcTaskName : "unknown");
   BootTrace_Write("\r\n");
   __disable_irq();
   while (1)
   {
   }
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void)
{
   /* 内核分配失败时记录故障并停止。 */
   BootTrace_Fatal("FreeRTOS malloc failed");
   __disable_irq();
   while (1)
   {
   }
}
/* USER CODE END 5 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  (void)argument;
  RtosApp_Init();
  osThreadExit();
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
