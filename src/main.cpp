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

void continuousSampling();

// 初始化设置
void setup() {
  tft.init();
  tft.setRotation(3);  // 屏幕方向，根据实际调整
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_PINK, TFT_BLACK);
  tft.setTextFont(6);
  tft.setCursor(0, 0);
  tft.print("Starting");  // 显示功率值

  Serial.begin(921600);
  startTime = millis();  // 开机时间重置
  pinMode(0, INPUT_PULLUP);  // 按钮引脚，假设接在 GPIO 0 上
  Wire.begin(SDA_PIN, SCL_PIN);
  ina226.init();
  
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
  tft.print(calculatePower(), 3);  // 显示功率值


  // 分隔线（cyan）
  tft.drawLine(0, 68, 160, 68, TFT_CYAN);

  // // 电压和电流
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextFont(4);
  tft.setCursor(0, 77);
  tft.print(loadVoltage_V, 2);
  tft.print(" V");

  tft.setCursor(0, 105);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.print(current_mA/1000l, 2);
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
  tft.setCursor(90, 107);  // 调整位置
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