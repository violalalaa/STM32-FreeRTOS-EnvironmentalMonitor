#include "delay.h"

static uint32_t fac_us = 0;   // 1 μs 需要多少个计数值

/* DWT 初始化，获取时钟频率 */
void delay_init(void)
{
    if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0) 
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  // 使能 DWT
    }
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;                 // 开启计数器
    fac_us = HAL_RCC_GetHCLKFreq() / 1000000;           // 72MHz -> 72
}

/* 微秒延时（0~233000 μs） */
void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * fac_us;
    while ((DWT->CYCCNT - start) < ticks);
}
