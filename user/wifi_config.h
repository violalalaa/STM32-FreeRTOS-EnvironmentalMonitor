#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

/*
 * 改成自己的热点和 TCP 服务端。不要把真实密码提交到公开仓库。
 * ESP8266 的 EN/CH_PD 接到 PB0（高电平工作）。模块若把 CH_PD 直接焊在 3.3V 上，
 * PB0 可以不接，休眠时模块不会掉电，唤醒后仍会重新入网。
 */
#define WIFI_SSID   "viola"
#define WIFI_PASS   "55555555"
#define TCP_HOST    "192.168.4.61"
#define TCP_PORT    "8899"

#endif
