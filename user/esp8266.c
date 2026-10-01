#include "esp8266.h"
#include "wifi_config.h"
                                                                                                                                                                                    #include "cmsis_os.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>
#include <string.h>

#define ESP_EN_GPIO_Port GPIOB
#define ESP_EN_Pin       GPIO_PIN_0

#define RB_SIZE 512u
#define RB_MASK (RB_SIZE - 1u)

extern UART_HandleTypeDef huart2;

static uint8_t rb[RB_SIZE];
static volatile uint16_t rb_w;
static volatile uint16_t rb_r;
static volatile uint8_t rb_overflow;

static char acc[192];
static size_t acc_len;

static volatile uint8_t s_restart;
static uint8_t s_fail;
static uint32_t s_backoff_ms;
static uint32_t s_boot_wait_until;

typedef enum {
    ST_BOOT = 0,
    ST_JOIN,
    ST_TCP,
    ST_UP,
    ST_BACKOFF
} esp_st_t;

static esp_st_t s_state;
static esp_st_t s_resume;

__weak void ESP8266_NotifyAlive(void)
{
}

static void rb_flush(void)
{
    NVIC_DisableIRQ(USART2_IRQn);
    rb_r = rb_w;
    rb_overflow = 0;
    NVIC_EnableIRQ(USART2_IRQn);
}

static int rb_pop(uint8_t *b)
{
    if (rb_r == rb_w) return 0;
    *b = rb[rb_r];
    rb_r = (uint16_t)((rb_r + 1u) & RB_MASK);
    return 1;
}

static void acc_reset(void)
{
    acc_len = 0;
    acc[0] = '\0';
}

static void acc_feed(void)
{
    uint8_t b;

    if (rb_overflow) {
        rb_overflow = 0;
        acc_reset();
    }
    while (rb_pop(&b)) {
        if (acc_len + 1u >= sizeof(acc)) {
            memmove(acc, acc + 64, acc_len - 64u);
            acc_len -= 64u;
        }
        acc[acc_len++] = (char)b;
        acc[acc_len] = '\0';
    }
}

void ESP8266_OnUartIrq(void)
{
    uint32_t sr = USART2->SR;

    if ((sr & (USART_SR_RXNE | USART_SR_ORE)) == 0u) {
        return;
    }
    {
        uint32_t dr = USART2->DR;
        if ((sr & USART_SR_RXNE) != 0u) {
            uint16_t next = (uint16_t)((rb_w + 1u) & RB_MASK);
            if (next == rb_r) {
                rb_overflow = 1;
            } else {
                rb[rb_w] = (uint8_t)dr;
                __DMB();
                rb_w = next;
            }
        }
    }
}

void ESP8266_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(ESP_EN_GPIO_Port, ESP_EN_Pin, GPIO_PIN_SET);
    gpio.Pin = ESP_EN_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ESP_EN_GPIO_Port, &gpio);

    rb_flush();
    acc_reset();
    s_state = ST_BOOT;
    SET_BIT(USART2->CR1, USART_CR1_RXNEIE);
}

void ESP8266_PowerOn(void)
{
    HAL_GPIO_WritePin(ESP_EN_GPIO_Port, ESP_EN_Pin, GPIO_PIN_SET);
    rb_flush();
    SET_BIT(USART2->CR1, USART_CR1_RXNEIE);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

void ESP8266_PowerOff(void)
{
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    CLEAR_BIT(USART2->CR1, USART_CR1_RXNEIE);
    NVIC_ClearPendingIRQ(USART2_IRQn);
    HAL_GPIO_WritePin(ESP_EN_GPIO_Port, ESP_EN_Pin, GPIO_PIN_RESET);
}

void ESP8266_RequestRestart(void)
{
    s_restart = 1;
}

unsigned ESP8266_FailStreak(void)
{
    return s_fail;
}

static int esp_wait(const char *ok_token, uint32_t timeout_ms)
{
    uint32_t start = osKernelGetTickCount();

    for (;;) {
        ESP8266_NotifyAlive();
        acc_feed();
        /* DISCONNECT 含有子串 CONNECT，必须先判断掉线。 */
        if (strstr(acc, "WIFI DISCONNECT") != NULL) return -3;
        if ((ok_token != NULL) && (strstr(acc, ok_token) != NULL)) return 0;
        if ((strstr(acc, "ERROR") != NULL) || (strstr(acc, "FAIL") != NULL)) return -1;
        if ((osKernelGetTickCount() - start) >= timeout_ms) return -2;
        osDelay(10);
    }
}

static int esp_cmd(const char *cmd, const char *ok_token, uint32_t timeout_ms)
{
    acc_reset();
    rb_flush();
    HAL_UART_Transmit(&huart2, (uint8_t *)cmd, (uint16_t)strlen(cmd), 1000);
    return esp_wait(ok_token, timeout_ms);
}

static void esp_backoff(esp_st_t resume)
{
    if (s_fail < 5u) s_fail++;
    s_backoff_ms = 500u << (s_fail - 1u);
    if (s_backoff_ms > 8000u) s_backoff_ms = 8000u;
    s_resume = resume;
    s_state = ST_BACKOFF;
}

static void service_urc(void)
{
    acc_feed();
    if (strstr(acc, "WIFI DISCONNECT") != NULL) {
        acc_reset();
        s_state = ST_JOIN;
        return;
    }
    if ((s_state == ST_UP) &&
        ((strstr(acc, "CLOSED") != NULL) || (strstr(acc, "link is not valid") != NULL))) {
        acc_reset();
        s_state = ST_TCP;
    }
}

int ESP8266_Poll(void)
{
    char cmd[160];

    if (s_restart) {
        s_restart = 0;
        rb_flush();
        acc_reset();
        s_fail = 0;
        s_boot_wait_until = osKernelGetTickCount() + 800u;
        s_state = ST_BOOT;
    }

    service_urc();

    switch (s_state) {
    case ST_BOOT:
        if ((int32_t)(osKernelGetTickCount() - s_boot_wait_until) < 0) {
            osDelay(20);
            return 0;
        }
        if (esp_cmd("AT\r\n", "OK", 1000) == 0) {
            s_fail = 0;
            s_state = ST_JOIN;
        } else {
            esp_backoff(ST_BOOT);
        }
        return 0;

    case ST_JOIN:
        (void)esp_cmd("ATE0\r\n", "OK", 1000);
        (void)esp_cmd("AT+CWMODE=1\r\n", "OK", 1000);
        (void)esp_cmd("AT+CIPMODE=0\r\n", "OK", 1000);
        snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASS);
        if (esp_cmd(cmd, "WIFI GOT IP", 20000) == 0) {
            s_fail = 0;
            s_state = ST_TCP;
        } else {
            esp_backoff(ST_JOIN);
        }
        return 0;

    case ST_TCP:
        (void)esp_cmd("AT+CIPCLOSE\r\n", "OK", 1000);
        snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%s\r\n", TCP_HOST, TCP_PORT);
        if (esp_cmd(cmd, "CONNECT", 8000) == 0) {
            s_fail = 0;
            s_state = ST_UP;
        } else {
            esp_backoff(ST_TCP);
        }
        return 0;

    case ST_UP:
        return 1;

    case ST_BACKOFF:
    default: {
        uint32_t left = s_backoff_ms;
        while (left > 0u) {
            uint32_t step = (left > 200u) ? 200u : left;
            ESP8266_NotifyAlive();
            osDelay(step);
            left -= step;
        }
        s_state = s_resume;
        return 0;
    }
    }
}

int ESP8266_SendData(const char *data)
{
    char cmd[32];
    int len;
    int rc;

    if ((s_state != ST_UP) || (data == NULL)) return -1;
    len = (int)strlen(data);
    if ((len <= 0) || (len > 200)) return -1;

    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d\r\n", len);
    rc = esp_cmd(cmd, ">", 2000);
    if (rc != 0) {
        s_state = (rc == -3) ? ST_JOIN : ST_TCP;
        return -1;
    }

    HAL_UART_Transmit(&huart2, (uint8_t *)data, (uint16_t)len, 1000);
    rc = esp_wait("SEND OK", 3000);
    if (rc != 0) {
        s_state = (rc == -3) ? ST_JOIN : ST_TCP;
        return -2;
    }
    return 0;
}
