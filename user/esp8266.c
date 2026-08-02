#include "delay.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>
extern UART_HandleTypeDef huart2;
int ESP8266_SendData(char *data) {
    char cmd[30];
    uint8_t len = strlen(data);
    sprintf(cmd, "AT+CIPSEND=%d\r\n", len);
    HAL_UART_Transmit(&huart2, (uint8_t*)cmd, strlen(cmd), 1000);
    // 等 '<' 或 '>' 字符，最多等 1.5 秒（防止死等）
    char ch = 0;
    uint16_t timeout = 150; // 150 * 10ms = 1.5秒
    while(timeout > 0) {
        if (HAL_UART_Receive(&huart2, (uint8_t*)&ch, 1, 10) == HAL_OK) {
            if (ch == '>') break;   // 等到了，立刻发数据
        } else {
            osDelay(10); // 任务让出CPU，给别的任务跑的机会
            timeout--;
        }
    }
    if (timeout == 0) return -1; // 超时，返回失败
    // 正式发送数据
    HAL_UART_Transmit(&huart2, (uint8_t*)data, len, 1000);
    return 0;
}
