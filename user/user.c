#include "stm32f1xx_hal.h"
#include "stdint.h"
#include "stdio.h"
#include "dht.h"
#include "oled.h"
#include "cmsis_os.h"
#include "esp8266.h"
#define KEY0_Pin       GPIO_PIN_11
#define KEY0_GPIO_Port GPIOA
extern osSemaphoreId_t Sem_KeyHandle;
extern osMessageQueueId_t Queue_OLEDHandle;
extern osMessageQueueId_t Queue_WiFiHandle;
extern UART_HandleTypeDef huart1; 
extern UART_HandleTypeDef huart2;
volatile uint8_t g_dht_error_count = 0;
volatile uint8_t g_wifi_error_count = 0;
int fputc(int ch, FILE *f) {
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 0xFFFF); 
    return ch;
}
 static uint8_t KEY0_Is_Pressed(void) 
{
    static uint8_t last_state = 1;    //  静态变量，记住上次的电平
    uint8_t now = HAL_GPIO_ReadPin(KEY0_GPIO_Port, KEY0_Pin);  // 读当前电平
    uint8_t pressed = 0;              // 默认没按
    if(last_state == 1 && now == 0) { // 判断下降沿
        pressed = 1;                  // 发生了按下动作
    }
    last_state = now;                 // 更新记忆
    return pressed;                   // 返回结果
}
void StartKeyTask(void *argument) {
  for(;;) {
    if (KEY0_Is_Pressed()) {
			printf("Key Task is running!\r\n");
      osSemaphoreRelease(Sem_KeyHandle);  
			osDelay(200);
    }
    osDelay(50);
  }
}
void StartDHTTask(void *argument)
{
    DHT_Data_t dht_data;
    int8_t ret;
    for(;;)
    {
        // 直接调用已经带临界区保护的DHT_Read_Data
        ret = DHT_Read_Data(&dht_data);

        if (ret == 0) // 读取成功
        {
            taskENTER_CRITICAL();
            g_dht_error_count = 0; // 成功一次就清零
            taskEXIT_CRITICAL();
					// 这里已经出了临界区，可以安全调用队列 API
            if (osMessageQueuePut(Queue_OLEDHandle, &dht_data, 0, 10) != osOK)
            {
                // printf("OLED Queue full!\r\n");可前期做调试用
            }
            if (osMessageQueuePut(Queue_WiFiHandle, &dht_data, 0, 10) != osOK)
            {
                // printf("WiFi Queue full!\r\n");可前期做调试用
            }
        }
        else
        {
           taskENTER_CRITICAL();
            g_dht_error_count++;
            taskEXIT_CRITICAL(); 
            // 读取失败  printf("DHT11 err: %d\r\n", ret);可前期做调试用
        }
        osDelay(1500); 
    }
}
void StartOLEDTask(void *argument) {
    DHT_Data_t oled_data = {0};          
    static uint8_t current_page = 0;
    char temp_str[20] = "--.-";         
    char humi_str[20] = "--.-";
    uint8_t need_update = 0;
    for(;;) {
        need_update = 0;
        if (osSemaphoreAcquire(Sem_KeyHandle, 0) == osOK) {
            current_page = !current_page;
            printf("Key pressed! Switch page.\r\n");
            need_update = 1;             // 按键肯定会刷新
        }
        if (osMessageQueueGet(Queue_OLEDHandle, &oled_data, 0, 0) == osOK) {
            // 只有成功拿到数据才更新字符串
            snprintf(temp_str, sizeof(temp_str), "%.1f", oled_data.temperature);
            snprintf(humi_str, sizeof(humi_str), "%.1f", oled_data.humidity);
            need_update = 1;
        }
        if (need_update) {
            OLED_Clear();
            if (current_page == 0) {
                OLED_ShowString(2, 1, "Temp: ");
                OLED_ShowString(2, 2, temp_str);
                OLED_ShowString(3, 1, "Humi: ");
                OLED_ShowString(3, 2, humi_str);
            } else {
                OLED_ShowString(3, 3, "violalalaa");
            }
        }
        osDelay(500);
    }
}
void StartWiFiTask(void *argument) {
    // 1. 初始化 WiFi 和连接
    HAL_UART_Transmit(&huart2, (uint8_t*)"ATE0\r\n", 6, 1000); osDelay(200);
    HAL_UART_Transmit(&huart2, (uint8_t*)"AT+CWMODE=1\r\n", 13, 1000); osDelay(200);
    HAL_UART_Transmit(&huart2, (uint8_t*)"AT+CWJAP=\"my home wifi\",\"wifi password\"\r\n", 100, 2000); osDelay(5000); 
    HAL_UART_Transmit(&huart2, (uint8_t*)"AT+CIPMODE=0\r\n", 15, 1000); osDelay(200);
    // 尝试建立第一次 TCP 连接
    HAL_UART_Transmit(&huart2, (uint8_t*)"AT+CIPSTART=\"TCP\",\"服务端ip\",8899\r\n", 100, 2000); osDelay(2000);
    DHT_Data_t wifi_data;
    char send_buf[50];
    uint8_t connect_ok = 0;
    for(;;) {
        if (osMessageQueueGet(Queue_WiFiHandle, &wifi_data, NULL, osWaitForever) == osOK) {
            sprintf(send_buf, "Temp:%.1f,Humi:%.1f", wifi_data.temperature, wifi_data.humidity);
            // 假设 ESP8266_SendData 返回 0 是成功，返回其他数字是失败
            if (ESP8266_SendData(send_buf) != 0) { 
							 g_wifi_error_count++;
                printf("发送失败 (link is not valid)，5秒后尝试重连 TCP...\r\n");
                HAL_UART_Transmit(&huart2, (uint8_t*)"AT+CIPCLOSE\r\n", 13, 1000); osDelay(500); // 先断开旧的连接，防止状态卡死
							HAL_UART_Transmit(&huart2, (uint8_t*)"AT+CIPSTART=\"TCP\",\"服务端ip\",8899\r\n", 100, 2000);  // 重新发起 TCP 连接
                osDelay(2000); // 给 ESP8266 充足的反应时间
            }else {
                  g_wifi_error_count = 0;
            }
        }
        osDelay(100);
    }
}  
void StartStatusLEDTask(void *argument) {
    for(;;) {
        if (g_wifi_error_count >= 3) {// WiFi 连续失败 3 次：快闪 5 下
            for(int i=0; i<5; i++) {
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET); // 亮
                osDelay(100);
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);   // 灭
                osDelay(100);
            }
            osDelay(2000); // 停 2 秒再循环
        }
        else if (g_dht_error_count >= 3) { // DHT 连续失败 3 次：慢闪 3 下
            for(int i=0; i<3; i++) {
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
                osDelay(300);
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
                osDelay(300);
            }
            osDelay(2000);
        }
        else {  // 正常心跳：每秒闪一次         
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
            osDelay(100);
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
            osDelay(900);
        }
    }
}