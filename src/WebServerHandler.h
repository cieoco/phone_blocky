#ifndef WEB_SERVER_HANDLER_H
#define WEB_SERVER_HANDLER_H

#include "CommandProcessor.h"
#include "CommChannel.h"
#include "config.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <cstring>

class WebServerHandler {
private:
  AsyncWebServer server;
  AsyncWebSocket ws;
  CommandProcessor &cmdProcessor;
  Preferences preferences;
  bool serialMode = false;

  // WebSocket 連接監控相關
  unsigned long lastWebSocketMessage = 0;
  bool webSocketConnected = false;
  const unsigned long WEBSOCKET_TIMEOUT = 3000; // 3秒超時

  // /api/program POST 累積緩衝 (XML 可能大於單一 chunk)
  String programPostBuffer;

public:
  // static 讓外部可用 sendJsonResponse
  static WebServerHandler *instance;

  WebServerHandler(CommandProcessor &processor)
      : server(80), ws("/ws"), cmdProcessor(processor) {
    instance = this;
    // 這裡不再直接用 ssid/password，改用 Preferences 讀取
  }

  // 系統控制相關的處理函數聲明
  void handleSystemRestart(AsyncWebServerRequest *request);

  // 馬達 PID 與 SysId 相關
  void handleMotorPIDGet(AsyncWebServerRequest *request);
  void handleMotorPIDPost(AsyncWebServerRequest *request, uint8_t *data,
                          size_t len);
  void handleMotorSysIdPost(AsyncWebServerRequest *request, uint8_t *data,
                            size_t len);
  void handleMotorSysIdResultsGet(AsyncWebServerRequest *request);
  void handleMotorHWApply(AsyncWebServerRequest *request, uint8_t *data,
                          size_t len);
  void handleMotorPIDApply(AsyncWebServerRequest *request, uint8_t *data,
                           size_t len);
  void loadPIDSettings();

  // 已儲存程式 (LittleFS) 相關 — Blockly 與 AI 共用
  void handleProgramGet(AsyncWebServerRequest *request);
  void handleProgramPost(AsyncWebServerRequest *request, uint8_t *data,
                         size_t len, size_t index, size_t total);
  void handleProgramDelete(AsyncWebServerRequest *request);
  void handleProgramAutorunGet(AsyncWebServerRequest *request);
  void handleProgramAutorunPost(AsyncWebServerRequest *request, uint8_t *data,
                                size_t len);
  // 開機 autorun：載入並執行 NVS 裡的存檔程式。
  //
  // 只依賴 ProgramStore（NVS）與 cmdProcessor，**不需要 Web 伺服器已啟動**，
  // 因此 I2C 從機模式（沒有 WiFi、沒有 begin()）同樣可以呼叫。從機模式下必須
  // 呼叫：主機能讀功能表、也能經 0x65 設定功能值，但沒有程式在跑就沒有人去
  // 消費那些值，自定回授動作等於是死的，得有人開網頁按一次「執行」才活。
  //
  // channels 決定回應往哪送。從機模式沒有 WebSocket，應傳 Comm::CH_SERIAL，
  // 否則結果只會寫進一個沒人聽的 ws。
  void runAutorunProgramIfEnabled(uint8_t channels = Comm::CH_WS);

  // WebSocket 連接監控相關函數
  void stopAllMotors();
  void checkWebSocketTimeout();

  // 提供靜態函式以供 CommandProcessor 使用
  // 通訊層唯一知道實體線路怎麼寫的地方：依 channels 位元遮罩把回應送往
  // WebSocket 與／或 Serial。CommandProcessor 只給遮罩、不認識傳輸。
  static void sendJsonResponse(const DynamicJsonDocument &doc, uint8_t channels) {
    String out;
    serializeJson(doc, out);
    if ((channels & Comm::CH_WS) && instance) {
      instance->ws.textAll(out);
    }
    if (channels & Comm::CH_SERIAL) {
      Serial.println(out);
    }
  }

  void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                        AwsEventType type, void *arg, uint8_t *data,
                        size_t len) {
    switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket 用戶端 #%u 已連接\n", client->id());
      webSocketConnected = true;
      lastWebSocketMessage = millis();
      break;

    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket 用戶端 #%u 已斷線\n", client->id());
      webSocketConnected = false;
      // 立即停止所有馬達
      stopAllMotors();
      break;

    case WS_EVT_DATA: {
      String msg;
      for (size_t i = 0; i < len; i++) {
        msg += (char)data[i];
      }
      // 更新最後接收訊息的時間
      lastWebSocketMessage = millis();

      cmdProcessor.processCommands(msg, Comm::CH_WS);

      // 在收到成功回應後，重置 waitingForResponse 以允許新的訊息送出
      if (msg.indexOf("message_success") >= 0) {
        cmdProcessor.resetWaitingForResponse();
      } else {
        // 回傳按鈕成功訊息
        DynamicJsonDocument respDoc(256);
        respDoc["status"] = "success";
        respDoc["message"] = "指令已接收";
        sendJsonResponse(respDoc, Comm::CH_WS);
      }
    } break;

    default:
      break;
    }
  }

  void begin() {
    if (!LittleFS.begin(true)) {
      Serial.println("LittleFS Mount Failed");
      return;
    }

    // 讀取 WiFi 設定
    preferences.begin("wifi", true);
    String wifiSSID = preferences.getString("ssid", "");
    String wifiPassword = preferences.getString("password", "");
    preferences.end();
    if (wifiSSID.length() == 0 && strlen(CONFIG_STA_SSID) > 0) {
      wifiSSID = CONFIG_STA_SSID;
    }
    if (wifiPassword.length() == 0 && strlen(CONFIG_STA_PASSWORD) > 0) {
      wifiPassword = CONFIG_STA_PASSWORD;
    }
    if (wifiSSID.length() > 0 && wifiPassword.length() > 0) {
      preferences.begin("wifi", false);
      if (!preferences.isKey("ssid")) {
        preferences.putString("ssid", wifiSSID);
      }
      if (!preferences.isKey("password")) {
        preferences.putString("password", wifiPassword);
      }
      preferences.end();
    }

    // 讀取馬達 PID 設定
    loadPIDSettings();

    // WiFi 連線
    WiFi.mode(WIFI_AP_STA);
    if (wifiSSID.length() > 0 && wifiPassword.length() > 0) {
      WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
      Serial.println("嘗試連線至路由器...");

      unsigned long startAttemptTime = millis();
      const unsigned long wifiTimeout = 10000; // 10秒

      while (WiFi.status() != WL_CONNECTED &&
             millis() - startAttemptTime < wifiTimeout) {
        delay(500);
        Serial.print(".");
      }
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nSTA 連線成功, IP位址: ");
        Serial.println(WiFi.localIP());
      }
    }

    // 啟動 AP

    String apName = getAPName();
    if (apName.length() == 0 && strlen(CONFIG_AP_NAME) > 0) {
      apName = CONFIG_AP_NAME;
    }
    // 顯示AP名稱時,加入STA的IP位址
    if (WiFi.status() == WL_CONNECTED) {
      apName += " (" + WiFi.localIP().toString() + ")";
    }

    // 啟動 AP，設定更多參數確保相容性
    const char *apPassword = CONFIG_AP_PASSWORD;
    if (apPassword == nullptr || strlen(apPassword) == 0) {
      apPassword = "12345678";
    }
    bool apResult = WiFi.softAP(apName.c_str(), apPassword, 1, 0, 4);
    if (apResult) {
      Serial.printf("AP 啟動成功 - Name: %s\n", apName.c_str());
      Serial.print("AP IP: ");
      Serial.println(WiFi.softAPIP());
      Serial.printf("AP MAC: %s\n", WiFi.softAPmacAddress().c_str());
    } else {
      Serial.println("AP 啟動失敗！");
    }

    // 系統重啟API
    server.on("/api/system/restart", HTTP_POST,
              [this](AsyncWebServerRequest *request) {
                handleSystemRestart(request);
              });

    // 馬達 PID 與 SysId API
    server.on(
        "/api/motor/pid", HTTP_GET,
        [this](AsyncWebServerRequest *request) { handleMotorPIDGet(request); });

    server.on(
        "/api/motor/pid", HTTP_POST, [this](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len,
               size_t index,
               size_t total) { handleMotorPIDPost(request, data, len); });

    server.on(
        "/api/motor/sysid", HTTP_POST,
        [this](AsyncWebServerRequest *request) {}, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len,
               size_t index,
               size_t total) { handleMotorSysIdPost(request, data, len); });

    server.on("/api/motor/sysid/results", HTTP_GET,
              [this](AsyncWebServerRequest *request) {
                handleMotorSysIdResultsGet(request);
              });

    server.on(
        "/api/motor/hw/apply", HTTP_POST,
        [this](AsyncWebServerRequest *request) {}, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len,
               size_t index,
               size_t total) { handleMotorHWApply(request, data, len); });

    server.on(
        "/api/motor/pid/apply", HTTP_POST,
        [this](AsyncWebServerRequest *request) {}, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len,
               size_t index,
               size_t total) { handleMotorPIDApply(request, data, len); });

    // 已儲存程式 (Blockly + AI 共用 NVS)
    //
    // ⚠ 註冊順序有意義，不要把 /api/program 移到 /api/program/autorun 之前。
    //
    // ESPAsyncWebServer 的 URI 比對不是精確比對（WebHandlerImpl.h）：
    //     if (_uri != request->url() && !request->url().startsWith(_uri + "/"))
    //         return false;
    // 也就是註冊 `/api/program` 的處理器會**同時吃掉所有 `/api/program/...`**，
    // 而處理器是依註冊順序比對、先中先贏。
    //
    // 這個順序寫反過的後果（2026-08-25 實際踩到）：POST /api/program/autorun
    // 被 /api/program 的處理器接走，handleProgramPost() 在 body 裡找不到 json
    // 欄位，回 {"error":"missing 'json' object"} —— 錯誤訊息完全指向存檔功能，
    // 跟 autorun 一點關係都沒有，非常難查。GET 同樣被吃掉。
    //
    // ai.html 的開關看起來能用，是因為它的狀態讀自 GET /api/program 回應裡的
    // autorun 欄位，繞過了這條壞掉的路由，所以這個 bug 一直沒被發現。
    server.on("/api/program/autorun", HTTP_GET,
              [this](AsyncWebServerRequest *r) {
                handleProgramAutorunGet(r);
              });

    server.on(
        "/api/program/autorun", HTTP_POST,
        [this](AsyncWebServerRequest *r) {}, NULL,
        [this](AsyncWebServerRequest *r, uint8_t *data, size_t len,
               size_t index, size_t total) {
          handleProgramAutorunPost(r, data, len);
        });

    server.on("/api/program", HTTP_GET,
              [this](AsyncWebServerRequest *r) { handleProgramGet(r); });

    server.on(
        "/api/program", HTTP_POST,
        [this](AsyncWebServerRequest *r) {}, NULL,
        [this](AsyncWebServerRequest *r, uint8_t *data, size_t len,
               size_t index, size_t total) {
          handleProgramPost(r, data, len, index, total);
        });

    server.on("/api/program", HTTP_DELETE,
              [this](AsyncWebServerRequest *r) { handleProgramDelete(r); });

    // 靜態檔案服務
    server.serveStatic("/", LittleFS, "/")
        .setDefaultFile("index.html")
        .setCacheControl("max-age=86400");
    server.serveStatic("/joy", LittleFS, "/joy.html")
        .setCacheControl("max-age=3600");

    // 新增 WiFi 設定頁面
    server.serveStatic("/set", LittleFS, "/set.html")
        .setCacheControl("max-age=3600");

    // 新增 WiFi 設定 POST handler
    server.on("/setwifi", HTTP_POST, [this](AsyncWebServerRequest *request) {
      String ssid = request->arg("ssid");
      String password = request->arg("password");
      String apName = request->arg("name");
      preferences.begin("wifi", false);
      preferences.putString("ssid", ssid);
      preferences.putString("password", password);
      preferences.putString("apName", apName);
      preferences.end();
      request->send(200, "text/html; charset=utf-8",
                    "WiFi 與 AP 名稱已儲存，裝置即將重啟...");
      delay(1000);
      ESP.restart();
    });

    // 新增僅設定控制板名稱 POST handler
    server.on("/setnameonly", HTTP_POST,
              [this](AsyncWebServerRequest *request) {
                String apName = request->arg("name");
                if (apName.length() > 0) {
                  preferences.begin("wifi", false);
                  preferences.putString("apName", apName);
                  preferences.end();
                  request->send(200, "text/html; charset=utf-8",
                                "控制板名稱已儲存，裝置即將重啟...");
                  delay(1000);
                  ESP.restart();
                } else {
                  request->send(400, "text/html; charset=utf-8",
                                "錯誤：控制板名稱不能為空");
                }
              });

    // 新增進入 Serial 控制模式的處理器
    server.on("/serialmode/enable", HTTP_POST,
              [this](AsyncWebServerRequest *request) {
                serialMode = true;
                // 印出目前的 Serial 控制模式狀態
                Serial.println("Serial 控制模式已啟用。");
                request->send(
                    200, "text/html; charset=utf-8",
                    "已進入 Serial 控制模式，請用電腦端序列埠工具連線。");
              });

    // 新增停用 Serial 控制模式的處理器
    server.on("/serialmode/disable", HTTP_POST,
              [this](AsyncWebServerRequest *request) {
                serialMode = false;
                // 印出目前的 Serial 控制模式狀態
                Serial.println("Serial 控制模式已停用。");
                request->send(200, "text/html; charset=utf-8",
                              "已離開 Serial 控制模式。");
              });

    server.begin();
    ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
      onWebSocketEvent(server, client, type, arg, data, len);
    });
    server.addHandler(&ws);
  }

  // 取得預設 AP 名稱（MAC 末四碼）
  String getDefaultAPName() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    return "ESP32-" + mac.substring(mac.length() - 4);
  }

  // 讀取 AP 名稱
  String getAPName() {
    preferences.begin("wifi", true);
    String apName = preferences.getString("apName", "");
    preferences.end();
    if (apName.length() == 0) {
      apName = getDefaultAPName();
    }
    return apName;
  }

  // 儲存 AP 名稱
  void saveAPName(const String &apName) {
    preferences.begin("wifi", false);
    preferences.putString("apName", apName);
    preferences.end();
  }

  bool isSerialMode() const { return serialMode; }
};

#endif
