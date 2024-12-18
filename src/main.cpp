#include <TFT_eSPI.h>
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI();  // 初始化 TFT 屏幕对象

// 全局变量定义
float voltage = 6.02;   // 电压
float current = 6.00;   // 电流
float multiplier = 1;  // 倍率 Φ
unsigned long startTime = 0;  // 开机时间 (毫秒)

// 功率计算函数
float calculatePower() {
  return voltage * current * multiplier; 
}

// 重置时间
void resetTime() {
  startTime = millis();
}

// 初始化设置
void setup() {
  tft.init();
  tft.setRotation(3);  // 屏幕方向，根据实际调整
  tft.fillScreen(TFT_BLACK);
  startTime = millis();  // 开机时间重置
  pinMode(0, INPUT_PULLUP);  // 按钮引脚，假设接在 GPIO 0 上


  // 功率单位显示  W 
  tft.setTextColor(TFT_PINK, TFT_BLACK);
  tft.setTextFont(1);
  tft.setCursor(143, 45);
  tft.print("W");  // 显示功率值


  // 分隔线（cyan）
  tft.drawLine(0, 65, 160, 65, TFT_CYAN);

}

// 主循环
void loop() {
  // 检查按钮是否按下，重置时间
  if (digitalRead(0) == LOW) {
    resetTime();
  }

  // 清屏
  // tft.fillScreen(TFT_BLACK);

  // 功率显示（左上角大字）
  tft.setTextColor(TFT_PINK, TFT_BLACK);
  tft.setTextFont(6);
  tft.setCursor(0, 5);
  tft.print(calculatePower(), 3);  // 显示功率值


  // // 电压和电流
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextFont(4);
  tft.setCursor(0, 75);
  tft.print(voltage, 2);
  tft.print(" V");

  tft.setCursor(0, 105);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.print(current, 2);
  tft.print(" A");



  tft.setTextFont(2);
  // 左侧显示倍率 Φ
  tft.setCursor(90, 77);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.print("Phi: ");
  tft.print(multiplier, 2);

  // 右侧显示时间（单位：秒）
  // 计算时间
  unsigned long elapsedSeconds = (millis() - startTime) / 1000;
  unsigned int hours = elapsedSeconds / 3600;
  unsigned int minutes = (elapsedSeconds % 3600) / 60;
  unsigned int seconds = elapsedSeconds % 60;

  // 格式化时间为 "HH:MM:SS"
  char timeBuffer[9];  // 字符数组存储 "HH:MM:SS"
  sprintf(timeBuffer, "%02d:%02d %02d", hours, minutes, seconds);

  // 显示时间
  tft.setCursor(90, 107);  // 调整位置
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.print(timeBuffer);

  // delay(200);  // 屏幕刷新间隔
}