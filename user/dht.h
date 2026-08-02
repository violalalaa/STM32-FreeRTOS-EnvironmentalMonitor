#ifndef __DHT_H
#define __DHT_H

#include "stm32f1xx_hal.h"

typedef struct {
    float temperature;
    float humidity;
} DHT_Data_t;

void DHT_Init(void);
int8_t DHT_Read_Data(DHT_Data_t *data);
void delay_ms(uint32_t ms);
#endif
