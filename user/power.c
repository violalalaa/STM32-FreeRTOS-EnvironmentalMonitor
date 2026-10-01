#include "power.h"
#include "cmsis_os.h"
#include "stdio.h"
#include "oled.h"
#include "esp8266.h"

extern IWDG_HandleTypeDef hiwdg;
extern void SystemClock_Config(void);

volatile uint8_t g_wakeup_by_key = 0;
volatile uint8_t g_rtc_wakeup = 0;

/*
 * IWDG: 预分频 64，重载 4095。超时 = 4096 * 64 / f_LSI。
 * LSI 最快约 60 kHz 时超时最短，约 4.4 s。RTC 与 IWDG 同源，
 * 比较的是 LSI 周期数：3 s 闹钟 = 3 * 40000 = 120000，
 * 看门狗窗口 = 4096 * 64 = 262144。闹钟先到。
 */
#define STOP_SLICE_SEC 3u
#define LSI_HZ_NOMINAL 40000u

static uint8_t s_rtc_ok;

static int rtc_wait_rtoff(void)
{
    uint32_t n = 1000000u;
    while ((RTC->CRL & RTC_CRL_RTOFF) == 0u) {
        if (--n == 0u) return -1;
    }
    return 0;
}

static int rtc_sync(void)
{
    uint32_t n = 1000000u;
    RTC->CRL &= (uint16_t)~RTC_CRL_RSF;
    while ((RTC->CRL & RTC_CRL_RSF) == 0u) {
        if (--n == 0u) return -1;
    }
    return 0;
}

static int RTC_ArmInSeconds(uint32_t seconds)
{
    uint32_t now;
    uint32_t alarm;

    if (rtc_wait_rtoff() != 0) return -1;
    now = ((uint32_t)RTC->CNTH << 16) | (RTC->CNTL & 0xFFFFu);
    alarm = now + seconds;

    RTC->CRL &= (uint16_t)~RTC_CRL_ALRF;
    EXTI->PR = EXTI_PR_PR17;

    if (rtc_wait_rtoff() != 0) return -1;
    RTC->CRL |= RTC_CRL_CNF;
    RTC->ALRH = (alarm >> 16) & 0xFFFFu;
    RTC->ALRL = alarm & 0xFFFFu;
    RTC->CRL &= (uint16_t)~RTC_CRL_CNF;
    return rtc_wait_rtoff();
}

static int rtc_setup(void)
{
    uint32_t n = 1000000u;

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    RCC->CSR |= RCC_CSR_LSION;
    while ((RCC->CSR & RCC_CSR_LSIRDY) == 0u) {
        if (--n == 0u) return -1;
    }

    if ((RCC->BDCR & RCC_BDCR_RTCSEL) != RCC_BDCR_RTCSEL_LSI) {
        RCC->BDCR |= RCC_BDCR_BDRST;
        RCC->BDCR &= ~RCC_BDCR_BDRST;
        RCC->BDCR |= RCC_BDCR_RTCSEL_LSI;
    }
    RCC->BDCR |= RCC_BDCR_RTCEN;
    /* RTCEN 之后等几个 RTCCLK，再同步寄存器。 */
    for (volatile uint32_t d = 0; d < 100000u; d++) {
    }

    if (rtc_sync() != 0) return -1;
    if (rtc_wait_rtoff() != 0) return -1;

    RTC->CRL |= RTC_CRL_CNF;
    RTC->CRH |= RTC_CRH_ALRIE;
    RTC->PRLH = 0u;
    RTC->PRLL = LSI_HZ_NOMINAL - 1u;
    RTC->CRL &= (uint16_t)~RTC_CRL_CNF;
    if (rtc_wait_rtoff() != 0) return -1;

    RTC->CRL &= (uint16_t)~RTC_CRL_ALRF;
    EXTI->IMR |= EXTI_IMR_MR17;
    EXTI->RTSR |= EXTI_RTSR_TR17;
    EXTI->PR = EXTI_PR_PR17;

    HAL_NVIC_SetPriority(RTC_Alarm_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(RTC_Alarm_IRQn);
    return 0;
}

void Power_Init(void)
{
    s_rtc_ok = (rtc_setup() == 0) ? 1u : 0u;
    if (s_rtc_ok == 0u) {
        printf("RTC init fail\r\n");
    }
}

void Power_LogResetReason(void)
{
    uint32_t csr = RCC->CSR;
    const char *reason = "UNK";

    if ((csr & RCC_CSR_IWDGRSTF) != 0u) reason = "IWDG";
    else if ((csr & RCC_CSR_WWDGRSTF) != 0u) reason = "WWDG";
    else if ((csr & RCC_CSR_SFTRSTF) != 0u) reason = "SFT";
    else if ((csr & RCC_CSR_LPWRRSTF) != 0u) reason = "LPWR";
    else if ((csr & RCC_CSR_PORRSTF) != 0u) reason = "POR";
    else if ((csr & RCC_CSR_PINRSTF) != 0u) reason = "PIN";

    printf("RST %s CSR=0x%08lX\r\n", reason, (unsigned long)csr);
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

static void stop_ticks(void)
{
    /* HAL 时基是 TIM4。FreeRTOS 节拍是 SysTick。STOP 期间 HCLK 停，
     * 两者都不再计数；进 WFI 前若中断已挂起，会立刻醒来。 */
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    HAL_SuspendTick();
    TIM4->SR &= ~TIM_SR_UIF;
    NVIC_ClearPendingIRQ(TIM4_IRQn);
}

static void resume_ticks(void)
{
    HAL_ResumeTick();
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
}

void RTC_Alarm_IRQHandler(void)
{
    if ((RTC->CRL & RTC_CRL_ALRF) != 0u) {
        uint32_t n = 100000u;
        while (((RTC->CRL & RTC_CRL_RTOFF) == 0u) && (n > 0u)) {
            n--;
        }
        RTC->CRL &= (uint16_t)~RTC_CRL_ALRF;
    }
    if ((EXTI->PR & EXTI_PR_PR17) != 0u) {
        EXTI->PR = EXTI_PR_PR17;
    }
    g_rtc_wakeup = 1u;
}

void Enter_StopMode(void)
{
    OLED_Clear();
    OLED_ShowString(1, 2, "Sleeping...");
    osDelay(500);
    OLED_DisplayOff();
    ESP8266_PowerOff();
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_11);
    g_wakeup_by_key = 0;
    g_rtc_wakeup = 0;

    vTaskSuspendAll();
    do {
        HAL_IWDG_Refresh(&hiwdg);
        if (s_rtc_ok != 0u) {
            (void)RTC_ArmInSeconds(STOP_SLICE_SEC);
        }
        g_rtc_wakeup = 0;
        stop_ticks();
        HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
        HAL_IWDG_Refresh(&hiwdg);
        SystemClock_Config();
        resume_ticks();
    } while ((s_rtc_ok != 0u) && (g_wakeup_by_key == 0u));
    xTaskResumeAll();

    ESP8266_PowerOn();
    ESP8266_RequestRestart();

    OLED_Init();
    OLED_ShowString(1, 2, "WOKEN UP!");
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    osDelay(200);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    osDelay(200);
    OLED_Clear();
}
