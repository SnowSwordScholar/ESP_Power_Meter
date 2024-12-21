#ifndef DEFINE_H
#define DEFINE_H


#include "FreeRTOS.h"
#include <Arduino.h>

// IIC
#define SCL_PIN 10
#define SDA_PIN 11

// 按钮
#define USER_PIN 0

// LCD
#define LCD_BLK_PIN  13   
#define LCD_SCl_PIN  14   // 时钟引脚
#define LCD_RST_PIN  15   // 复位引脚（如没有使用复位引脚，可设置为 -1）
#define LCD_CS_PIN   16   // 片选引脚
#define LCD_SDA_PIN 17    // 主设备输出，设备输入
#define LCD_DC_PIN  18    

#define KeyPin1 4	// Key1 : >
#define KeyPin2 5	// Key2 : <
#define KeyPin3 6	// key3 : #
#define KeyPin4 7	// Key4 : *



#endif // DEFINE_H




