#ifndef __POWER_H
#define __POWER_H

#include "stm32f1xx_hal.h"

void Power_Init(void);
void Power_LogResetReason(void);
void Enter_StopMode(void);

#endif
