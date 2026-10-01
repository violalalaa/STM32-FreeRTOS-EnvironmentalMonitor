#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

/*
 * ESP8266 的 EN/CH_PD 接到 PB0（高电平工作）。模块若把 CH_PD 直接焊在 3.3V 上，
 * PB0 可以不接，休眠时模块不会掉电，唤醒后仍会重新入网。
 */
#define WIFI_SSID   "wifi名称"
#define WIFI_PASS   "wifi密码"
#define TCP_HOST    "ip地址"
#define TCP_PORT    "端口号"

#endif
