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
  bool programPostRejected = false;   // 本次 POST 已因過大而回應，忽略後續 chunk

  // STA 憑證留一份在 RAM：自動重連要重新呼叫 WiFi.begin()，/api/wifi 也要回報
  // 目前設定的是哪個 SSID。密碼只進得去、不出得來（/api/wifi 一律不回傳）。
  String staSsid;
  String staPassword;
  unsigned long staRetryAt = 0;        // 下次重試的時刻（0 = 無待辦）
  unsigned long staRetryBackoff = 0;   // 目前退避間隔
  bool staWasConnected = false;        // 用來只在狀態「轉變」時印 log
  unsigned long restartAt = 0;         // 延遲重開機（0 = 不重開）
  String broadcastApName;              // softAP() 實際用的名稱（開機時定案）

  static constexpr unsigned long STA_RETRY_MIN_MS = 5000;
  static constexpr unsigned long STA_RETRY_MAX_MS = 60000;
  static constexpr size_t kMaxSsidBytes = 32;   // 802.11 SSID 上限，超過 softAP() 直接失敗

  // NVS 寫入驗證（CLAUDE.md 強制規範 #5）。
  //
  // 為什麼不看 putString 的回傳值就好：空字串時它回傳 0，而失敗時也回傳 0,
  // 兩者無法區分 —— 而「密碼存成空的」正好是會讓 STA 整段被跳過的情況。
  // 寫完讀回來比對是唯一能把這兩者分開的做法。呼叫前命名空間必須已開啟。
  bool putStringVerified(const char *key, const String &value) {
    preferences.putString(key, value);
    // 「寫完之後 NVS 真的是我們要的值嗎」——這個問法對空字串也成立，
    // 不必靠哨兵值去區分「回傳 0 = 成功寫入空字串」和「回傳 0 = 失敗」。
    return preferences.isKey(key) && preferences.getString(key, String()) == value;
  }

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
      // AsyncWebSocket::count() 只計算 WS_CONNECTED client。關閉其中一個
      // Blockly／搖桿分頁時，其他仍在線的控制頁不應被誤判為全部斷線。
      webSocketConnected = server && server->count() > 0;
      if (!webSocketConnected) {
        // 最後一個 WebSocket client 離線才停止所有馬達。
        stopAllMotors();
      }
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
    // 把 config.h 的預設憑證播種進 NVS（只在 NVS 還沒有這把 key 時）。
    // 規範 #5：寫入要驗證。這裡失敗不影響「這一次開機」——下面用的是 RAM 裡的
    // 副本——但會讓設定無法留到下次，所以必須講出來，不能無聲無息。
    if (wifiSSID.length() > 0 && wifiPassword.length() > 0) {
      if (!preferences.begin("wifi", false)) {
        Serial.println("[WiFi] 無法開啟 NVS 'wifi' 命名空間，預設憑證沒有寫入");
      } else {
        if (!preferences.isKey("ssid") && !putStringVerified("ssid", wifiSSID)) {
          Serial.println("[WiFi] config.h 的 SSID 寫入 NVS 失敗");
        }
        if (!preferences.isKey("password") &&
            !putStringVerified("password", wifiPassword)) {
          Serial.println("[WiFi] config.h 的密碼寫入 NVS 失敗");
        }
        preferences.end();
      }
    }

    // 讀取馬達 PID 設定
    loadPIDSettings();

    // WiFi 連線
    //
    // 憑證留一份在成員變數裡：開機這一次連不上並**不是**終局，wifiTick() 會
    // 持續退避重試。原本只有這 10 秒一次機會，路由器比模組晚開機、DHCP 慢、
    // 訊號弱都會讓模組永遠停在 AP-only，而且沒有任何地方看得出原因。
    staSsid = wifiSSID;
    staPassword = wifiPassword;
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    if (staSsid.length() > 0 && staPassword.length() > 0) {
      WiFi.begin(staSsid.c_str(), staPassword.c_str());
      Serial.printf("嘗試連線至路由器 ssid=%s（密碼 %u 字元）...\n",
                    staSsid.c_str(), (unsigned)staPassword.length());

      unsigned long startAttemptTime = millis();
      const unsigned long wifiTimeout = 10000; // 10秒

      while (WiFi.status() != WL_CONNECTED &&
             millis() - startAttemptTime < wifiTimeout) {
        delay(500);
        Serial.print(".");
      }
      if (WiFi.status() == WL_CONNECTED) {
        staWasConnected = true;
        Serial.println("\nSTA 連線成功, IP位址: ");
        Serial.println(WiFi.localIP());
      } else {
        // 排下第一次重試，並把「為什麼沒連上」講清楚（status 碼見 staReasonText）
        staRetryBackoff = STA_RETRY_MIN_MS;
        staRetryAt = millis() + staRetryBackoff;
        Serial.printf("\n[WiFi] 開機 10 秒內沒連上：%s（status=%d）。"
                      "背景會持續重試，狀態可查 /api/wifi\n",
                      staReasonText(WiFi.status()), (int)WiFi.status());
      }
    } else if (staSsid.length() == 0) {
      Serial.println("[WiFi] 沒有已儲存的 SSID，只啟動 AP。"
                     "請連上 AP 後開 /set 設定，或查 /api/wifi");
    } else {
      // 真的會絆倒人的一種：SSID 有、密碼是空的，舊版會整段跳過且一聲不響
      Serial.println("[WiFi] 有 SSID 但密碼是空的，STA 不會嘗試連線"
                     "（本韌體目前不支援無密碼的開放網路）");
    }

    // 啟動 AP

    // 名稱組法（含 STA 連上時的精簡格式與 32 bytes 上限）見 buildBroadcastAPName()
    String apName = buildBroadcastAPName();

    // 啟動 AP，設定更多參數確保相容性
    const char *apPassword = CONFIG_AP_PASSWORD;
    if (apPassword == nullptr || strlen(apPassword) == 0) {
      apPassword = "12345678";
    }
    bool apResult = WiFi.softAP(apName.c_str(), apPassword, 1, 0, 4);
    if (apResult) {
      // 記下「實際廣播出去的是哪個名字」。AP 名稱在這裡定案之後就不再更動
      // （重設 softAP 會把學生已連上的手機踢掉），所以 /api/wifi 必須回報
      // 這個值，而不是重新組一次 —— 否則背景重連成功後，網頁上寫的名字
      // 會和手機清單上看到的不一樣。
      broadcastApName = apName;
      Serial.printf("AP 啟動成功 - Name: %s（%u bytes）\n", apName.c_str(),
                    (unsigned)apName.length());
      Serial.print("AP IP: ");
      Serial.println(WiFi.softAPIP());
      Serial.printf("AP MAC: %s\n", WiFi.softAPmacAddress().c_str());
    } else {
      Serial.printf("AP 啟動失敗！name=%s（%u bytes，上限 %u）\n",
                    apName.c_str(), (unsigned)apName.length(),
                    (unsigned)kMaxSsidBytes);
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

    // WiFi 狀態查詢 —— 沒有這支端點以前，「設定到底有沒有存進去、為什麼連不上」
    // 只能接 USB 看序列輸出。密碼一律不回傳，只回長度。
    server.on("/api/wifi", HTTP_GET, [this](AsyncWebServerRequest *request) {
      preferences.begin("wifi", true);
      const String savedSsid = preferences.getString("ssid", "");
      const size_t savedPwLen = preferences.getString("password", "").length();
      const String savedApName = preferences.getString("apName", "");
      preferences.end();

      DynamicJsonDocument doc(640);
      doc["ok"] = true;
      JsonObject sta = doc.createNestedObject("sta");
      sta["ssid"] = savedSsid;
      sta["password_len"] = savedPwLen;
      sta["configured"] = savedSsid.length() > 0 && savedPwLen > 0;
      const wl_status_t st = WiFi.status();
      sta["connected"] = st == WL_CONNECTED;
      sta["status"] = (int)st;
      sta["reason"] = staReasonText(st);
      if (st == WL_CONNECTED) {
        sta["ip"] = WiFi.localIP().toString();
        sta["rssi"] = WiFi.RSSI();
        sta["channel"] = WiFi.channel();
      } else {
        const unsigned long now = millis();
        sta["retry_in_ms"] =
            (staRetryAt > now) ? (uint32_t)(staRetryAt - now) : 0;
      }
      JsonObject ap = doc.createNestedObject("ap");
      // name = 設定的名稱；broadcast = 手機 WiFi 清單上真正看到的那個。
      // STA 連上時 broadcast 會是「末四碼-IP」的精簡格式，兩者本來就不同，
      // 所以兩個都回報，使用者才對得起來。
      ap["name"] = savedApName.length() > 0 ? savedApName : getDefaultAPName();
      ap["broadcast"] = broadcastApName;
      ap["ip"] = WiFi.softAPIP().toString();
      ap["clients"] = WiFi.softAPgetStationNum();
      String out;
      serializeJson(doc, out);
      request->send(200, "application/json", out);
    });

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
      const String ssid = request->arg("ssid");
      const String password = request->arg("password");
      const String apName = request->arg("name");

      // 規範 #5：每個 NVS 寫入都要驗證，失敗絕不回報成功。
      // 失敗時也刻意**不重開機** —— 重開只會把「設定沒存進去」變成
      // 「網頁消失、使用者不知道為什麼」，正是最難自己想通的那種狀況。
      String err;
      if (ssid.length() == 0) {
        err = "SSID 不可空白";
      } else if (!preferences.begin("wifi", false)) {
        err = "無法開啟 NVS 'wifi' 命名空間（NVS 可能已滿或損壞）";
      } else {
        if (!putStringVerified("ssid", ssid)) err = "SSID 寫入失敗";
        if (err.length() == 0 && !putStringVerified("password", password))
          err = "密碼寫入失敗";
        // apName 只在真的有填時才寫。set.html 的名稱欄位是用 display:none
        // 隱藏的（不是 disabled），所以「只存 WiFi」時瀏覽器照樣會送出
        // name=""，無條件寫入會把使用者原本的控制板名稱清掉。
        if (err.length() == 0 && apName.length() > 0 &&
            !putStringVerified("apName", apName))
          err = "AP 名稱寫入失敗";
        preferences.end();
      }

      if (err.length() > 0) {
        Serial.printf("[WiFi] 設定儲存失敗：%s\n", err.c_str());
        request->send(500, "text/html; charset=utf-8",
                      "設定<b>沒有</b>存進去：" + err +
                          "<br>裝置不會重啟，原本的設定保持不變。");
        return;
      }

      Serial.printf("[WiFi] 設定已儲存 ssid=%s（密碼 %u 字元）\n", ssid.c_str(),
                    (unsigned)password.length());
      request->send(200, "text/html; charset=utf-8",
                    "WiFi 設定已儲存，裝置即將重啟...");
      scheduleRestart(800);   // 不在 async callback 裡 delay + restart
    });

    // 新增僅設定控制板名稱 POST handler
    server.on("/setnameonly", HTTP_POST,
              [this](AsyncWebServerRequest *request) {
                String apName = request->arg("name");
                if (apName.length() > 0) {
                  // 規範 #5：寫入要驗證，失敗不得回報成功、也不重開機
                  if (!preferences.begin("wifi", false)) {
                    request->send(500, "text/html; charset=utf-8",
                                  "名稱<b>沒有</b>存進去：無法開啟 NVS "
                                  "'wifi' 命名空間。裝置不會重啟。");
                    return;
                  }
                  const bool ok = putStringVerified("apName", apName);
                  preferences.end();
                  if (!ok) {
                    Serial.println("[WiFi] AP 名稱寫入失敗");
                    request->send(500, "text/html; charset=utf-8",
                                  "名稱<b>沒有</b>存進去：NVS 寫入失敗。"
                                  "裝置不會重啟，原本的名稱保持不變。");
                    return;
                  }
                  request->send(200, "text/html; charset=utf-8",
                                "控制板名稱已儲存，裝置即將重啟...");
                  scheduleRestart(800);
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

  // MAC 末四碼 —— AP 名稱裡真正用來分辨「哪一塊板子」的部分
  String macSuffix() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    return mac.substring(mac.length() - 4);
  }

  // 取得預設 AP 名稱（MAC 末四碼）
  String getDefaultAPName() { return "ESP32-" + macSuffix(); }

  // 在 UTF-8 字元邊界上截斷，不切出半個字（切壞了手機端會變亂碼）
  static String truncateUtf8(const String &s, int maxBytes) {
    if (maxBytes <= 0) return String();
    if ((int)s.length() <= maxBytes) return s;
    int cut = maxBytes;
    // s[cut] 是第一個要丟掉的位元組；它若是延續位元組（10xxxxxx）就代表
    // 切在字元中間，往前退到前導位元組為止。
    while (cut > 0 && ((uint8_t)s[cut] & 0xC0) == 0x80) cut--;
    return s.substring(0, cut);
  }

  // 實際廣播出去的 AP 名稱。
  //
  // STA 也連上時要把 IP 一起放進名稱，手機掃到就知道該連哪個位址。問題是
  // 手機 WiFi 清單的欄位很窄，「ESP32-243C (192.168.0.12)」會被截掉尾巴，
  // 而被截掉的正好是唯一有用的那半段。所以 AP + STA 都有的情況改成
  // 「243C-192.168.0.12」：丟掉每塊板子都一樣的 "ESP32-" 前綴、以及括號和
  // 空格，省下 8 個字元，識別碼和 IP 就都看得見了。
  //
  // 自訂名稱不會被換成 MAC —— 那是老師用來分辨板子的名字，換掉就白設了；
  // 只改用同樣精簡的 "-" 分隔。
  //
  // 順帶守住 SSID 的 32 bytes 上限：中文名稱一個字 3 bytes，接上 IP 很容易
  // 超過，而 softAP() 超過就是直接失敗、AP 整個不見（最難查的那種症狀）。
  // 超長時保 IP、截名稱——IP 才是這裡非看到不可的資訊。
  String buildBroadcastAPName() {
    String name = getAPName();
    if (name.length() == 0 && strlen(CONFIG_AP_NAME) > 0) {
      name = CONFIG_AP_NAME;
    }
    // 沒有 STA 時也要守上限：使用者可以自訂名稱，中文一個字 3 bytes，
    // 設長了同樣會讓 softAP() 失敗、AP 整個不見。
    if (WiFi.status() != WL_CONNECTED) {
      return truncateUtf8(name, (int)kMaxSsidBytes);
    }

    if (name == getDefaultAPName()) name = macSuffix();
    const String suffix = "-" + WiFi.localIP().toString();
    return truncateUtf8(name, (int)kMaxSsidBytes - (int)suffix.length()) + suffix;
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

  // 儲存 AP 名稱。回傳是否真的寫進去了（規範 #5），呼叫端不得忽略。
  bool saveAPName(const String &apName) {
    if (!preferences.begin("wifi", false)) {
      Serial.println("[WiFi] saveAPName：無法開啟 NVS 'wifi' 命名空間");
      return false;
    }
    const bool ok = putStringVerified("apName", apName);
    preferences.end();
    if (!ok) Serial.println("[WiFi] saveAPName：寫入失敗");
    return ok;
  }

  // wl_status_t → 看得懂的原因。這幾個碼就是「為什麼連不上」的答案：
  // 1 = 掃不到這個 SSID（打錯，或那是 5GHz —— ESP32 只有 2.4GHz），
  // 4 = 掃到了但關聯失敗（密碼錯最常見）。
  static const char *staReasonText(wl_status_t status) {
    switch (status) {
    case WL_NO_SHIELD:       return "WiFi 硬體未就緒";
    case WL_IDLE_STATUS:     return "閒置中（尚未開始連線）";
    case WL_NO_SSID_AVAIL:   return "找不到這個 SSID（名稱打錯，或它是 5GHz；ESP32 只支援 2.4GHz）";
    case WL_SCAN_COMPLETED:  return "掃描完成，正在連線";
    case WL_CONNECTED:       return "已連線";
    case WL_CONNECT_FAILED:  return "連線失敗（密碼錯誤最常見）";
    case WL_CONNECTION_LOST: return "連線中斷";
    case WL_DISCONNECTED:    return "未連線";
    default:                 return "狀態不明";
    }
  }

  // 安排延遲重開機。不在 AsyncWebServer 的 callback 裡直接 delay + ESP.restart：
  // 那會卡住 AsyncTCP 任務，回應常常還沒送完就被重開切斷，使用者只看到
  // 「網頁壞了」而不知道設定其實已經存好。
  void scheduleRestart(unsigned long afterMs) { restartAt = millis() + afterMs; }

  // 由 loop() 每輪呼叫（非阻塞）。負責 STA 退避重連與延遲重開機。
  void wifiTick() {
    const unsigned long now = millis();

    if (restartAt != 0 && (long)(now - restartAt) >= 0) {
      Serial.println("[系統] 執行延遲重開機");
      Serial.flush();
      ESP.restart();
    }

    if (staSsid.length() == 0 || staPassword.length() == 0) return;

    if (WiFi.status() == WL_CONNECTED) {
      if (!staWasConnected) {
        staWasConnected = true;
        staRetryBackoff = 0;
        staRetryAt = 0;
        Serial.printf("[WiFi] STA 已連線 ip=%s rssi=%d ch=%d\n",
                      WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(),
                      (int)WiFi.channel());
      }
      return;
    }

    if (staWasConnected) {
      staWasConnected = false;
      staRetryBackoff = STA_RETRY_MIN_MS;
      staRetryAt = now + staRetryBackoff;
      Serial.printf("[WiFi] STA 斷線（%s），%lu 秒後重試\n",
                    staReasonText(WiFi.status()), staRetryBackoff / 1000);
      return;
    }

    if (staRetryAt == 0 || (long)(now - staRetryAt) < 0) return;

    // 退避：5s → 10s → 20s → 40s → 60s 上限。不無限縮短間隔，免得連不上的
    // 板子一直重打 WiFi 堆疊，拖累同時在跑的 Web 伺服器與（從機模式的）I2C。
    // 不用 min()：Arduino 各版本有時是巨集、有時是 std::min，後者會對
    // class 內定義的 static const 取參考而要求額外的 out-of-class 定義。
    const unsigned long doubled = staRetryBackoff * 2;
    staRetryBackoff = (staRetryBackoff == 0)      ? STA_RETRY_MIN_MS
                      : (doubled > STA_RETRY_MAX_MS) ? STA_RETRY_MAX_MS
                                                     : doubled;
    staRetryAt = now + staRetryBackoff;
    Serial.printf("[WiFi] 重試連線 ssid=%s（上次：%s），下次間隔 %lu 秒\n",
                  staSsid.c_str(), staReasonText(WiFi.status()),
                  staRetryBackoff / 1000);
    WiFi.begin(staSsid.c_str(), staPassword.c_str());
  }

  bool isSerialMode() const { return serialMode; }
};

#endif
