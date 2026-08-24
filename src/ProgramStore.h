#ifndef PROGRAM_STORE_H
#define PROGRAM_STORE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <vector>

// 單一 PROG 程式的持久化儲存。
//
// 使用 NVS 而非 LittleFS：`uploadfs` 會覆寫 LittleFS 網頁檔，但不會清除 NVS，
// 因此使用者的 Blockly 程式可安全地跨網頁更新保留。
// 首次讀取時會自動搬移舊版 LittleFS 的 /program.* 檔案。
class ProgramStore {
public:
  static constexpr const char *PATH_JSON = "/program.json"; // 舊版遷移來源
  static constexpr const char *PATH_XML = "/program.xml";
  static constexpr const char *PATH_META = "/program_meta.json";
  static constexpr const char *PATH_AUTORUN = "/program_autorun";

  static bool save(const String &json, const String &xml, const String &source) {
    if (json.length() == 0) return false;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return false;

    bool ok = putBlob(prefs, KEY_JSON, json);
    if (ok && xml.length() > 0) ok = putBlob(prefs, KEY_XML, xml);
    if (ok && xml.length() == 0) prefs.remove(KEY_XML);

    DynamicJsonDocument meta(256);
    meta["source"] = source.length() > 0 ? source : "unknown";
    meta["has_xml"] = xml.length() > 0;
    String metaJson;
    serializeJson(meta, metaJson);
    if (ok) ok = prefs.putString(KEY_META, metaJson) == metaJson.length();
    prefs.end();
    return ok;
  }

  static bool exists() {
    migrateLegacyIfNeeded();
    Preferences prefs;
    bool found = prefs.begin(NAMESPACE, true) && prefs.isKey(KEY_JSON);
    prefs.end();
    return found;
  }

  static bool hasXml() {
    migrateLegacyIfNeeded();
    Preferences prefs;
    bool found = prefs.begin(NAMESPACE, true) && prefs.isKey(KEY_XML);
    prefs.end();
    return found;
  }

  static String loadJson() { migrateLegacyIfNeeded(); return getBlob(KEY_JSON); }
  static String loadXml() { migrateLegacyIfNeeded(); return getBlob(KEY_XML); }
  static String loadMeta() {
    migrateLegacyIfNeeded();
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, true)) return "{}";
    String value = prefs.getString(KEY_META, "{}");
    prefs.end();
    return value;
  }

  static void clear() {
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return;
    prefs.remove(KEY_JSON);
    prefs.remove(KEY_XML);
    prefs.remove(KEY_META);
    prefs.end();
  }

  // 對外功能表的版本計數器（Blockly 模組功能契約 §4.5）。
  //
  // 為什麼要持久化：gen 的語意是「功能表的版本」，而功能表本身存在 NVS 裡。
  // 若 gen 只活在 RAM，模組每次重開機都會從 0 開始，主機看到的是「改版了」，
  // 於是白白重讀一次 0x64 並重建儀表板 channel——明明什麼都沒變。
  //
  // 只在功能表真的改變時才寫（BlocklyFunctions::applyDeclarations 回傳 true），
  // 所以不會有 NVS 寫入次數的疑慮。
  static uint8_t generation() {
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, true)) return 0;
    uint8_t g = prefs.getUChar(KEY_FUNC_GEN, 0);
    prefs.end();
    return g;
  }

  static bool setGeneration(uint8_t g) {
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return false;
    const bool ok = prefs.putUChar(KEY_FUNC_GEN, g) == sizeof(uint8_t);
    prefs.end();
    if (!ok) {
      Serial.println("[ProgramStore] func gen 寫入失敗");
    }
    return ok;
  }

  static bool setAutorun(bool on) {
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return false;
    bool ok = on ? prefs.putBool(KEY_AUTORUN, true) == 1 : prefs.remove(KEY_AUTORUN);
    prefs.end();
    return ok;
  }

  static bool isAutorun() {
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, true)) return false;
    bool on = prefs.getBool(KEY_AUTORUN, false);
    prefs.end();
    return on;
  }

private:
  static constexpr const char *NAMESPACE = "program";
  static constexpr const char *KEY_FUNC_GEN = "func_gen";
  static constexpr const char *KEY_JSON = "json";
  static constexpr const char *KEY_XML = "xml";
  static constexpr const char *KEY_META = "meta";
  static constexpr const char *KEY_AUTORUN = "autorun";

  static bool putBlob(Preferences &prefs, const char *key, const String &value) {
    return prefs.putBytes(key, value.c_str(), value.length()) == value.length();
  }

  static String getBlob(const char *key) {
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, true)) return "";
    size_t len = prefs.getBytesLength(key);
    if (len == 0) { prefs.end(); return ""; }
    std::vector<char> buffer(len + 1, '\0');
    size_t read = prefs.getBytes(key, buffer.data(), len);
    prefs.end();
    return read == len ? String(buffer.data()) : "";
  }

  static String readLegacy(const char *path) {
    if (!LittleFS.exists(path)) return "";
    File file = LittleFS.open(path, "r");
    if (!file) return "";
    String value = file.readString();
    file.close();
    return value;
  }

  static void migrateLegacyIfNeeded() {
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, true)) return;
    bool alreadyMigrated = prefs.isKey(KEY_JSON);
    prefs.end();
    if (alreadyMigrated || !LittleFS.exists(PATH_JSON)) return;

    String json = readLegacy(PATH_JSON);
    if (json.length() == 0) return;
    String xml = readLegacy(PATH_XML);
    String meta = readLegacy(PATH_META);
    String source = "legacy-littlefs";
    DynamicJsonDocument metaDoc(256);
    if (!deserializeJson(metaDoc, meta)) source = metaDoc["source"] | source;
    if (save(json, xml, source)) {
      Serial.println("[ProgramStore] 已將舊 LittleFS 存檔搬移至 NVS");
    }
  }
};

#endif // PROGRAM_STORE_H
