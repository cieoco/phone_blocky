#ifndef BLOCKLY_FUNCTIONS_H
#define BLOCKLY_FUNCTIONS_H

// Blockly 對外功能註冊表
// ---------------------------------------------------------------------------
// 學生在 Blockly 拉「對外功能」積木，宣告這支程式對主機提供哪些可開關／可設定的
// 功能。本類別只保存「結構」（有幾個、各是什麼型態、目前值），**不保存名稱、
// 單位、數值範圍**——那些是呈現層的事，由主機儀表板負責，學生在儀表板上自己命名。
//
// 契約規格：robot repo 的 docs/architecture/blockly_module_contract_sdd.md §4。
// 名稱為何不上 I2C，見該文件 §2.1；簡述：兩端命名慣例相反（儀表板全英文、
// Blockly 積木全中文），而名稱一旦上線，20 bytes 的單次請求上限立刻變成主要約束。
//
// 併發模型
// --------
// - 寫入者只有 jsonTask（主迴圈）：學生存檔時 declare()、主機下令經佇列落地後
//   setValue()。
// - 讀取者是 i2c_slave_task（callback）：備妥 0x64 / 0x66 / 0x67 的回應。
// 單寫單讀、且欄位都是 uint8/int16 對齊存取，因此不需要鎖。callback 內只做
// 唯讀快照，符合 phone_blocky/docs/i2c_slave_sdd.md §7.1 的禁令。

#include <Arduino.h>
#include <string.h>

class BlocklyFunctions {
public:
  // 上限 16 —— 這不是隨手取的數字：0x64 的回應是
  // [STATUS][ECHO][n_func][type × n]，16 個剛好 19 bytes，加 CRC8 為 20，
  // 正好貼齊單次 I2C 請求的硬上限。要再擴充必須改成分頁讀取。
  static constexpr uint8_t kMaxFunctions = 16;

  // 型態位元組（SDD §4.2）
  static constexpr uint8_t kTypeAnalog = 0x01;   // bit0：0 = 數位、1 = 類比
  static constexpr uint8_t kTypeReadable = 0x02; // bit1：主機可回讀目前值
  static constexpr uint8_t kTypeReserved = 0xFC; // bit2-7 必須為 0

  // 0x67 一頁最多 8 個 int16：[STATUS][ECHO][page][val × 8] = 19 bytes
  static constexpr uint8_t kAnalogPerPage = 8;

  void clear() {
    count_ = 0;
    memset(types_, 0, sizeof(types_));
    memset(values_, 0, sizeof(values_));
  }

  // 學生存檔時由 PROG 解析呼叫。idx 決定 I2C 上的位置，必須連續由 0 起算。
  // 回傳 false 表示宣告無效（索引越界或型態位元組帶了保留位元）。
  bool declare(uint8_t idx, uint8_t typeByte) {
    if (idx >= kMaxFunctions || (typeByte & kTypeReserved) != 0) {
      return false;
    }
    types_[idx] = typeByte;
    values_[idx] = 0;
    if (idx >= count_) {
      count_ = static_cast<uint8_t>(idx + 1);
    }
    return true;
  }

  // 功能表變更後呼叫。主機靠這個計數器偵測「該重讀功能表了」——它放在 0x66 的
  // 回應裡（只有 5 bytes），讓變更偵測與狀態同步共用同一次輪詢。
  void bumpGeneration() { generation_ = static_cast<uint8_t>(generation_ + 1); }

  // 開機時由 NVS 還原（見 ProgramStore）。gen 是「功能表的版本」，而功能表是
  // 持久化的，所以 gen 也該持久化——否則模組每次重開機都會讓主機看到「改版了」
  // 而白白重讀一次 0x64。
  void setGeneration(uint8_t g) { generation_ = g; }

  struct Declaration {
    uint8_t idx;
    uint8_t type;
  };

  // 套用一整份宣告（PROG JSON 的頂層 `functions`）。
  //
  // **只有在功能表真的改變時才 bump `gen`。** 學生按「執行」重跑同一支程式是
  // 常態動作，若每次都 bump，主機會被迫重讀 0x64 並重建儀表板 channel，
  // 學生剛調好的版面就會閃一下——而且什麼也沒變。
  //
  // 回傳 true 表示功能表變了（呼叫端據此決定要不要寫 NVS）。
  bool applyDeclarations(const Declaration *decls, uint8_t n) {
    uint8_t newTypes[kMaxFunctions] = {0};
    uint8_t newCount = 0;
    for (uint8_t i = 0; i < n; ++i) {
      const uint8_t idx = decls[i].idx;
      if (idx >= kMaxFunctions || (decls[i].type & kTypeReserved) != 0) {
        continue; // 無效宣告直接忽略，不讓它污染整張表
      }
      newTypes[idx] = decls[i].type;
      if (idx >= newCount) {
        newCount = static_cast<uint8_t>(idx + 1);
      }
    }

    if (newCount == count_ && memcmp(newTypes, types_, sizeof(types_)) == 0) {
      return false; // 完全相同：不動 gen、不清值
    }

    count_ = newCount;
    memcpy(types_, newTypes, sizeof(types_));
    // 表變了，舊的值不再有意義（型態可能整個換掉），一律歸零。
    memset(values_, 0, sizeof(values_));
    bumpGeneration();
    return true;
  }

  uint8_t count() const { return count_; }
  uint8_t generation() const { return generation_; }

  uint8_t typeAt(uint8_t idx) const {
    return (idx < kMaxFunctions) ? types_[idx] : 0;
  }
  bool isAnalog(uint8_t idx) const {
    return (typeAt(idx) & kTypeAnalog) != 0;
  }
  bool isReadable(uint8_t idx) const {
    return (typeAt(idx) & kTypeReadable) != 0;
  }
  bool valid(uint8_t idx) const { return idx < count_; }

  bool setValue(uint8_t idx, int16_t value) {
    if (!valid(idx)) {
      return false;
    }
    // 數位一律正規化為 0/1，避免主機送 2 之後 bitmap 與 Blockly 判讀不一致。
    values_[idx] = isAnalog(idx) ? value : (value != 0 ? 1 : 0);
    return true;
  }

  int16_t valueAt(uint8_t idx) const {
    return valid(idx) ? values_[idx] : 0;
  }

  // 0x66 用。只有「數位且可回讀」的功能會佔位元，其餘恆為 0——主機據此就知道
  // 哪些位元有意義，不必另外傳一張遮罩。
  uint16_t digitalBitmap() const {
    uint16_t bits = 0;
    for (uint8_t i = 0; i < count_; ++i) {
      if (!isAnalog(i) && isReadable(i) && values_[i] != 0) {
        bits |= static_cast<uint16_t>(1u << i);
      }
    }
    return bits;
  }

  // 0x67 用。非類比或不可回讀的欄位填 0（SDD §4.6）。
  // 回傳實際填入的組數。
  uint8_t analogPage(uint8_t page, int16_t *out, uint8_t outCapacity) const {
    const uint8_t base = static_cast<uint8_t>(page * kAnalogPerPage);
    uint8_t filled = 0;
    for (uint8_t i = 0; i < kAnalogPerPage && i < outCapacity; ++i) {
      const uint8_t idx = static_cast<uint8_t>(base + i);
      out[i] = (idx < count_ && isAnalog(idx) && isReadable(idx))
                   ? values_[idx]
                   : 0;
      ++filled;
    }
    return filled;
  }

  // 是否有任何可回讀的類比功能。沒有的話主機就不必發 0x67。
  bool hasReadableAnalog() const {
    for (uint8_t i = 0; i < count_; ++i) {
      if (isAnalog(i) && isReadable(i)) {
        return true;
      }
    }
    return false;
  }

private:
  uint8_t count_ = 0;
  uint8_t generation_ = 0;
  uint8_t types_[kMaxFunctions] = {0};
  int16_t values_[kMaxFunctions] = {0};
};

#endif // BLOCKLY_FUNCTIONS_H
