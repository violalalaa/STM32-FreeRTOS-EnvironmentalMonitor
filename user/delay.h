#ifndef __DELAY_H
#define __DELAY_H

#include "stm32f1xx_hal.h"

void delay_init(void);
void delay_us(uint32_t us);

#endif
