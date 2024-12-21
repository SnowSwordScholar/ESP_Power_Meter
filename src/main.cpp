#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <INA226_WE.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

/*===================== 引脚定义 =====================*/
// 如果你已有 Define.h，可以在此 include 并确保一致
#define KeyPin1 4  // '>'
#define KeyPin2 5  // '<'
#define KeyPin3 6  // '#': 加
#define KeyPin4 7  // '*': 减 / 进入设置
#define USER_PIN  0



#define I2C_ADDRESS 0x40

/*===================== 全局对象 =====================*/
TFT_eSPI tft = TFT_eSPI();
INA226_WE ina226(I2C_ADDRESS);
Preferences prefs;

/*================= Event Group 用于任务同步 =================*/
static EventGroupHandle_t xEventGroup = NULL;
#define BIT_HARDWARE_INIT_DONE   (1 << 0)
#define BIT_ANIMATION_DONE       (1 << 1)

/*===================== 全局变量 =====================*/
float multiplier = 1.0f;     
unsigned long startTime = 0; 
bool inSettingMode = false;  
bool inSettingModeLast = false;
int editingDigitIndex = 0;   

// INA226 数据
float shuntVoltage_mV = 0.0;
float loadVoltage_V   = 0.0;
float busVoltage_V    = 0.0;
float current_mA      = 0.0;
float power_mW        = 0.0; 

// 屏幕上前一次显示的字符串，用于对比或者局部刷新
String prevPowerStr   = "";
String prevVoltStr    = "";
String prevCurrStr    = "";

// 新增：用于检测数值是否跨越10的阈值
bool powerAbove10_prev = false;
bool voltAbove10_prev = false;
bool currAbove10_prev = false;

/*===================== 函数声明 =====================*/
void animationTask(void *pvParameters);
void keyMonitorTask(void *pvParameters);
void displayTask(void *pvParameters);

void resetTime();
float calculatePower();
void continuousSampling();
String formatFloat(float num, int decimalPlaces);

// 设置模式
void enterSettingMode();
void exitSettingMode();
void handleSettingKeyPress(int keyPin);
void updateSettingUI();

/* -------------------------------------------------------------------------------- */
/*  setup：创建动画任务 + 初始化硬件 + 等待动画结束后创建其余任务                  */
/* -------------------------------------------------------------------------------- */
void setup() {
  Serial.begin(115200);

  // 初始化屏幕（仅做最小初始化，开始动画任务用）
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  // 创建 Event Group
  xEventGroup = xEventGroupCreate();
  if(xEventGroup == NULL) {
    Serial.println("Failed to create Event Group!");
    while(true){}
  }

  // 1) 先创建动画任务，用于在硬件初始化时实时刷新进度条
  xTaskCreatePinnedToCore(
    animationTask,         // 任务函数
    "AnimationTask",       
    4096,                  // 栈大小
    NULL,                  // 参数
    1,                     // 优先级 (稍低)
    NULL,                  
    0                      // Pin 到 Core 0
  );

  // 2) 主线程继续硬件初始化 (与动画并行执行)
  // ------------------ 硬件初始化开始 ------------------
  // 模拟硬件初始化的分阶段，可以在此处添加你真实的初始化流程
  Serial.println("Hardware init start...");

  // 2.1) 初始化 NVS 并读取 multiplier
  prefs.begin("my_app", false);
  multiplier = prefs.getFloat("multiplier", 1.0f);
  prefs.end();

  // 2.2) 初始化 INA226
  Wire.begin(SDA_PIN, SCL_PIN);
  ina226.init();
  ina226.setResistorRange(0.005, 10.0);
  ina226.setCorrectionFactor(1.220);

  // 2.3) 设置按键
  pinMode(KeyPin1, INPUT_PULLUP);
  pinMode(KeyPin2, INPUT_PULLUP);
  pinMode(KeyPin3, INPUT_PULLUP);
  pinMode(KeyPin4, INPUT_PULLUP);
  pinMode(USER_PIN, INPUT_PULLUP);

  // 2.4) 重置时间
  resetTime();

  // // 。。。其他硬件初始化操作。。。
  delay(200); // 模拟耗时

  Serial.println("Hardware init done.");

  // 设置事件：硬件初始化已完成
  xEventGroupSetBits(xEventGroup, BIT_HARDWARE_INIT_DONE);
  // ------------------ 硬件初始化结束 ------------------

  // 3) 等待动画结束
  EventBits_t uxBits = xEventGroupWaitBits(
      xEventGroup, 
      BIT_ANIMATION_DONE,        // 等待动画结束位
      pdFALSE,                   // 不清除该位
      pdTRUE,                    // 同时等待(这里只有 1 位，也可以使用 pdFALSE)
      portMAX_DELAY
  );
  if(uxBits & BIT_ANIMATION_DONE) {
    Serial.println("Animation Done, now create tasks...");
  }

  // 4) 动画结束后，创建：屏幕刷新 & 按键监控任务
  // ------------------------------------------------
  //   数据刷新任务：Core 1，最高优先级
  // ------------------------------------------------
  xTaskCreatePinnedToCore(
    displayTask,           // 任务函数
    "DisplayTask",         
    4096,                  // 栈大小
    NULL,                  // 参数
    5,                     // 优先级(示例中设5, 或更高)
    NULL,                  
    1                      // Pin 到 Core 1
  );

  // ------------------------------------------------
  //   按键监控任务：Core 0，优先级略低
  // ------------------------------------------------
  xTaskCreatePinnedToCore(
    keyMonitorTask,
    "KeyMonitorTask",
    4096,
    NULL,
    4,     // 比显示任务低一些即可
    NULL,
    0
  );

  // setup() 结束，删除自身任务
  vTaskDelete(NULL);
}

/* -------------------------------------------------------------------------------- */
/*  loop：在 ESP32 + FreeRTOS 下通常不会使用反复循环                                */
/* -------------------------------------------------------------------------------- */
void loop() {
  // 已经在 setup() 创建了多任务，这里空置或删除自身
}

/* -------------------------------------------------------------------------------- */
/*  动画任务：负责绘制进度条 + 等待硬件初始化完成                                   */
/* -------------------------------------------------------------------------------- */
void animationTask(void *pvParameters) {
  // 清屏 & 打印初始信息
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 40);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.println("Starting");
  tft.setTextSize(1);
  tft.setCursor(70, 110);
  tft.println("Version 1.0.0");

  // 画一个进度条
  int progressBarWidth  = 200;
  int progressBarHeight = 20;
  int progressX = (tft.width() - progressBarWidth) / 2;
  int progressY = 120;

  tft.fillRect(progressX, progressY, progressBarWidth, progressBarHeight, TFT_DARKGREY);

  // 等待硬件初始化完成
  // 我们可以做一个“假进度”条，每次循环 +1
  // 如果硬件初始化在这段期间结束，就在最后直接跳到100%
  bool hardwareInitDone = false;
  for(int i = 0; i <= 100; i++) {
    // 检查是否硬件初始化完成
    EventBits_t uxBits = xEventGroupGetBits(xEventGroup);
    if(uxBits & BIT_HARDWARE_INIT_DONE) {
      hardwareInitDone = true;
    }

    // 如果已经完成，直接把进度置到 100
    if(hardwareInitDone && i < 100) {
      i = 100;
    }

    // 绘制当前进度
    int filled = map(i, 0, 100, 0, progressBarWidth);
    // 先画灰色底，再画绿色填充
    tft.fillRect(progressX, progressY, progressBarWidth, progressBarHeight, TFT_DARKGREY);
    tft.fillRect(progressX, progressY, filled, progressBarHeight, TFT_GREEN);

    // 模拟刷新耗时
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  // 进度条完成，结束动画
  tft.fillScreen(TFT_BLACK);
  // 设置事件：动画结束
  xEventGroupSetBits(xEventGroup, BIT_ANIMATION_DONE);

  // 自杀本任务
  vTaskDelete(NULL);
}

/* -------------------------------------------------------------------------------- */
/*  屏幕刷新任务：采样 + 显示数值 (Core 1, 高优先级)                                */
/* -------------------------------------------------------------------------------- */
void displayTask(void *pvParameters) {
  // 初始界面：保留分隔线等基础元素
  tft.fillScreen(TFT_BLACK);
  // tft.setTextSize(2);
  tft.setTextColor(TFT_PINK, TFT_BLACK);
  tft.setCursor(0, 0);
  tft.print("Power(W):");

  // 画分界线 (不再每次重画)
  tft.drawLine(0, 68, 160, 68, TFT_CYAN);

  // 初始化前一次状态变量
  powerAbove10_prev = false;
  voltAbove10_prev = false;
  currAbove10_prev = false;

  // 主循环：数据尽可能高频率采样 & 局部刷新
  while(true) {
    // 如果没进入设置模式，就正常显示
    if(!inSettingMode) {

      if (inSettingModeLast){
            // 初始界面：保留分隔线等基础元素
        tft.fillScreen(TFT_BLACK);
        // tft.setTextSize(2);
        tft.setTextColor(TFT_PINK, TFT_BLACK);
        tft.setCursor(0, 0);
        tft.print("Power(W):");

        // 画分界线 (不再每次重画)
        tft.drawLine(0, 68, 160, 68, TFT_CYAN);
        inSettingModeLast = false;
      }
      continuousSampling();

      // 1) 计算功率并格式化
      String powerStr = formatFloat(calculatePower(), 3);
      // 2) 电压
      String voltStr = formatFloat(loadVoltage_V, 2);
      // 3) 电流
      String currStr = formatFloat(current_mA / 1000.0, 3);

      // 解析当前数值是否大于等于10
      bool powerAbove10_current = (calculatePower() >= 10.0f);
      bool voltAbove10_current = (loadVoltage_V >= 10.0f);
      bool currAbove10_current = ((current_mA / 1000.0f) >= 10.0f);

      // 检查是否有数值跨越10的阈值
      bool screenClearNeeded = false;
      if (powerAbove10_current != powerAbove10_prev ||
          voltAbove10_current != voltAbove10_prev ||
          currAbove10_current != currAbove10_prev) {
        screenClearNeeded = true;
      }

      // 更新前一次状态
      powerAbove10_prev = powerAbove10_current;
      voltAbove10_prev = voltAbove10_current;
      currAbove10_prev = currAbove10_current;

      // 如果需要清屏
      if(screenClearNeeded) {
        tft.fillScreen(TFT_BLACK);
        // 重新绘制固定元素
        tft.setTextColor(TFT_PINK, TFT_BLACK);
        tft.setCursor(0, 0);
        tft.print("Power(W):");
        tft.drawLine(0, 68, 160, 68, TFT_CYAN);
      }

      // 4) 显示功率 (大字)
      {
        // 覆盖掉上次的数字区域
        // 大约从(10,20)开始，宽120, 高40 的范围清空
        // tft.fillRect(10, 20, 120, 40, TFT_BLACK);
        tft.setTextColor(TFT_PINK, TFT_BLACK);
        tft.setTextFont(6);
        tft.setCursor(10, 20);
        tft.print(powerStr);
      }

      // 5) 显示电压
      {
        // 覆盖原电压区域
        // tft.fillRect(0, 77, 80, 30, TFT_BLACK);
        tft.setTextFont(4);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setCursor(0, 77);
        tft.print(voltStr);
        tft.print(" V");
      }

      // 6) 显示电流
      {
        // 覆盖原电流区域
        // tft.fillRect(0, 105, 80, 30, TFT_BLACK);
        tft.setTextFont(4);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setCursor(0, 105);
        tft.print(currStr);
        tft.print(" A");
      }

      // 7) 显示 multiplier
      {
        // tft.fillRect(90, 79, 70, 40, TFT_BLACK);
        tft.setTextFont(2);
        tft.setTextColor(TFT_BLUE, TFT_BLACK);
        tft.setCursor(90, 79);
        tft.print("Phi: ");
        tft.print(multiplier, 2);
      }

      // 8) 显示运行时间
      {
        unsigned long elapsedSeconds = (millis() - startTime) / 1000;
        unsigned int hours = elapsedSeconds / 3600;
        unsigned int minutes = (elapsedSeconds % 3600) / 60;
        unsigned int seconds = elapsedSeconds % 60;
        char timeBuffer[10];
        sprintf(timeBuffer, "%02d:%02d:%02d", hours, minutes, seconds);

        // 覆盖旧时间
        // tft.fillRect(90, 0, 80, 20, TFT_BLACK);
        tft.setCursor(90, 0);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setTextFont(2);
        tft.print(timeBuffer);
      }
    }
    else {
      // 设置模式下，每次按键才会刷新UI，因此此处可以根据需要加一些别的显示
      // 或保持不动
    }

    // 刷新频率尽可能高，这里示例20ms
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

/* -------------------------------------------------------------------------------- */
/*  按键监控任务：点按 * 进入/退出设置， 在设置模式下对 multiplier 进行加减          */
/* -------------------------------------------------------------------------------- */
void keyMonitorTask(void *pvParameters) {
  const TickType_t xDelay = pdMS_TO_TICKS(50);  // 50ms 扫描周期

  for(;;) {
    bool key1State = (digitalRead(KeyPin1) == LOW); // '>'
    bool key2State = (digitalRead(KeyPin2) == LOW); // '<'
    bool key3State = (digitalRead(KeyPin3) == LOW); // '#': 加
    bool key4State = (digitalRead(KeyPin4) == LOW); // '*': 减 / 进入设置

    // 未进入设置模式时，如果按下 '*'
    if(!inSettingMode) {
      if(key4State) {
        // 直接进入设置模式
        enterSettingMode();
      }
    }
    else {
      // 已在设置模式，分别处理
      if(key1State) handleSettingKeyPress(KeyPin1); // '>'
      if(key2State) handleSettingKeyPress(KeyPin2); // '<'
      if(key3State) handleSettingKeyPress(KeyPin3); // '#': 加
      if(key4State) handleSettingKeyPress(KeyPin4); // '*': 减

      // 如果按下 USER_PIN，就保存退出
      if(digitalRead(USER_PIN) == LOW) {
        exitSettingMode();
      }
    }
    vTaskDelay(xDelay);
  }
}

/* -------------------------------------------------------------------------------- */
/*  进入设置模式                                                                    */
/* -------------------------------------------------------------------------------- */
void enterSettingMode() {
  inSettingMode = true;
  inSettingModeLast = true;
  editingDigitIndex = 0;  // 默认先编辑整数部分最左侧
  updateSettingUI();
}

/* -------------------------------------------------------------------------------- */
/*  退出设置模式并保存 multiplier 到 NVS                                            */
/* -------------------------------------------------------------------------------- */
void exitSettingMode() {
  inSettingMode = false;
  // 保存到 NVS
  prefs.begin("my_app", false);
  prefs.putFloat("multiplier", multiplier);
  prefs.end();

  // 退出后，主界面不清屏，继续显示即可
  Serial.print("New multiplier saved: ");
  Serial.println(multiplier);
}

/* -------------------------------------------------------------------------------- */
/*  在设置模式下，处理按键                                                          */
/* -------------------------------------------------------------------------------- */
void handleSettingKeyPress(int keyPin) {
  static unsigned long lastPressTime = 0;
  unsigned long now = millis();
  // 简单防抖
  if(now - lastPressTime < 150) {
    return;
  }
  lastPressTime = now;

  // 把 multiplier 转为字符串
  char buf[16];
  dtostrf(multiplier, 1, 3, buf);  // 如 "1.220"
  int len = strlen(buf);

  switch(keyPin) {
    case KeyPin1: // '>' -> 光标右移
      editingDigitIndex++;
      if(editingDigitIndex >= len) editingDigitIndex = len-1;
      break;

    case KeyPin2: // '<' -> 光标左移
      editingDigitIndex--;
      if(editingDigitIndex < 0) editingDigitIndex = 0;
      break;

    case KeyPin3: // '#' -> 加
      if(buf[editingDigitIndex] == '.') break; // 小数点跳过
      if(buf[editingDigitIndex] < '9') {
        buf[editingDigitIndex]++;
      }
      break;

    case KeyPin4: // '*' -> 减
      if(buf[editingDigitIndex] == '.') break; // 小数点跳过
      if(buf[editingDigitIndex] > '0') {
        buf[editingDigitIndex]--;
      }
      break;
  }

  // 更新 multiplier
  multiplier = atof(buf);
  // 刷新设置界面
  updateSettingUI();
}

/* -------------------------------------------------------------------------------- */
/*  更新设置界面                                                                    */
/* -------------------------------------------------------------------------------- */
void updateSettingUI() {
  // 清屏或局部覆盖都可以；为了简单，这里整屏清空再画一次设置界面
  tft.fillScreen(TFT_BLACK);

  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(0, 0);
  tft.println("Setting Mode");
  tft.println("Press <, > to move digit");
  tft.println("Press * to - / # to +");
  tft.println("USER_PIN to save&exit");

  // 重新把 multiplier 转成字符串显示
  char buf[16];
  dtostrf(multiplier, 1, 3, buf);
  int len = strlen(buf);

  // 在屏幕较下方显示，可调整位置
  int startX = 10;
  int startY = 80;
  tft.setCursor(startX, startY);

  for(int i = 0; i < len; i++) {
    if(i == editingDigitIndex) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
    } else {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    tft.print(buf[i]);
  }
}

/* -------------------------------------------------------------------------------- */
/*  工具函数：计算功率                                                              */
/* -------------------------------------------------------------------------------- */
float calculatePower() {
  return loadVoltage_V * (current_mA / 1000.0f) * multiplier; 
}

/* -------------------------------------------------------------------------------- */
/*  工具函数：重置时间                                                              */
/* -------------------------------------------------------------------------------- */
void resetTime() {
  startTime = millis();
}

/* -------------------------------------------------------------------------------- */
/*  采样 INA226                                                                    */
/* -------------------------------------------------------------------------------- */
void continuousSampling() {
  ina226.readAndClearFlags();
  shuntVoltage_mV = ina226.getShuntVoltage_mV();
  busVoltage_V    = ina226.getBusVoltage_V();
  current_mA      = ina226.getCurrent_mA();
  power_mW        = ina226.getBusPower();
  loadVoltage_V   = busVoltage_V + (shuntVoltage_mV / 1000.0);

  // 可选的调试输出
  // Serial.print("Bus Voltage [V] : "); Serial.println(busVoltage_V);
  // Serial.print("Load Voltage[V] : "); Serial.println(loadVoltage_V);
  // Serial.print("Current   [mA] : "); Serial.println(current_mA);
  // Serial.print("Power     [mW] : "); Serial.println(power_mW);
  // ...
}

/* -------------------------------------------------------------------------------- */
/*  工具函数：格式化浮点数                                                          */
/* -------------------------------------------------------------------------------- */
String formatFloat(float num, int decimalPlaces) {
  if (num < 0) {
    num = 0;
  }
  String formatString = String("%0." + String(decimalPlaces) + "f");
  char buffer[32];
  snprintf(buffer, sizeof(buffer), formatString.c_str(), num);
  return String(buffer);
}