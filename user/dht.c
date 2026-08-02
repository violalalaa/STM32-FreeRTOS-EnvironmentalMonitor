#include "dht.h"
#include "delay.h"      
#include "FreeRTOS.h"
#include "task.h"

#define DHT_PORT GPIOA
#define DHT_PIN  GPIO_PIN_1

/* 开漏输出 + 内部上拉：确保释放总线后为高电平 */
static void DHT_Mode_Out(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = DHT_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;      
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT_PORT, &GPIO_InitStruct);
}

/* 输入模式，继续保持上拉（避免浮空） */
static void DHT_Mode_In(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin  = DHT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;      
    HAL_GPIO_Init(DHT_PORT, &GPIO_InitStruct);
}
static int8_t DHT_Read_Byte(uint8_t *byte)
{
    uint8_t i;
    uint8_t value = 0;
    uint8_t mask  = 0x80;                // 从最高位开始

    for (i = 0; i < 8; i++) {
        uint32_t timeout = 10000;
        while (HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN) == GPIO_PIN_RESET) {
            if (--timeout == 0) return -1;
        }
        delay_us(30);
        if (HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN) == GPIO_PIN_SET) {
            value |= mask;
        }
        mask >>= 1;                     
        timeout = 10000;
        while (HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN) == GPIO_PIN_SET) {
            if (--timeout == 0) return -1;
        }
    }
    *byte = value;
    return 0;
}

int8_t DHT_Read_Data(DHT_Data_t *data)
{
    uint8_t buf[5];
    uint8_t i;
    uint32_t timeout;
    // 进入临界区（关闭所有中断，保证时序）
    taskENTER_CRITICAL();

    /* 1. 主机发送起始信号 */
    DHT_Mode_Out();
    HAL_GPIO_WritePin(DHT_PORT, DHT_PIN, GPIO_PIN_RESET);
    delay_ms(18);                       
    HAL_GPIO_WritePin(DHT_PORT, DHT_PIN, GPIO_PIN_SET);
    delay_us(30);                       
    DHT_Mode_In();
    // DHT11 拉低总线（响应开始）
    timeout = 10000;
    while (HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN) == GPIO_PIN_SET) {
        if (--timeout == 0) { taskEXIT_CRITICAL(); return -1; }
    }
    // DHT11 拉高总线（80μs 低电平结束）
    timeout = 10000;
    while (HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN) == GPIO_PIN_RESET) {
        if (--timeout == 0) { taskEXIT_CRITICAL(); return -2; }
    }
    // DHT11 再次拉低（准备开始传输数据）
    timeout = 10000;
    while (HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN) == GPIO_PIN_SET) {
        if (--timeout == 0) { taskEXIT_CRITICAL(); return -3; }
    }

    /* 3. 读取 5 个字节 */
    for (i = 0; i < 5; i++) {
        if (DHT_Read_Byte(&buf[i]) != 0) {
            taskEXIT_CRITICAL();
            return -4;
        }
    }
    // 数据读完，退出临界区 
    taskEXIT_CRITICAL();

    if ((buf[0] + buf[1] + buf[2] + buf[3]) != buf[4]) {
        return -5;
    }
    data->humidity    = (float)buf[0] + (float)buf[1] * 0.1f;
    data->temperature = (float)buf[2] + (float)buf[3] * 0.1f;
    return 0;
}
// delay.c 补充
void delay_ms(uint32_t ms)
{
    while (ms--)
    {
        delay_us(1000);
    }
}
