#ifndef PROGRAM_STORE_H
#define PROGRAM_STORE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_partition.h>
#include <nvs_flash.h>
#include <vector>

// 單一 PROG 程式的持久化儲存。
//
// 使用 NVS 而非 LittleFS：`uploadfs` 會覆寫 LittleFS 網頁檔，但不會清除 NVS，
// 因此使用者的 Blockly 程式可安全地跨網頁更新保留。
//
// 2026-10：程式改存在專用的 NVS 分割區 "prog"（partitions.csv，128 KB）。
// 預設 NVS 只有 20 KB，單筆資料上限約 8 KB，Blockly XML 約 30 個積木就放不下。
// - 若韌體燒錄時沒有更新分割表（找不到 "prog"），自動退回預設 NVS，功能照舊。
// - 首次使用時會把預設 NVS 裡的舊存檔（含 autorun 設定）搬到 "prog"。
// - 更早的 LittleFS /program.* 檔案也會搬移。
class ProgramStore {
public:
  static constexpr const char *PATH_JSON = "/program.json"; // 舊版遷移來源
  static constexpr const char *PATH_XML = "/program.xml";
  static constexpr const char *PATH_META = "/program_meta.json";
  static constexpr const char *PATH_AUTORUN = "/program_autorun";

  static bool save(const String &json, const String &xml, const String &source) {
    return save(json, xml.c_str(), xml.length(), source);
  }

  // xml 可直接指向請求緩衝（不必先複製成 String）
  static bool save(const String &json, const char *xml, size_t xmlLen, const String &source) {
    if (json.length() == 0) return false;
    migrateLegacyIfNeeded();
    Preferences prefs;
    if (!openPrefs(prefs, false)) return false;

    bool ok = putBlob(prefs, KEY_JSON, json.c_str(), json.length());
    if (ok && xmlLen > 0) ok = putBlob(prefs, KEY_XML, xml, xmlLen);
    if (ok && xmlLen == 0 && prefs.isKey(KEY_XML)) prefs.remove(KEY_XML);

    DynamicJsonDocument meta(256);
    meta["source"] = source.length() > 0 ? source : "unknown";
    meta["has_xml"] = xmlLen > 0;
    String metaJson;
    serializeJson(meta, metaJson);
    if (ok) ok = prefs.putString(KEY_META, metaJson) == metaJson.length();
    prefs.end();
    return ok;
  }

  static bool exists() {
    migrateLegacyIfNeeded();
    Preferences prefs;
    bool found = openPrefs(prefs, true) && prefs.isKey(KEY_JSON);
    prefs.end();
    return found;
  }

  static bool hasXml() {
    migrateLegacyIfNeeded();
    Preferences prefs;
    bool found = openPrefs(prefs, true) && prefs.isKey(KEY_XML);
    prefs.end();
    return found;
  }

  static String loadJson() { migrateLegacyIfNeeded(); return getBlob(KEY_JSON); }
  static String loadXml() { migrateLegacyIfNeeded(); return getBlob(KEY_XML); }
  static String loadMeta() {
    migrateLegacyIfNeeded();
    Preferences prefs;
    if (!openPrefs(prefs, true)) return "{}";
    String value = prefs.getString(KEY_META, "{}");
    prefs.end();
    return value;
  }

  static void clear() {
    migrateLegacyIfNeeded();
    Preferences prefs;
    if (!openPrefs(prefs, false)) return;
    prefs.remove(KEY_JSON);
    prefs.remove(KEY_XML);
    prefs.remove(KEY_META);
    // A deleted migrated program must not reappear from the old filesystem.
    prefs.putBool(KEY_MIGRATED, true);
    prefs.end();
  }

  static bool setAutorun(bool on) {
    migrateLegacyIfNeeded();
    Preferences prefs;
    if (!openPrefs(prefs, false)) return false;
    bool ok = on ? prefs.putBool(KEY_AUTORUN, true) == 1
                 : (!prefs.isKey(KEY_AUTORUN) || prefs.remove(KEY_AUTORUN));
    prefs.end();
    return ok;
  }

  static bool isAutorun() {
    migrateLegacyIfNeeded();   // 開機 autorun 前要先把舊位置的設定搬過來
    Preferences prefs;
    if (!openPrefs(prefs, true)) return false;
    bool on = prefs.getBool(KEY_AUTORUN, false);
    prefs.end();
    return on;
  }

  // 目前是否使用專用分割區（診斷用）
  static bool usingDedicatedPartition() { return partitionReady(); }

private:
  static constexpr const char *NAMESPACE = "program";
  static constexpr const char *PARTITION = "prog";          // partitions.csv 的 Name
  static constexpr const char *KEY_JSON = "json";
  static constexpr const char *KEY_XML = "xml";
  static constexpr const char *KEY_META = "meta";
  static constexpr const char *KEY_AUTORUN = "autorun";
  static constexpr const char *KEY_MIGRATED = "migrated";    // 已處理 LittleFS 舊檔
  static constexpr const char *KEY_NVS_MOVED = "nvs_moved";  // 已處理預設 NVS 舊存檔

  // 專用分割區是否存在且可用（只檢查一次）。分割區是新的、內容不是 NVS 格式時清空後再初始化；
  // 這只發生在初始化失敗時，不會因為「namespace 還不存在」而清資料。
  static bool partitionReady() {
    static int state = -1;   // -1 未檢查、0 不可用、1 可用
    if (state >= 0) return state == 1;
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, PARTITION);
    if (!part) {
      Serial.println("[ProgramStore] 找不到 'prog' 分割區，使用預設 NVS（容量較小）");
      state = 0;
      return false;
    }
    esp_err_t err = nvs_flash_init_partition(PARTITION);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      Serial.println("[ProgramStore] 初始化 'prog' 分割區");
      nvs_flash_erase_partition(PARTITION);
      err = nvs_flash_init_partition(PARTITION);
    }
    state = (err == ESP_OK) ? 1 : 0;
    if (!state) Serial.printf("[ProgramStore] 'prog' 分割區無法使用 (err=%d)，改用預設 NVS\n", (int)err);
    return state == 1;
  }

  static bool openPrefs(Preferences &prefs, bool readOnly) {
    return partitionReady() ? prefs.begin(NAMESPACE, readOnly, PARTITION)
                            : prefs.begin(NAMESPACE, readOnly);
  }

  static bool putBlob(Preferences &prefs, const char *key, const char *value, size_t len) {
    return prefs.putBytes(key, value, len) == len;
  }

  static String readBlob(Preferences &prefs, const char *key) {
    size_t len = prefs.getBytesLength(key);
    if (len == 0) return "";
    std::vector<char> buffer(len + 1, '\0');
    size_t read = prefs.getBytes(key, buffer.data(), len);
    return read == len ? String(buffer.data()) : "";
  }

  static String getBlob(const char *key) {
    Preferences prefs;
    if (!openPrefs(prefs, true)) return "";
    String value = readBlob(prefs, key);
    prefs.end();
    return value;
  }

  static String readLegacy(const char *path) {
    if (!LittleFS.exists(path)) return "";
    File file = LittleFS.open(path, "r");
    if (!file) return "";
    String value = file.readString();
    file.close();
    return value;
  }

  // 預設 NVS 的舊存檔 → 專用分割區（一次性；搬完清掉舊的以釋放預設 NVS 空間）
  static void migrateFromDefaultNvsIfNeeded() {
    if (!partitionReady()) return;
    Preferences dst;
    if (!dst.begin(NAMESPACE, false, PARTITION)) return;
    if (dst.isKey(KEY_JSON) || dst.getBool(KEY_NVS_MOVED, false)) { dst.end(); return; }

    Preferences src;
    bool moved = true;
    if (src.begin(NAMESPACE, false)) {
      if (src.isKey(KEY_JSON)) {
        String json = readBlob(src, KEY_JSON);
        String xml = readBlob(src, KEY_XML);
        moved = json.length() > 0 && putBlob(dst, KEY_JSON, json.c_str(), json.length());
        if (moved && xml.length() > 0) moved = putBlob(dst, KEY_XML, xml.c_str(), xml.length());
        if (moved && src.isKey(KEY_META)) dst.putString(KEY_META, src.getString(KEY_META, "{}"));
      }
      if (moved) {
        if (src.getBool(KEY_AUTORUN, false)) dst.putBool(KEY_AUTORUN, true);
        if (src.getBool(KEY_MIGRATED, false)) dst.putBool(KEY_MIGRATED, true);
        src.clear();
        Serial.println("[ProgramStore] 已將存檔搬到專用分割區 'prog'");
      }
      src.end();
    }
    if (moved) dst.putBool(KEY_NVS_MOVED, true);
    dst.end();
  }

  static void migrateLegacyIfNeeded() {
    static bool done = false;   // 每次開機只需檢查一次
    if (done) return;
    done = true;
    migrateFromDefaultNvsIfNeeded();

    Preferences prefs;
    // Read/write opens the namespace on the very first NVS boot too.
    if (!openPrefs(prefs, false)) return;
    bool alreadyMigrated = prefs.isKey(KEY_JSON) || prefs.getBool(KEY_MIGRATED, false);
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
