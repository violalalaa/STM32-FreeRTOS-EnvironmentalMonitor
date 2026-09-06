#include "power.h"
#include "cmsis_os.h"
#include "stdio.h"
#include "oled.h"

extern IWDG_HandleTypeDef hiwdg;
extern void SystemClock_Config(void); 

volatile uint8_t g_wakeup_by_key = 0;

void Enter_StopMode(void)
{
    OLED_Clear();
    OLED_ShowString(1, 2, "Sleeping...");
    osDelay(500);
    OLED_Clear(); // 关屏
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET); // 灭LED
    // 清中断标志，防抖
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_11); 
    g_wakeup_by_key = 0; 
    vTaskSuspendAll(); // 挂起调度器
	  HAL_IWDG_Refresh(&hiwdg);   // 进 STOP 前喂满
    HAL_SuspendTick();  
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI); // 进STOP
	  HAL_IWDG_Refresh(&hiwdg);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET); // 点亮
    for (volatile int i = 0; i < 1000000; i++);            // 简单延时
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);   // 熄灭
    // ===== 被按键唤醒后 =====
    SystemClock_Config(); // 恢复时钟
    HAL_ResumeTick();     // 恢复Tick
    xTaskResumeAll();     // 恢复调度

    OLED_Init();
    OLED_ShowString(1, 2, "WOKEN UP!");
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    osDelay(200);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    osDelay(200);
    
    OLED_Clear();
}