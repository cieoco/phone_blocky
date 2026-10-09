#ifndef PROGRAM_PAYLOAD_H
#define PROGRAM_PAYLOAD_H

#include <Arduino.h>
#include <ArduinoJson.h>

// /api/program 的請求解析與回應組裝（只依賴 ArduinoJson，可在電腦上單元測試）。
//
// 為什麼不用固定大小的 JsonDocument：Blockly 的 XML 很大（每個積木約 230 bytes），
// 舊做法 GET 用 8 KB 文件、POST 用「長度 + 1 KB」，約 30 個積木就會溢位，
// 造成 XML 被默默丟掉（讀回驗證失敗）或 POST 回 NoMemory。

// 存檔上限（JSON + XML 合計）。讀回時「載入的 XML」與「回應字串」要同時放在 heap，
// 32 KB 以內較安全；約可存 140 個動作積木。前端 app.js 的 MAX_SAVE_BYTES 需一致。
static const size_t PROGRAM_STORE_MAX_BYTES = 32768;

namespace ProgramPayload {

struct SaveRequest {
  String json;               // 重新序列化後的 PROG JSON
  const char *xml = "";      // 指向請求內容內部（zero-copy），請求緩衝釋放前有效
  size_t xmlLen = 0;
  String source = "unknown";
  const char *error = nullptr;
};

// 解析 POST 內容 {"json":{...PROG...},"xml":"...","source":"..."}。
// body 必須以 '\0' 結尾且可寫入：使用 zero-copy 模式，字串直接留在 body 裡，
// 文件只需要節點空間；節點數以「, { [」的個數估上限（XML 內的逗號只會讓估計偏大）。
inline bool parseSaveBody(char *body, size_t len, SaveRequest &out) {
  size_t tokens = 4;
  for (size_t i = 0; i < len; i++) {
    const char c = body[i];
    if (c == ',' || c == '{' || c == '[') tokens++;
  }
  DynamicJsonDocument doc(JSON_OBJECT_SIZE(tokens) + 512);
  DeserializationError err = deserializeJson(doc, body);   // char* → zero-copy
  if (err) {
    out.error = (err == DeserializationError::NoMemory || err == DeserializationError::TooDeep)
                    ? "program_too_large" : "bad_json";
    return false;
  }
  JsonVariantConst prog = doc["json"];
  if (!prog.is<JsonObjectConst>()) { out.error = "missing_json"; return false; }
  const char *mode = prog["mode"] | "";
  if (strcmp(mode, "PROG") != 0) { out.error = "mode_must_be_prog"; return false; }

  out.json = "";
  serializeJson(prog, out.json);
  JsonVariantConst xml = doc["xml"];
  if (xml.is<const char *>()) {
    out.xml = xml.as<const char *>();
    out.xmlLen = strlen(out.xml);
  }
  out.source = doc["source"] | "unknown";
  if (out.json.length() + out.xmlLen > PROGRAM_STORE_MAX_BYTES) {
    out.error = "program_too_large";
    return false;
  }
  return true;
}

// 把字串以 JSON 字串格式（含引號、跳脫）接到 out 後面
inline void appendJsonString(String &out, const char *s, size_t len) {
  static const char HEX_DIGITS[] = "0123456789abcdef";
  out += '"';
  for (size_t i = 0; i < len; i++) {
    const char c = s[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((unsigned char)c < 0x20) {
          out += "\\u00";
          out += HEX_DIGITS[(c >> 4) & 0xF];
          out += HEX_DIGITS[c & 0xF];
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

// 檢查文字是不是合法的 JSON 物件；用空過濾器解析，不保存內容，幾乎不佔記憶體
inline bool isJsonObject(const String &text) {
  if (text.length() == 0) return false;
  StaticJsonDocument<16> filter;
  filter.to<JsonObject>();
  StaticJsonDocument<64> doc;
  DeserializationError err = deserializeJson(doc, text, DeserializationOption::Filter(filter),
                                             DeserializationOption::NestingLimit(64));
  return !err && doc.is<JsonObject>();
}

// 組 GET /api/program 的回應。json / meta 是存檔時由 serializeJson 產生的文字，
// 驗證合法後直接嵌入，不再解析成文件（避免固定大小文件溢位）。
inline String buildGetResponse(bool hasProgram, const String &json, const String &xml,
                               const String &meta, bool autorun) {
  String out;
  out.reserve(json.length() + xml.length() + xml.length() / 8 + meta.length() + 192);
  out += "{\"ok\":true,\"has_program\":";
  out += hasProgram ? "true" : "false";
  if (hasProgram) {
    if (isJsonObject(json)) {
      out += ",\"json\":";
      out += json;
    } else {
      out += ",\"json\":null,\"json_parse_error\":\"invalid_stored_json\"";
    }
    if (xml.length() > 0) {
      out += ",\"xml\":";
      appendJsonString(out, xml.c_str(), xml.length());
    }
    if (isJsonObject(meta)) {
      out += ",\"meta\":";
      out += meta;
    }
  }
  out += ",\"autorun\":";
  out += autorun ? "true" : "false";
  out += '}';
  return out;
}

} // namespace ProgramPayload

#endif // PROGRAM_PAYLOAD_H
