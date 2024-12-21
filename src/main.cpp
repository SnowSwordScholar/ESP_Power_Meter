#include <TFT_eSPI.h>
#include <SPI.h>
#include <INA226_WE.h>
#define I2C_ADDRESS 0x40

TFT_eSPI tft = TFT_eSPI();  // 初始化 TFT 屏幕对象
INA226_WE ina226(I2C_ADDRESS);


// 全局变量定义

float multiplier = 1;  // 倍率 Φ
unsigned long startTime = 0;  // 开机时间 (毫秒)


  float shuntVoltage_mV = 0.0;
  float loadVoltage_V = 0.0;
  float busVoltage_V = 0.0;
  float current_mA = 0.0;
  float power_mW = 0.0; 
  float res = 0.0;
  float pes = 0.0;

// 功率计算函数
float calculatePower() {
  return loadVoltage_V * current_mA/1000l * multiplier; 
}

// 重置时间
void resetTime() {
  startTime = millis();
}

// 请求电压数值
void continuousSampling();
// 格式化输出
String formatFloat(float num, int decimalPlaces);

// 初始化设置
void setup() {
  tft.init();
  tft.setRotation(3);  // 屏幕方向，根据实际调整
  tft.fillScreen(TFT_BLACK);

    // 设置文本颜色和字体
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);

  // 显示开机界面内容
  tft.setCursor(10, 30);
  tft.setTextSize(3);
  tft.print("Starting");


  tft.setCursor(80, 120);
  tft.setTextSize(1);
  tft.print("Version 1.0.0");

  // 显示一个简单的加载进度条
  int progressBarWidth = 200;
  int progressBarHeight = 20;
  int progressX = (tft.width() - progressBarWidth) / 2;
  int progressY = tft.height() - 50;
  
  // 绘制进度条背景
  tft.fillRect(progressX, progressY, progressBarWidth, progressBarHeight, TFT_DARKGREY);
  
  // 填充进度条
  for (int i = 0; i <= progressBarWidth; i++) {
    tft.fillRect(progressX, progressY, i, progressBarHeight, TFT_GREEN);
    delay(3);  // 延时模拟加载进度
  }


  Serial.begin(921600);
  startTime = millis();  // 开机时间重置
  pinMode(0, INPUT_PULLUP);  // 按钮引脚，假设接在 GPIO 0 上
  Wire.begin(SDA_PIN, SCL_PIN);
  ina226.init();
  // 电阻和最大电流设置
  ina226.setResistorRange(0.005,10.0);
    /* 如果INA226提供的电流值与使用校准设备获得的值相差一个常数因子
     您可以定义一个校正因子。
     校正因子 = 校准设备提供的电流 / INA226提供的电流
  */
  ina226.setCorrectionFactor(1.220);
  
  delay(1000);
  // 功率单位显示  W 
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_PINK, TFT_BLACK);
  tft.setTextFont(2);
  tft.setCursor(0, 0);
  tft.print("Power(W):");  // 显示功率值


}

// 主循环
void loop() {
  // 检查按钮是否按下，重置时间
  if (digitalRead(0) == LOW) {
    resetTime();
  }

  continuousSampling();

  // 清屏
  // tft.fillScreen(TFT_BLACK);

  // 功率显示（左上角大字）
  tft.setTextColor(TFT_PINK, TFT_BLACK);
  tft.setTextFont(6);
  tft.setCursor(10, 20);
  tft.print(formatFloat(calculatePower(),3));  // 显示功率值


  // 分隔线（cyan）
  tft.drawLine(0, 68, 160, 68, TFT_CYAN);

  // // 电压和电流
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextFont(4);
  tft.setCursor(0, 77);
  tft.print(formatFloat(loadVoltage_V,2));
  tft.print(" V");

  tft.setCursor(0, 105);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.print(formatFloat(current_mA/1000l,3));
  tft.print(" A");



  tft.setTextFont(2);
  // 左侧显示倍率 Φ
  tft.setCursor(90, 79);
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
  tft.setCursor(90, 0);  // 调整位置
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.print(timeBuffer);

  // delay(200);  // 屏幕刷新间隔
}


void continuousSampling() {


  ina226.readAndClearFlags();
  shuntVoltage_mV = ina226.getShuntVoltage_mV();
  busVoltage_V = ina226.getBusVoltage_V();
  current_mA = ina226.getCurrent_mA();
  power_mW = ina226.getBusPower();
  loadVoltage_V  = busVoltage_V + (shuntVoltage_mV / 1000);

  Serial.print("Shunt Voltage [mV]: "); Serial.println(shuntVoltage_mV);
  Serial.print("Bus Voltage [V]: "); Serial.println(busVoltage_V);
  Serial.print("Load Voltage [V]: "); Serial.println(loadVoltage_V);
  Serial.print("Current [mA]: "); Serial.println(current_mA);
  res = busVoltage_V / 0.07;
  pes = res * 10;
  Serial.print("Percentage: "); Serial.print(pes); Serial.println("%");
  Serial.print("Bus Power [mW]: "); Serial.println(power_mW);

  if(!ina226.overflow){
    Serial.println("Values OK - no overflow");
  } else {
    Serial.println("Overflow! Choose higher current range");
  }
  Serial.println();
}



String formatFloat(float num, int decimalPlaces) {
  // If the number is negative, set it to zero
  if (num < 0) {
    num = 0;
  }

  // Create a format string for the required number of decimal places
  String formatString = String("%0." + String(decimalPlaces) + "f");
  
  // Format the number according to the format string
  char buffer[20];  // Buffer to hold the formatted string
  snprintf(buffer, sizeof(buffer), formatString.c_str(), num);

  String formattedString = String(buffer);

  // Ensure the output always has two digits before the decimal point
  if (formattedString.length() < (decimalPlaces + 4)) {  // Considering 1 for decimal point
    formattedString = "0" + formattedString;
  }

  return formattedString;
}
