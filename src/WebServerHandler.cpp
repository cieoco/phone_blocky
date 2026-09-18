#include "WebServerHandler.h"
#include "ProgramStore.h"

// 靜態成員定義
WebServerHandler *WebServerHandler::instance = nullptr;

void WebServerHandler::handleSystemRestart(AsyncWebServerRequest *request) {
  Serial.println("[系統] 網頁請求: 重啟ESP32");

  // 先回應請求，告知重啟指令已收到
  request->send(
      200, "application/json",
      "{\"success\":true,\"message\":\"重啟指令已收到，ESP32即將重新啟動\"}");

  // 稍微延遲讓回應完成傳送
  delay(500);

  Serial.println("[系統] 執行ESP32重啟...");
  ESP.restart();
}

// 馬達 PID 與 SysId 相關處理函數
void WebServerHandler::handleMotorPIDGet(AsyncWebServerRequest *request) {
  float p, i, d;
  int minS, maxS;
  cmdProcessor.getPIDGains(p, i, d, minS, maxS);
  bool ffEnabled;
  float ffKS, ffKV, ffKA;
  cmdProcessor.getSpeedFeedForward(ffEnabled, ffKS, ffKV, ffKA);

  float ratio, ppr;
  int pos;
  cmdProcessor.getMotorHardwareParams(ratio, ppr, pos);

  float pkp, pki, pkd;
  int pmaxDuty;
  float ptolerance;
  cmdProcessor.getPositionPIDParams(pkp, pki, pkd, pmaxDuty, ptolerance);

  float hSettle, hKi;
  int hMaxDuty;
  cmdProcessor.getHoldParams(hSettle, hKi, hMaxDuty);

  DynamicJsonDocument doc(1024);
  doc["kp"] = p;
  doc["ki"] = i;
  doc["kd"] = d;
  doc["minSpeed"] = minS;
  doc["maxSpeed"] = maxS;
  doc["speedFFEnabled"] = ffEnabled;
  doc["speedFFkS"] = ffKS;
  doc["speedFFkV"] = ffKV;
  doc["speedFFkA"] = ffKA;
  doc["encoderPos"] = pos;
  doc["gearRatio"] = ratio;
  doc["encoderPPR"] = ppr;
  doc["posCtrlMode"] = cmdProcessor.getPosCtrlMode();
  doc["posKp"] = pkp;
  doc["posKi"] = pki;
  doc["posKd"] = pkd;
  doc["posMaxDuty"] = pmaxDuty;
  doc["posToleranceDeg"] = ptolerance;
  doc["holdSettleDeg"] = hSettle;
  doc["holdKi"] = hKi;
  doc["holdMaxDuty"] = hMaxDuty;

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

void WebServerHandler::handleMotorPIDPost(AsyncWebServerRequest *request,
                                          uint8_t *data, size_t len) {
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, data, len);
  if (error) {
    request->send(400, "text/plain", "Invalid JSON");
    return;
  }

  float p = doc["kp"] | DEFAULT_PID_KP;
  float i = doc["ki"] | DEFAULT_PID_KI;
  float d = doc["kd"] | DEFAULT_PID_KD;
  int minS = doc["minSpeed"] | DEFAULT_MIN_SPEED;
  int maxS = doc["maxSpeed"] | DEFAULT_MAX_SPEED;
  bool ffEnabled = doc["speedFFEnabled"] | DEFAULT_SPEED_FF_ENABLED;
  float ffKS = doc["speedFFkS"] | DEFAULT_SPEED_FF_KS;
  float ffKV = doc["speedFFkV"] | DEFAULT_SPEED_FF_KV;
  float ffKA = doc["speedFFkA"] | DEFAULT_SPEED_FF_KA;
  int encPos = doc["encoderPos"] | DEFAULT_ENCODER_POS;
  float ratio = doc["gearRatio"] | GEAR_RATIO;
  float ppr = doc["encoderPPR"] | ENCODER_PPR;
  int posCtrlMode = doc["posCtrlMode"] | DEFAULT_POS_CTRL_MODE;
  float pkp = doc["posKp"] | DEFAULT_POS_KP;
  float pki = doc["posKi"] | DEFAULT_POS_KI;
  float pkd = doc["posKd"] | DEFAULT_POS_KD;
  int pmaxDuty = doc["posMaxDuty"] | DEFAULT_POS_MAX_DUTY;
  float ptolerance = doc["posToleranceDeg"] | DEFAULT_POS_TOLERANCE_DEG;
  float hSettle = doc["holdSettleDeg"] | DEFAULT_HOLD_SETTLE_DEG;
  float hKi = doc["holdKi"] | DEFAULT_HOLD_KI;
  int hMaxDuty = doc["holdMaxDuty"] | DEFAULT_HOLD_MAX_DUTY;

  cmdProcessor.setPIDGains(p, i, d);
  cmdProcessor.setSpeedLimits(minS, maxS);
  cmdProcessor.setSpeedFeedForward(ffEnabled, ffKS, ffKV, ffKA);
  cmdProcessor.setMotorHardwareParams(ratio, ppr, encPos);
  cmdProcessor.setPosCtrlMode(posCtrlMode);
  cmdProcessor.setPositionPIDParams(pkp, pki, pkd, pmaxDuty, ptolerance);
  cmdProcessor.setHoldParams(hSettle, hKi, hMaxDuty);

  // 收斂到 CommandProcessor 的有檢查 NVS 寫入（規範 #5）：失敗必須回報
  if (cmdProcessor.saveConfigToNVS()) {
    request->send(200, "text/plain", "馬達與硬體參數已更新並儲存");
  } else {
    request->send(500, "text/plain", "NVS 寫入失敗");
  }
}

void WebServerHandler::handleMotorPIDApply(AsyncWebServerRequest *request,
                                           uint8_t *data, size_t len) {
  DynamicJsonDocument doc(768);
  if (deserializeJson(doc, data, len)) {
    request->send(400, "text/plain", "Invalid JSON");
    return;
  }

  float p_cur, i_cur, d_cur;
  int minS_cur, maxS_cur;
  cmdProcessor.getPIDGains(p_cur, i_cur, d_cur, minS_cur, maxS_cur);
  bool ffEnabled_cur;
  float ffKS_cur, ffKV_cur, ffKA_cur;
  cmdProcessor.getSpeedFeedForward(ffEnabled_cur, ffKS_cur, ffKV_cur, ffKA_cur);

  float p   = doc["kp"]            | p_cur;
  float i   = doc["ki"]            | i_cur;
  float d   = doc["kd"]            | d_cur;
  int   minS = doc["minSpeed"]     | minS_cur;
  int   maxS = doc["maxSpeed"]     | maxS_cur;
  bool ffEnabled = doc["speedFFEnabled"] | ffEnabled_cur;
  float ffKS = doc["speedFFkS"] | ffKS_cur;
  float ffKV = doc["speedFFkV"] | ffKV_cur;
  float ffKA = doc["speedFFkA"] | ffKA_cur;
  int   posCtrlMode = doc["posCtrlMode"] | cmdProcessor.getPosCtrlMode();
  float pkp = doc["posKp"]         | 1.0f;
  float pki = doc["posKi"]         | 0.0f;
  float pkd = doc["posKd"]         | 0.05f;
  int   pmaxDuty = doc["posMaxDuty"]    | 60;
  float ptolerance = doc["posToleranceDeg"] | 3.0f;

  float hSettle_cur, hKi_cur;
  int hMaxDuty_cur;
  cmdProcessor.getHoldParams(hSettle_cur, hKi_cur, hMaxDuty_cur);
  float hSettle = doc["holdSettleDeg"] | hSettle_cur;
  float hKi = doc["holdKi"] | hKi_cur;
  int hMaxDuty = doc["holdMaxDuty"] | hMaxDuty_cur;

  cmdProcessor.setPIDGains(p, i, d);
  cmdProcessor.setSpeedLimits(minS, maxS);
  cmdProcessor.setSpeedFeedForward(ffEnabled, ffKS, ffKV, ffKA);
  cmdProcessor.setPosCtrlMode(posCtrlMode);
  cmdProcessor.setPositionPIDParams(pkp, pki, pkd, pmaxDuty, ptolerance);
  cmdProcessor.setHoldParams(hSettle, hKi, hMaxDuty);

  request->send(200, "text/plain", "PID 參數已應用至 RAM（未寫入 NVS）");
}

void WebServerHandler::handleMotorHWApply(AsyncWebServerRequest *request,
                                          uint8_t *data, size_t len) {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, data, len)) {
    request->send(400, "text/plain", "Invalid JSON");
    return;
  }

  float ratio, ppr;
  int pos;
  cmdProcessor.getMotorHardwareParams(ratio, ppr, pos);

  ratio = doc["gearRatio"] | ratio;
  ppr   = doc["encoderPPR"] | ppr;
  pos   = doc["encoderPos"] | pos;

  cmdProcessor.setMotorHardwareParams(ratio, ppr, pos);
  Serial.printf("[HW] apply gearRatio=%.1f encoderPPR=%.1f encoderPos=%d (not saved to NVS)\n",
                ratio, ppr, pos);
  request->send(200, "text/plain", "硬體參數已應用（未寫入 NVS）");
}

void WebServerHandler::handleMotorSysIdPost(AsyncWebServerRequest *request,
                                            uint8_t *data, size_t len) {
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, data, len);
  if (error) {
    request->send(400, "text/plain", "Invalid JSON");
    return;
  }

  int motor = doc["motor"] | 3;
  int voltage = doc["voltage"] | 100;
  bool start = doc["start"] | false;

  if (start) {
    cmdProcessor.startSysId(motor, voltage);
    request->send(200, "text/plain", "SysId 測試已啟動");
  } else {
    cmdProcessor.stopSysId();
    request->send(200, "text/plain", "SysId 測試已停止");
  }
}

void WebServerHandler::handleMotorSysIdResultsGet(
    AsyncWebServerRequest *request) {
  String results = cmdProcessor.getSysIdResultsJson();
  request->send(200, "application/json", results);
}

void WebServerHandler::loadPIDSettings() {
  preferences.begin("motor", true);
  float p = preferences.getFloat("kp", DEFAULT_PID_KP);
  float i = preferences.getFloat("ki", DEFAULT_PID_KI);
  float d = preferences.getFloat("kd", DEFAULT_PID_KD);
  int minS = preferences.getInt("minSpeed", DEFAULT_MIN_SPEED);
  int maxS = preferences.getInt("maxSpeed", DEFAULT_MAX_SPEED);
  bool ffEnabled = preferences.getBool("speedFFEn", DEFAULT_SPEED_FF_ENABLED);
  float ffKS = preferences.getFloat("speedFFkS", DEFAULT_SPEED_FF_KS);
  float ffKV = preferences.getFloat("speedFFkV", DEFAULT_SPEED_FF_KV);
  float ffKA = preferences.getFloat("speedFFkA", DEFAULT_SPEED_FF_KA);
  int encPos = preferences.getInt("encoderPos", DEFAULT_ENCODER_POS);
  float ratio = preferences.getFloat("gearRatio", GEAR_RATIO);
  float ppr = preferences.getFloat("encoderPPR", ENCODER_PPR);
  int posCtrlMode = preferences.getInt("posCtrlMode", DEFAULT_POS_CTRL_MODE);
  float pkp = preferences.getFloat("posKp", DEFAULT_POS_KP);
  float pki = preferences.getFloat("posKi", DEFAULT_POS_KI);
  float pkd = preferences.getFloat("posKd", DEFAULT_POS_KD);
  int pmaxDuty = preferences.getInt("posMaxDuty", DEFAULT_POS_MAX_DUTY);
  float ptolerance = preferences.getFloat("posTolDeg", DEFAULT_POS_TOLERANCE_DEG);
  float hSettle = preferences.getFloat("holdSettle", DEFAULT_HOLD_SETTLE_DEG);
  float hKi = preferences.getFloat("holdKi", DEFAULT_HOLD_KI);
  int hMaxDuty = preferences.getInt("holdMax", DEFAULT_HOLD_MAX_DUTY);
  preferences.end();

  cmdProcessor.setPIDGains(p, i, d);
  cmdProcessor.setSpeedLimits(minS, maxS);
  cmdProcessor.setSpeedFeedForward(ffEnabled, ffKS, ffKV, ffKA);
  cmdProcessor.setMotorHardwareParams(ratio, ppr, encPos);
  cmdProcessor.setPosCtrlMode(posCtrlMode);
  cmdProcessor.setPositionPIDParams(pkp, pki, pkd, pmaxDuty, ptolerance);
  cmdProcessor.setHoldParams(hSettle, hKi, hMaxDuty);
  Serial.println("[WebServerHandler] 馬達與硬體參數設定已從 NVS 載入");
}

// WebSocket 連接監控相關函數實現
void WebServerHandler::stopAllMotors() {
  Serial.println("[WebSocket] 連接中斷，停止所有馬達");
  String stopCommand = "{\"cmd\":\"stop\"}";
  cmdProcessor.processCommands(stopCommand, Comm::CH_WS);
}

void WebServerHandler::checkWebSocketTimeout() {
  if (webSocketConnected &&
      (millis() - lastWebSocketMessage > WEBSOCKET_TIMEOUT)) {
    Serial.println("[WebSocket] 連接超時，停止所有馬達");
    webSocketConnected = false;
    stopAllMotors();
  }

  // 定期清理殭屍 WebSocket 連線：前端斷線每 2 秒自動重連，沒清掉的舊 client 會
  // 佔住連線槽與 heap，和網頁載入的並行連線搶資源（加重首次載入卡頓）。節流每秒一次。
  static unsigned long lastWsCleanup = 0;
  unsigned long now = millis();
  if (now - lastWsCleanup >= 1000) {
    lastWsCleanup = now;
    ws.cleanupClients();
  }
}

// ============================================================================
// 已儲存程式 (Blockly + AI 共用) — LittleFS 持久化
// ============================================================================

void WebServerHandler::handleProgramGet(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(8192);
  doc["ok"] = true;
  doc["has_program"] = ProgramStore::exists();

  if (ProgramStore::exists()) {
    // 把 program.json 內容當成巢狀物件還原回去
    String jsonStr = ProgramStore::loadJson();
    DynamicJsonDocument progDoc(4096);
    DeserializationError err = deserializeJson(progDoc, jsonStr);
    if (!err) {
      doc["json"] = progDoc.as<JsonVariant>();
    } else {
      doc["json"] = nullptr;
      doc["json_parse_error"] = err.c_str();
    }

    String xml = ProgramStore::loadXml();
    if (xml.length() > 0) doc["xml"] = xml;

    String metaStr = ProgramStore::loadMeta();
    DynamicJsonDocument metaDoc(256);
    if (!deserializeJson(metaDoc, metaStr)) {
      doc["meta"] = metaDoc.as<JsonVariant>();
    }
  }

  doc["autorun"] = ProgramStore::isAutorun();

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerHandler::handleProgramPost(AsyncWebServerRequest *request,
                                         uint8_t *data, size_t len,
                                         size_t index, size_t total) {
  // 累積多段 chunk (XML 可能 > 1 packet)
  if (index == 0) {
    programPostBuffer = "";
    programPostBuffer.reserve(total + 1);
  }
  for (size_t i = 0; i < len; i++) {
    programPostBuffer += (char)data[i];
  }
  if (index + len < total) return; // 還沒收完

  // 收完了,解析
  DynamicJsonDocument doc(programPostBuffer.length() + 1024);
  DeserializationError err = deserializeJson(doc, programPostBuffer);
  if (err) {
    request->send(400, "application/json",
                  String("{\"ok\":false,\"error\":\"bad json: ") + err.c_str() +
                      "\"}");
    return;
  }

  // 必須有 json,且必須是 PROG mode
  if (!doc.containsKey("json") || !doc["json"].is<JsonObject>()) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"missing 'json' object\"}");
    return;
  }
  const char *mode = doc["json"]["mode"] | "";
  if (strcmp(mode, "PROG") != 0) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"json.mode must be 'PROG'\"}");
    return;
  }

  String progJsonStr;
  serializeJson(doc["json"], progJsonStr);

  // Use the execution parser without staging or running any commands.
  // Match its JSON capacity so a saved program can actually be loaded.
  DynamicJsonDocument executable(16384);
  std::vector<BlocklyCommand> parsedSetup, parsedLoop;
  if (deserializeJson(executable, progJsonStr) || executable.overflowed() ||
      !executable["setup"].is<JsonArrayConst>() ||
      !executable["loop"].is<JsonArrayConst>() ||
      !cmdProcessor.parseCommandArray(executable["setup"].as<JsonArrayConst>(), parsedSetup) ||
      !cmdProcessor.parseCommandArray(executable["loop"].as<JsonArrayConst>(), parsedLoop)) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"unsupported_or_invalid_program_command\"}");
    return;
  }

  String xmlStr = doc["xml"] | "";
  String source = doc["source"] | "unknown";

  if (!ProgramStore::save(progJsonStr, xmlStr, source)) {
    request->send(500, "application/json",
                  "{\"ok\":false,\"error\":\"nvs write failed\"}");
    return;
  }

  // 寫入完成後立刻讀回，避免只回報「已接收」卻沒有真正持久化。
  if (!ProgramStore::exists() || ProgramStore::loadJson() != progJsonStr) {
    request->send(500, "application/json",
                  "{\"ok\":false,\"error\":\"program persistence verification failed\"}");
    return;
  }

  Serial.printf("[ProgramStore] 已存檔 (source=%s, has_xml=%d, json_len=%d)\n",
                source.c_str(), xmlStr.length() > 0 ? 1 : 0,
                progJsonStr.length());

  request->send(200, "application/json",
                String("{\"ok\":true,\"source\":\"") + source +
                    "\",\"has_xml\":" + (xmlStr.length() > 0 ? "true" : "false") +
                    "}");
}

void WebServerHandler::handleProgramDelete(AsyncWebServerRequest *request) {
  ProgramStore::clear();
  Serial.println("[ProgramStore] 已清除存檔");
  request->send(200, "application/json", "{\"ok\":true}");
}

void WebServerHandler::handleProgramAutorunGet(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(64);
  doc["ok"] = true;
  doc["on"] = ProgramStore::isAutorun();
  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerHandler::handleProgramAutorunPost(
    AsyncWebServerRequest *request, uint8_t *data, size_t len) {
  DynamicJsonDocument doc(64);
  if (deserializeJson(doc, data, len)) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  bool on = doc["on"] | false;
  ProgramStore::setAutorun(on);
  Serial.printf("[ProgramStore] autorun = %s\n", on ? "ON" : "OFF");
  request->send(200, "application/json",
                String("{\"ok\":true,\"on\":") + (on ? "true" : "false") + "}");
}

void WebServerHandler::runAutorunProgramIfEnabled() {
  if (!ProgramStore::isAutorun() || !ProgramStore::exists()) return;
  String json = ProgramStore::loadJson();
  if (json.length() == 0) return;
  Serial.println("[ProgramStore] 開機 autorun: 載入已儲存程式並執行");
  cmdProcessor.processCommands(json, Comm::CH_WS);
}
