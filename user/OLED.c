#include "oled.h"
#include "OLED_Font.h"
#include "main.h"     
#include <stdio.h>
#include <stdarg.h>
#define OLED_ADDR 0x78
extern I2C_HandleTypeDef hi2c1; 
/* ---------- 底层命令/数据写（硬件 I2C） ---------- */
void OLED_WriteCommand(uint8_t Command)
{
    HAL_I2C_Mem_Write(&hi2c1, OLED_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, &Command, 1, 10);
}

void OLED_WriteData(uint8_t Data)
{
    HAL_I2C_Mem_Write(&hi2c1, OLED_ADDR, 0x40, I2C_MEMADD_SIZE_8BIT, &Data, 1, 10);
}

/* ---------- 设置光标 ---------- */
void OLED_SetCursor(uint8_t Y, uint8_t X)
{
    OLED_WriteCommand(0xB0 | Y);
    OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));
    OLED_WriteCommand(0x00 | (X & 0x0F));
}

/* ---------- 清屏 ---------- */
void OLED_Clear(void)
{
    uint8_t i, j;
    for (j = 0; j < 8; j++)
    {
        OLED_SetCursor(j, 0);
        for(i = 0; i < 128; i++)
        {
            OLED_WriteData(0x00);
        }
    }
}

/* ---------- 显示一个字符 ---------- */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
    uint8_t i;
    OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);
    for (i = 0; i < 8; i++)
    {
        OLED_WriteData(OLED_F8x16[Char - ' '][i]);
    }
    OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);
    for (i = 0; i < 8; i++)
    {
        OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]);
    }
}

/* ---------- 显示字符串 ---------- */
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
    uint8_t i;
    for (i = 0; String[i] != '\0'; i++)
    {
        OLED_ShowChar(Line, Column + i, String[i]);
    }
}
void OLED_Init(void)
{
    volatile uint32_t i;
    for (i = 0; i < 1000000; i++);  // 简单粗延时，避开 HAL_Delay
    // 以下初始化序列原封不动
    OLED_WriteCommand(0xAE); // 关闭显示// 上电延时

    OLED_WriteCommand(0xAE); // 关闭显示
    OLED_WriteCommand(0xD5); 
    OLED_WriteCommand(0x80);
    OLED_WriteCommand(0xA8); 
    OLED_WriteCommand(0x3F);
    OLED_WriteCommand(0xD3); 
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(0x40); 
    OLED_WriteCommand(0xA1); 
    OLED_WriteCommand(0xC8); 
    OLED_WriteCommand(0xDA); 
    OLED_WriteCommand(0x12);
    OLED_WriteCommand(0x81); 
    OLED_WriteCommand(0xCF);
    OLED_WriteCommand(0xD9); 
    OLED_WriteCommand(0xF1);
    OLED_WriteCommand(0xDB); 
    OLED_WriteCommand(0x30);
    OLED_WriteCommand(0xA4); 
    OLED_WriteCommand(0xA6); 
    OLED_WriteCommand(0x8D); 
    OLED_WriteCommand(0x14);
    OLED_WriteCommand(0xAF); // 开启显示
    
    OLED_Clear();
}
/**
  * @brief  OLED格式化显示（类似printf）
  * @param  Line    行位置（1~4）
  * @param  Column  列位置（1~16）
  * @param  fmt     格式化字符串
  * @retval 无
  */
void OLED_Printf(uint8_t Line, uint8_t Column, const char *fmt, ...)
{
    char buf[32];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OLED_ShowString(Line, Column, buf);
}
