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

// JSON 處理任務
void jsonTask(void *pvParameters) {
  for (;;) {
    static unsigned long lastPrint = 0;

    // 檢查 WebSocket 連接超時
    webServerHandler.checkWebSocketTimeout();

    // 在安全點套用 WS/序列任務解析好的新 PROG 程式（跨核心交接，避免向量懸空）
    cmdProcessor.applyPendingProgramIfAny();

    // 只有在 Serial 控制模式未啟用時才執行 PROG 模式 loop 指令
    if (!webServerHandler.isSerialMode()) {
      cmdProcessor.executeLoopCommands();
      cmdProcessor.updateAngleControl(); // 角度控制每拍都跑（不再因新指令而跳拍）
    }
    if (millis() - lastPrint > 1000) {
      lastPrint = millis();
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

  Serial.println("[初始化] Web 伺服器...");
  webServerHandler.begin();

  // 開機 autorun:若 LittleFS 有存檔且 autorun flag 開,自動載入並執行
  webServerHandler.runAutorunProgramIfEnabled();

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
