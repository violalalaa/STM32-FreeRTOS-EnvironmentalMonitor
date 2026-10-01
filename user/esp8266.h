#ifndef __ESP8266_H
#define __ESP8266_H

void ESP8266_Init(void);
void ESP8266_PowerOn(void);
void ESP8266_PowerOff(void);
void ESP8266_RequestRestart(void);
void ESP8266_OnUartIrq(void);

/* 推进一步状态机。返回 1 表示 TCP 已连通，可以发送。 */
int ESP8266_Poll(void);
int ESP8266_SendData(const char *data);
/* 连续入网/TCP 失败次数，成功后清零。 */
unsigned ESP8266_FailStreak(void);

/* 弱符号：等待 AT 应答时周期调用，用来刷新任务心跳。 */
void ESP8266_NotifyAlive(void);

#endif
