#include "CommandProcessor.h"
#include "CommChannel.h"
#include "EncoderHandler.h"
#include "MotorController.h"
#include "PWMManager.h"
#include "ServoController.h"
#include "WebServerHandler.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>

// 從機模式（env:esp32dev_i2c）：掛 i2cESP32 主機匯流排，WiFi 完全不啟動。
// 兩者互斥——WiFi task（prio 23）會搶佔 i2c_slave_task（prio 20），
// 造成主機 bus recovery。見 docs/i2c_slave_sdd.md §7。
#ifndef I2C_SLAVE_MODE
#define I2C_SLAVE_MODE 0
#endif

#if I2C_SLAVE_MODE
#include "I2CSlaveBridge.h"
#endif

const int servo1Pin = SERVO1_PIN;
const int servo2Pin = SERVO2_PIN;

// 建立全域物件
PWMManager pwmManager;
MotorController motorController;
ServoController servoController(servo1Pin, servo2Pin);
EncoderHandler encoderHandler;
CommandProcessor cmdProcessor(motorController, servoController, encoderHandler,
                              pwmManager);
WebServerHandler webServerHandler(cmdProcessor);

#if I2C_SLAVE_MODE
I2CSlaveBridge i2cBridge(cmdProcessor);

// -- 8.1 WiFi/I2C 共存實驗 --------------------------------------------
// 見 robot repo 的 docs/architecture/blockly_module_contract_sdd.md 8.1。
//
// 待驗證的假設：Arduino core 2.0.17 的 i2c_slave_task 優先權 20，會被 WiFi task
// （優先權 23）搶佔。關鍵在 esp32-hal-i2c-slave.c 的 TX 分支——callback 回來之後
// 才呼叫 i2c_ll_stretch_clr()，也就是說 task 沒被排到時，從機硬體是「一直拉低
// SCL 做 clock stretching」，主機的交易整個卡在匯流排上。這才是 2026-07-07 實測
// 主機 loop time 1-2s 的機制。
//
// 該 task 的 TX 路徑只是把預先備妥的 txBuffer_ 寫出去（微秒級），平時阻塞在
// xQueueReceive(portMAX_DELAY)，因此把它提到 WiFi 之上不應餓死 WiFi。
//
// 三組設定用序列指令即時切換，同一顆韌體、同一次開機量完：
//   （開機預設）WiFi 未啟動、優先權 20  -> 基準線
//   EXP PRIO 24                        -> 對照組（WiFi 仍未啟動）
//   EXP WIFI ON                        -> 實驗組（WiFi + 高優先權）
//   EXP PRIO 20                        -> SDD 記錄的失效組態
static bool expWifiStarted = false;

static void expReportI2cTaskPriority(const char *tag) {
  TaskHandle_t handle = xTaskGetHandle("i2c_slave_task");
  if (handle == nullptr) {
    Serial.printf("[EXP] %s: i2c_slave_task 找不到\n", tag);
    return;
  }
  Serial.printf("[EXP] %s: i2c_slave_task prio=%u\n", tag,
                (unsigned)uxTaskPriorityGet(handle));
}

static void expSetI2cTaskPriority(int prio) {
  if (prio < 0 || prio >= configMAX_PRIORITIES) {
    Serial.printf("[EXP] prio %d 超出範圍 0..%d\n", prio,
                  (int)configMAX_PRIORITIES - 1);
    return;
  }
  TaskHandle_t handle = xTaskGetHandle("i2c_slave_task");
  if (handle == nullptr) {
    Serial.println("[EXP] i2c_slave_task 找不到，優先權未變更");
    return;
  }
  const UBaseType_t before = uxTaskPriorityGet(handle);
  vTaskPrioritySet(handle, (UBaseType_t)prio);
  Serial.printf("[EXP] i2c_slave_task prio %u -> %u（WiFi task = 23）\n",
                (unsigned)before, (unsigned)uxTaskPriorityGet(handle));
}

static void expHandleCommand(const String &line) {
  String rest = line.substring(3);
  rest.trim();
  // 動詞比對用大寫，但參數（SSID/密碼）必須保留原始大小寫。
  String upper = rest;
  upper.toUpperCase();

  if (upper == "REBOOT") {
    Serial.println("[EXP] 重新開機（取得乾淨基準線）");
    Serial.flush();
    delay(50);
    ESP.restart();
    return;
  }
  if (upper.startsWith("WIFI STA ")) {
    // 憑證由序列指令帶入，不寫進原始碼。純 STA、不起 AP 與 Web 伺服器，
    // 目的是單獨隔離出 WiFi task 對 i2c_slave_task 的排程影響。
    String args = rest.substring(9);
    args.trim();
    const int space = args.indexOf(' ');
    if (space <= 0) {
      Serial.println("[EXP] 用法：EXP WIFI STA <ssid> <password>");
      return;
    }
    const String ssid = args.substring(0, space);
    String pass = args.substring(space + 1);
    pass.trim();
    Serial.printf("[EXP] 連線 STA ssid=%s（密碼 %u 字元）\n", ssid.c_str(),
                  (unsigned)pass.length());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    const unsigned long deadline = millis() + 15000;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
      delay(200);
    }
    if (WiFi.status() == WL_CONNECTED) {
      expWifiStarted = true;
      Serial.printf("[EXP] STA 已連線 ip=%s rssi=%d\n",
                    WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    } else {
      Serial.println("[EXP] STA 連線逾時——本階段數據不可用");
    }
    expReportI2cTaskPriority("STA 連線後");
    return;
  }
  if (upper == "WIFI ON") {
    // 不設「已啟動就忽略」的守衛：EXP WIFI STA 也會把 expWifiStarted 設起來，
    // 擋掉這裡就永遠升級不到 AP_STA + Web 伺服器的完整堆疊。實驗腳本因此量到
    // 「標示為完整堆疊、實際只有純 STA」的假數據，兩輪都被騙。
    Serial.println("[EXP] === 啟動 WiFi + Web 伺服器（完整堆疊）===");
    webServerHandler.begin();
    expWifiStarted = true;
    expReportI2cTaskPriority("WiFi 啟動後");
    return;
  }
  if (upper == "WIFI OFF") {
    WiFi.mode(WIFI_OFF);
    expWifiStarted = false;
    Serial.println("[EXP] WiFi 已關閉（Web 伺服器物件仍在，但無流量）");
    return;
  }
  if (upper.startsWith("PRIO")) {
    String value = upper.substring(4);
    value.trim();
    expSetI2cTaskPriority(value.toInt());
    return;
  }
  if (upper == "STATUS") {
    Serial.printf("[EXP] wifi_started=%d wifi_mode=%d sta_connected=%d rssi=%d\n",
                  expWifiStarted ? 1 : 0, (int)WiFi.getMode(),
                  WiFi.status() == WL_CONNECTED ? 1 : 0, (int)WiFi.RSSI());
    expReportI2cTaskPriority("目前");
    I2CSlaveBridge::Stats st = i2cBridge.getStats();
    Serial.printf("[EXP] i2c rx=%lu bad_cmd=%lu not_ready=%lu dropped=%lu\n",
                  (unsigned long)st.rxCount, (unsigned long)st.badCmdCount,
                  (unsigned long)st.notReadyCount, (unsigned long)st.droppedCount);
    return;
  }
  Serial.println("[EXP] 用法：EXP REBOOT / EXP WIFI STA <ssid> <pw> / EXP WIFI ON|OFF / EXP PRIO <0-24> / EXP STATUS");
}
#endif

// JSON 處理任務
void jsonTask(void *pvParameters) {
  for (;;) {
    static unsigned long lastPrint = 0;

#if !I2C_SLAVE_MODE
    // 檢查 WebSocket 連接超時
    webServerHandler.checkWebSocketTimeout();
#endif

    // 在安全點套用 WS/序列任務解析好的新 PROG 程式（跨核心交接，避免向量懸空）
    cmdProcessor.applyPendingProgramIfAny();

#if I2C_SLAVE_MODE
    // 同一個「安全點落地」原則：I2C callback 只解析排隊，動作在此執行。
    i2cBridge.service();
#endif

    // 只有在 Serial 控制模式未啟用時才執行 PROG 模式 loop 指令
    if (!webServerHandler.isSerialMode()) {
      cmdProcessor.executeLoopCommands();
      cmdProcessor.updateAngleControl(); // 角度控制每拍都跑（不再因新指令而跳拍）
    }
    if (millis() - lastPrint > 1000) {
      lastPrint = millis();
#if I2C_SLAVE_MODE
      // 診斷只能在主迴圈列印：I2C callback 內禁止 Serial（SDD §7.1）
      static uint32_t lastRxCount = 0;
      I2CSlaveBridge::Stats st = i2cBridge.getStats();
      if (st.rxCount != lastRxCount) {
        lastRxCount = st.rxCount;
        Serial.printf(
            "[I2C] rx=%lu bad_cmd=%lu not_ready=%lu dropped=%lu last=0x%02X\n",
            (unsigned long)st.rxCount, (unsigned long)st.badCmdCount,
            (unsigned long)st.notReadyCount, (unsigned long)st.droppedCount,
            st.lastCommand);
      }
#endif
    }
    vTaskDelay(10 / portTICK_PERIOD_MS); // 高頻率運行
  }
}

// 新的任務，專門處理 Serial 指令解析與執行
void serialTask(void *pvParameters) {
  vTaskDelay(100 / portTICK_PERIOD_MS); // 開機初期延遲，避免搶資源
  for (;;) {
    if (Serial.available()) {
      String line = Serial.readStringUntil('\n');
      line.trim();
      if (line.length() == 0)
        continue; // 跳過空行或雜訊
      // JSON cmd 行（{...}）與 WebSocket 共用同一個 router，回應走 SERIAL 通道；
      // 不受 serialMode 限制，USB 不需先連 WiFi 即可下 JSON 指令。
      if (line.startsWith("{")) {
        cmdProcessor.processCommands(line, Comm::CH_SERIAL);
#if I2C_SLAVE_MODE
        // 共存實驗指令不受 serialMode 閘門限制——從機模式下 serialMode 為 false，
        // 掛在閘門後面會永遠收不到。
      } else if (line.startsWith("EXP")) {
        expHandleCommand(line);
#endif
      } else if (webServerHandler.isSerialMode()) {
        String jsonStr;
        if (line.startsWith("M")) {
          int firstComma = line.indexOf(',');
          int secondComma = line.indexOf(',', firstComma + 1);
          int motor = line.substring(1, firstComma).toInt();
          char direction = line.charAt(firstComma + 1);
          int speed = line.substring(secondComma + 1).toInt();
          motorController.controlMotor(motor, direction, speed);
          Serial.println("OK");
        } else if (line.startsWith("S")) {
          int firstComma = line.indexOf(',');
          int servo = line.substring(1, firstComma).toInt();
          int angle = line.substring(firstComma + 1).toInt();
          servoController.controlServo(servo, angle);
          Serial.println("OK");
        } else if (line.startsWith("READ,")) {
          // 先用逗號分割
          int comma1 = line.indexOf(',');
          int comma2 = line.indexOf(',', comma1 + 1);
          int comma3 = line.indexOf(',', comma2 + 1);

          String cmd = line.substring(0, comma1); // "READ"
          String type =
              (comma2 == -1)
                  ? ""
                  : line.substring(comma1 + 1, comma2); // "D"、"A"、"DP"、"US"
          String param1 =
              (comma2 == -1)
                  ? ""
                  : ((comma3 == -1) ? line.substring(comma2 + 1)
                                    : line.substring(comma2 + 1, comma3));
          String param2 = (comma3 == -1) ? "" : line.substring(comma3 + 1);

          if (type == "D") {
            int pin = param1.toInt();
            pinMode(pin, INPUT);
            int val = digitalRead(pin);
            Serial.printf("D%d=%d\n", pin, val);
          } else if (type == "DP") {
            int pin = param1.toInt();
            pinMode(pin, INPUT_PULLUP);
            int val = digitalRead(pin);
            Serial.printf("DP%d=%d\n", pin, val);
          } else if (type == "A") {
            int pin = param1.toInt();
            int val = analogRead(pin);
            Serial.printf("A%d=%d\n", pin, val);
          } else if (type == "US") {
            int trigPin = param1.toInt();
            int echoPin = param2.toInt();
            pinMode(trigPin, OUTPUT);
            pinMode(echoPin, INPUT);
            digitalWrite(trigPin, LOW);
            delayMicroseconds(2);
            digitalWrite(trigPin, HIGH);
            delayMicroseconds(10);
            digitalWrite(trigPin, LOW);
            long duration = pulseIn(echoPin, HIGH, 30000);
            long distance = duration * 0.034 / 2;
            Serial.printf("US,%d,%d=%ld\n", trigPin, echoPin, distance);
          } else {
            Serial.println("無效的 READ 指令格式");
          }
        } else {
          Serial.println("無效的指令格式");
        }
        // 指令處理完後，清空序列緩衝區，避免殘留舊值
        while (Serial.available())
          Serial.read();
      }
    }

    // 高頻率 Serial 處理 (用於網頁和Serial控制)
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000); // 等待序列埠穩定

  Serial.println("");
  Serial.println("========================================");
  Serial.println("       ESP32 控制系統啟動中...        ");
  Serial.println("========================================");

  Serial.println("[初始化] PWM 管理器...");
  pwmManager.initChannels();

  Serial.println("[初始化] 馬達控制器...");
  motorController.begin();

  Serial.println("[初始化] 舵機控制器...");
  servoController.initServo();

  Serial.println("[初始化] 編碼器處理器...");
  encoderHandler.begin();

  // 建立回呼讓 CommandProcessor 能回傳 JSON
  cmdProcessor.setSendJsonResponseCallback(WebServerHandler::sendJsonResponse);

#if I2C_SLAVE_MODE
  // 從機模式：不啟動 WiFi 與 Web 伺服器（見檔頭說明）。
  Serial.println("[初始化] I2C 從機橋接層...");
  i2cBridge.begin(I2C_SLAVE_ADDRESS, I2C_SLAVE_SDA_PIN, I2C_SLAVE_SCL_PIN);
  Serial.printf("[I2C] slave addr=0x%02X sda=%d scl=%d freq=%lu (WiFi 未啟動)\n",
                I2C_SLAVE_ADDRESS, I2C_SLAVE_SDA_PIN, I2C_SLAVE_SCL_PIN,
                (unsigned long)I2C_SLAVE_FREQ);
  expReportI2cTaskPriority("開機基準");
  Serial.println("[EXP] 共存實驗：EXP REBOOT / EXP WIFI STA <ssid> <pw> / EXP WIFI ON|OFF / EXP PRIO <n> / EXP STATUS");
#else
  Serial.println("[初始化] Web 伺服器...");
  webServerHandler.begin();

  // 開機 autorun:若 LittleFS 有存檔且 autorun flag 開,自動載入並執行
  webServerHandler.runAutorunProgramIfEnabled();
#endif

  // 建立一個新任務，將 JSON 處理工作分配給 core1
  xTaskCreatePinnedToCore(jsonTask,   // 任務函式
                          "JSONTask", // 任務名稱
                          8192,       // 堆疊大小
                          NULL,       // 傳入參數
                          2,          // 任務優先權
                          NULL, // 任務句柄（不需要時可設為 NULL）
                          1);   // 指定執行在 core1

  // 建立一個新任務，將 Serial 處理工作分配給 core1，並設定較低的優先權（例如
  // priority = 1）
  xTaskCreatePinnedToCore(serialTask,   // 任務函式
                          "SerialTask", // 任務名稱
                          8192,         // 堆疊大小
                          NULL,         // 傳入參數
                          1,            // 任務優先權
                          NULL,         // 任務句柄
                          1);           // 指定執行在 core1

  Serial.println("[初始化] 建立多核心任務...");

  Serial.println("========================================");
  Serial.println("    系統啟動完成！等待連線...         ");
  Serial.println("========================================");
}

void loop() {
  // 主 loop 不再處理 Serial 指令，僅保留最小延遲
  delay(10);
}
