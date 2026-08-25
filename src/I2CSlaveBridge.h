#ifndef I2C_SLAVE_BRIDGE_H
#define I2C_SLAVE_BRIDGE_H

// phone_blocky I2C 從機橋接層
//
// 讓本板掛上 i2cESP32 主機的 I2C 匯流排（位址 0x36），控制語法沿用
// motorControl 的既有契約。設計規格見 docs/i2c_slave_sdd.md。
//
// 協議來源（唯讀引用，不得修改對方）：
//   motorControl/src/config.h L147-202                  命令碼表
//   motorControl/docs/architecture/I2C_FRAME_V2.md      回應框架
//   motorControl/src/middleware/i2c_slave.cpp           參考實作
//
// ── callback 執行環境的硬性約束（SDD §7.1）─────────────────────────
// Arduino core 2.0.17 的 esp32-hal-i2c-slave.c 把 onReceive / onRequest
// 兩個 callback 都放在 i2c_slave_task（xTaskCreate，prio 20，未綁核心）
// 執行，而 ESP-IDF WiFi task 的 prio 是 23——WiFi 可以直接搶佔它。
// 因此本檔所有 callback 路徑內：
//   * 不得配置 heap（不得用 String / DynamicJsonDocument / new）
//   * 不得呼叫 Serial.print*
//   * 不得推送 WebSocket
// 診斷一律走計數器，由主迴圈自行取用列印。

#include "BlocklyFunctions.h"
#include "CommandProcessor.h"
#include "HardwareConfig.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include <string.h>

// ── 命令碼：沿用 motorControl（勿改動數值）──────────────────────────
#define CMD_PING 0x01
#define CMD_GET_INFO 0x02
#define CMD_SET_MODE 0x10
#define CMD_SET_SPEED 0x11
#define CMD_SET_POSITION 0x12
#define CMD_SET_ALL_MOTORS 0x20
#define CMD_STOP_ALL 0x21
#define CMD_MOTOR_HOME 0x22
#define CMD_MOTOR_STOP 0x23
#define CMD_GET_STATUS 0x26
#define CMD_GET_ALL_STATUS 0x27
#define CMD_HEARTBEAT 0x42

// ── bench 專屬區塊 0x60-0x6F（本板自有，不寫回 motorControl）────────
#define CMD_BENCH_SERVO 0x60     // [60][ch][deg i16 BE]
#define CMD_BENCH_PWM 0x61       // [61][id][duty i16 BE]
#define CMD_BENCH_GET_SERVO 0x62 // [62] -> [s1_deg][s2_deg]

// ── Blockly 對外功能契約（SDD §4）────────────────────────────────────
// 自 0x64 起，因為 0x60-0x63 在 motorControl 標記為 SysId 遺留「保留勿重用」，
// 而 0x60-0x62 本檔已用於 bench 直驅。
#define CMD_FUNC_TABLE 0x64   // [64] -> [n_func][type x n]
#define CMD_FUNC_SET 0x65     // [65][idx][0|1] 數位 ／ [65][idx][i16 BE] 類比
#define CMD_FUNC_STATE_D 0x66 // [66] -> [gen][bitmap_hi][bitmap_lo]
#define CMD_FUNC_STATE_A 0x67 // [67][page] -> [page][i16 BE x8]

// 傳輸框架 CRC 相容開關。兩端必須一致——主機 i2cESP32 與從機 motorControl
// 目前皆為 0（無任何 env 開啟），單邊硬啟用會讓整條匯流排失效。
#ifndef I2C_TRANSPORT_FRAME_CRC
#define I2C_TRANSPORT_FRAME_CRC 0
#endif

#if I2C_TRANSPORT_FRAME_CRC
// CRC-8-SAE J1850，多項式 0x1D。與主機 lib/I2CTransport/src/crc8.h 相同。
static inline uint8_t i2cBridgeCrc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x1D)
                         : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}
#endif

class I2CSlaveBridge {
public:
  // 回應框架 V2 狀態碼（I2C_FRAME_V2.md §2）
  static constexpr uint8_t kStatusOk = 0x00;
  static constexpr uint8_t kStatusNotReady = 0x01;
  static constexpr uint8_t kStatusBadCmd = 0x02;

  // staged 回應過期窗口，與 motorControl 一致
  static constexpr uint32_t kStagedResponseMaxAgeMs = 50;

  struct Stats {
    uint32_t rxCount;
    uint32_t badCmdCount;
    uint32_t notReadyCount;
    uint32_t droppedCount;
    uint32_t lastCommMs;
    uint8_t lastCommand;
    uint32_t linkRecoveries;
    uint8_t linkBackoffShift;
  };

  explicit I2CSlaveBridge(CommandProcessor &processor,
                          TwoWire &wireInstance = Wire)
      : proc_(processor), wire_(wireInstance) {
    instanceRef() = this;
  }

  static I2CSlaveBridge *instance() { return instanceRef(); }

  void begin(uint8_t address, int sdaPin, int sclPin) {
    address_ = address;
    sdaPin_ = sdaPin;
    sclPin_ = sclPin;

    // TwoWire::begin() 的從機路徑會失敗（`bad pin state`、`Bus busy, reinit`、
    // 佇列配置失敗），而且只在核心層 log_e，上層看不見。不檢查回傳值的話症狀
    // 會是「板子活著、網頁正常，但主機掃不到 0x36」——那很容易被誤判成接線問題。
    const bool started = wire_.begin(address, sdaPin, sclPin, I2C_SLAVE_FREQ);
    if (!started) {
      initialized_ = false;
      Serial.printf("[I2C] slave begin FAILED addr=0x%02X sda=%d scl=%d\n",
                    address, sdaPin, sclPin);
      return;
    }
    wire_.onReceive(onReceiveCallback);
    wire_.onRequest(onRequestCallback);
    lastCommMs_ = millis();
    initialized_ = true;
  }

  // I2C 周邊看門狗。與 service() 一樣由主迴圈（jsonTask）呼叫，
  // **絕不可在 onReceive / onRequest 內呼叫**（理由見實作內註解）。
  //
  // 病理：主機在交易中途被重置（esptool 的 RTS/DTR，或單純重開）時，本從機的
  // I2C FSM 會停在 clock stretching，之後連自己的位址都不再 ACK，只有重新上電
  // 能清掉。主機端救不了——I2CTransport::recoverBus() 的 9 次 SCL 脈衝只在
  // digitalRead(SDA)==LOW 時才跑，而這個故障的 SDA 是正常的（同匯流排上其他
  // 從機照樣 ACK）。詳見 robot repo 的 DEVELOPER_GUIDELINES.md 第六條第 4 項。
  //
  // 為什麼 end() + begin() 有效（查 Arduino core 原始碼所得，非推測）：
  // Wire.end() -> i2cSlaveDeinit() -> i2c_slave_free_resources() ->
  // i2c_slave_detach_gpio()，最後一步把 SCL/SDA 設回 GPIO_MODE_INPUT 並從 I2C
  // matrix 斷開——**被 stretch 住的 SCL 就是在那一步被釋放的**。
  //
  // 兩個地雷：
  //   1. Wire.begin() 在 is_slave 已為 true 時直接跳過不做事，必須先 end()。
  //   2. i2c_slave_free_resources() 會 vTaskDelete(i2c_slave_task)——在 callback
  //      內呼叫等於刪掉自己正在跑的 task。
  void checkLink() {
    const uint32_t now = millis();

    // 兩種需要進來的狀況，判準完全不同：
    //
    //   (a) 周邊沒起來（initialized_ == false）
    //       開機 begin() 失敗，或本函式上一次重建失敗。此時 hasSeenMaster_
    //       永遠是 false、lastCommMs_ 也永遠不會前進，「靜默逾時」那組條件一條
    //       都不會成立 —— 只能用退避節流。
    //
    //   (b) 周邊活著但被卡住（initialized_ == true）
    //       主機重燒造成的 clock stretching 殘留，靠靜默逾時偵測。
    //
    // 2026-08-25 修正：原本第一行是
    //     if (!initialized_ || !hasSeenMaster_) return;
    // 把 (a) 整個擋在門外，造成兩個永久死狀態，都只能手動重新上電：
    //   1. 開機 begin() 失敗 -> 永遠不重試。實機遇過：本板開機印
    //      「[I2C] slave begin FAILED addr=0x36」，主機 i2c_scan 從此掃不到它。
    //   2. 下面重建失敗的分支註解寫著「下個退避週期再試」，但 initialized_ 留在
    //      false，下次進來又被第一行擋掉 —— 重試永遠不會發生。
    // 與 motorControl 的 I2CSlave::checkI2CLink() 是同一個修法（同日修）。
    const bool needsInit = !initialized_;
    uint32_t idleMs = 0; // 只有 (b) 有意義，供日誌用

    if (needsInit) {
      // 周邊沒起來：第一次進來（lastLinkRecoveryMs_ == 0）立刻重試，之後照退避。
      const uint32_t interval = kLinkIdleMs << linkBackoffShift_;
      if (lastLinkRecoveryMs_ != 0 && now - lastLinkRecoveryMs_ < interval) {
        return;
      }
      return rebuildPeripheral(now, needsInit, idleMs);
    }

    if (!hasSeenMaster_) {
      // 從未與主機通訊過 -> 周邊是剛初始化的乾淨狀態，沒有東西要救。這條同時
      // 擋掉「桌上板單獨用手機網頁玩、根本沒接主機」的常態用法，否則會每 5 秒
      // 無謂地重建一次 I2C 周邊。
      return;
    }

    const uint32_t comm = lastCommMs_; // 快照，避免與 callback 競態

    // 上次重建之後真的收到過東西 -> 鏈路是活的，把退避收回基準值。
    if (lastLinkRecoveryMs_ != 0 &&
        static_cast<int32_t>(comm - lastLinkRecoveryMs_) > 0) {
      linkBackoffShift_ = 0;
    }

    const uint32_t interval = kLinkIdleMs << linkBackoffShift_;
    if (now - comm < interval) {
      return;
    }
    // 主機真的關機時 lastCommMs_ 不會再前進，少了這道閘門會每拍都重建。
    if (lastLinkRecoveryMs_ != 0 && now - lastLinkRecoveryMs_ < interval) {
      return;
    }

    idleMs = now - comm;
    rebuildPeripheral(now, needsInit, idleMs);
  }

  // 重建 I2C 周邊。**只能從主迴圈呼叫**：wire_.end() 會 vTaskDelete
  // (i2c_slave_task)，在 callback 內呼叫等於刪掉正在跑自己的那個 task。
  void rebuildPeripheral(uint32_t now, bool needsInit, uint32_t idleMs) {
    lastLinkRecoveryMs_ = now;
    if (linkBackoffShift_ < kLinkMaxBackoffShift) {
      linkBackoffShift_++;
    }

    // 重建期間先讓 callback 空轉，避免半重建狀態下被呼叫。
    initialized_ = false;
    txReady_ = false;
    txLength_ = 0;

    wire_.end();
    delay(5); // 讓周邊與 GPIO 落定
    const bool started =
        wire_.begin(address_, sdaPin_, sclPin_, I2C_SLAVE_FREQ);
    if (!started) {
      // 多半是 `bad pin state` 或 `Bus busy`——匯流排此刻被別人佔著。不補救，
      // 下個退避週期再試；initialized_ 保持 false，callback 不會半殘動作。
      // 「下個週期再試」現在是真的了——修正前會被 checkLink() 開頭的
      // `if (!initialized_ ...) return;` 擋掉，重試永遠不會發生。
      Serial.printf("[I2C-WD] re-init FAILED addr=0x%02X\n", address_);
      return;
    }
    wire_.onReceive(onReceiveCallback);
    wire_.onRequest(onRequestCallback);
    initialized_ = true;
    linkRecoveryCount_++;
    lastCommMs_ = now; // 重新計時，否則下一拍又立刻判定逾時

    // 這裡可以 Serial：checkLink() 跑在主迴圈，不在 callback 內（SDD 7.1）。
    // 兩種情境分開記，現場才分得出「原本就沒起來」與「起來後被卡住」。
    if (needsInit) {
      Serial.printf("[I2C-WD] initial begin recovered addr=0x%02X count=%lu\n",
                    address_, (unsigned long)linkRecoveryCount_);
    } else {
      Serial.printf(
          "[I2C-WD] peripheral re-init addr=0x%02X idle=%lums count=%lu\n",
          address_, (unsigned long)idleMs, (unsigned long)linkRecoveryCount_);
    }
  }

  bool isInitialized() const { return initialized_; }

  // 主迴圈（jsonTask）每拍呼叫：把 callback 排進來的命令落地執行。
  // 動作類命令一律延後到這裡，因為底層會 Serial.printf，不能在 callback 跑。
  void service() {
    while (qTail_ != qHead_) {
      const PendingCmd c = queue_[qTail_];
      qTail_ = static_cast<uint8_t>((qTail_ + 1) % kQueueSize);
      apply(c);
    }
  }

  // Blockly 對外功能註冊表。寫入者只有主迴圈（PROG 解析、佇列落地），
  // callback 只做唯讀快照。
  BlocklyFunctions &functions() { return funcs_; }
  const BlocklyFunctions &functions() const { return funcs_; }

  // 主迴圈用：取診斷計數（callback 內不得列印，一律由此取出）
  Stats getStats() const {
    Stats s;
    s.rxCount = rxCount_;
    s.badCmdCount = badCmdCount_;
    s.notReadyCount = notReadyCount_;
    s.droppedCount = droppedCount_;
    s.lastCommMs = lastCommMs_;
    s.lastCommand = lastCommand_;
    s.linkRecoveries = linkRecoveryCount_;
    s.linkBackoffShift = linkBackoffShift_;
    return s;
  }

private:
  static constexpr int kMaxBuffer = 32;

  // 延後執行佇列。單一生產者（i2c_slave_task）／單一消費者（jsonTask）的
  // SPSC 環形緩衝，因此不需要鎖；head 只由生產者寫、tail 只由消費者寫。
  struct PendingCmd {
    uint8_t cmd;
    uint8_t motor;
    uint8_t mode;
    float value;
  };
  static constexpr uint8_t kQueueSize = 16;

  CommandProcessor &proc_;
  TwoWire &wire_;
  BlocklyFunctions funcs_;

  PendingCmd queue_[kQueueSize] = {};
  volatile uint8_t qHead_ = 0;
  volatile uint8_t qTail_ = 0;
  volatile uint32_t droppedCount_ = 0;

  // header-only 專案：用函式區域 static 持有單例指標，避免類別靜態成員
  // 在多個 TU 引入時重複定義（本檔為 C++11，不能用 inline static）。
  static I2CSlaveBridge *&instanceRef() {
    static I2CSlaveBridge *ptr = nullptr;
    return ptr;
  }

  uint8_t rxBuffer_[kMaxBuffer] = {0};
  uint8_t txBuffer_[kMaxBuffer] = {0};
  volatile size_t txLength_ = 0;
  volatile bool txReady_ = false;
  volatile bool initialized_ = false;
  volatile uint32_t txStagedAtMs_ = 0;

  volatile uint8_t lastCommand_ = 0;
  volatile bool lastCmdUnknown_ = false;
  volatile uint32_t rxCount_ = 0;
  volatile uint32_t badCmdCount_ = 0;
  volatile uint32_t notReadyCount_ = 0;
  volatile uint32_t lastCommMs_ = 0;
  uint8_t address_ = 0;

  // ── I2C 周邊看門狗 ─────────────────────────────────────────────────────
  // 基準 5 s、退避最多左移 4 位（5→10→20→40→80 s）。主機對本模組的穩態輪詢
  // 約 2.25 Hz（0x66 每 500ms + 0x67 每四輪），5 s 是十倍以上餘裕。
  static constexpr uint32_t kLinkIdleMs = 5000;
  static constexpr uint8_t kLinkMaxBackoffShift = 4;

  int sdaPin_ = -1;
  int sclPin_ = -1;
  // 只由 callback 寫：lastCommMs_ / hasSeenMaster_
  // 只由主迴圈寫：lastLinkRecoveryMs_ / linkBackoffShift_ / linkRecoveryCount_
  volatile bool hasSeenMaster_ = false;
  uint32_t lastLinkRecoveryMs_ = 0;
  uint8_t linkBackoffShift_ = 0;
  uint32_t linkRecoveryCount_ = 0;

  static void onReceiveCallback(int numBytes) {
    I2CSlaveBridge *self = instanceRef();
    if (self != nullptr) {
      self->onReceive(numBytes);
    }
  }

  static void onRequestCallback() {
    I2CSlaveBridge *self = instanceRef();
    if (self != nullptr) {
      self->onRequest();
    }
  }

  void onReceive(int numBytes) {
    if (!initialized_ || numBytes <= 0) {
      return;
    }

    const int expected = (numBytes < kMaxBuffer) ? numBytes : kMaxBuffer;
    int length = 0;
    while (length < expected && wire_.available()) {
      rxBuffer_[length++] = static_cast<uint8_t>(wire_.read());
    }
    // 若 callback 給的位元組多過本地緩衝，排空剩餘避免污染下一筆
    while (wire_.available()) {
      wire_.read();
    }
    if (length <= 0) {
      return;
    }

    txLength_ = 0;
    txReady_ = false;
    memset(txBuffer_, 0, sizeof(txBuffer_));

    int payloadLength = length;
#if I2C_TRANSPORT_FRAME_CRC
    if (length < 2 ||
        i2cBridgeCrc8(rxBuffer_, length - 1) != rxBuffer_[length - 1]) {
      return;
    }
    payloadLength = length - 1;
#endif

    lastCommMs_ = millis();
    // checkLink() 用它區分「周邊卡住」與「這塊板子從來沒接過主機」。
    hasSeenMaster_ = true;
    rxCount_++;
    lastCommand_ = rxBuffer_[0];

    processCommand(payloadLength);

    if (txReady_ && txLength_ > 0) {
      txStagedAtMs_ = millis();
    }
  }

  void onRequest() {
    if (!initialized_) {
      return;
    }

    // staged 回應逾期即作廢，避免主機讀到上一筆命令的殘留回應
    if (txReady_ && txLength_ > 0 &&
        millis() - txStagedAtMs_ > kStagedResponseMaxAgeMs) {
      txReady_ = false;
      txLength_ = 0;
      txStagedAtMs_ = 0;
    }

    // V2 框架：[STATUS][CMD_ECHO][payload][CRC?]
    static uint8_t frame[kMaxBuffer + 3];
    size_t len = 0;
    if (txReady_ && txLength_ > 0) {
      size_t payloadLen = txLength_;
      if (payloadLen > kMaxBuffer) {
        payloadLen = kMaxBuffer;
      }
      frame[0] = kStatusOk;
      frame[1] = lastCommand_;
      memcpy(&frame[2], txBuffer_, payloadLen);
      len = payloadLen + 2;
      txReady_ = false;
      txLength_ = 0;
      txStagedAtMs_ = 0;
    } else {
      frame[0] = lastCmdUnknown_ ? kStatusBadCmd : kStatusNotReady;
      frame[1] = lastCommand_;
      len = 2;
      notReadyCount_++;
    }

#if I2C_TRANSPORT_FRAME_CRC
    frame[len] = i2cBridgeCrc8(frame, len);
    wire_.write(frame, len + 1);
#else
    wire_.write(frame, len);
#endif
  }

  void processCommand(int length) {
    if (length <= 0) {
      return;
    }

    const uint8_t cmd = rxBuffer_[0];
    lastCmdUnknown_ = false;

    switch (cmd) {
    case CMD_PING:
      handlePing(length);
      break;

    case CMD_GET_INFO:
      handleGetInfo();
      break;

    case CMD_HEARTBEAT:
      // 僅更新通訊時戳（已在 onReceive 完成），無回應 payload
      break;

    // ── S3：沿用 motorControl 的馬達命令 ──────────────────────────
    case CMD_SET_MODE: // [10][id][mode]
      if (length < 3) {
        lastCmdUnknown_ = true;
      } else {
        handleSetMode(rxBuffer_[1], rxBuffer_[2]);
      }
      break;

    case CMD_SET_SPEED: // [11][id][float LE ×4]
      if (length < 6) {
        lastCmdUnknown_ = true;
      } else {
        handleSetSpeed(rxBuffer_[1], bytesToFloat(&rxBuffer_[2]));
      }
      break;

    case CMD_SET_POSITION: // [12][id][float LE ×4]
      if (length < 6) {
        lastCmdUnknown_ = true;
      } else {
        handleSetPosition(rxBuffer_[1], bytesToFloat(&rxBuffer_[2]));
      }
      break;

    case CMD_STOP_ALL: // [21]
      enqueue(CMD_STOP_ALL, 0, 0, 0.0f);
      break;

    case CMD_MOTOR_HOME: // [22][id]
      if (length < 2 || !motorHasEncoder(rxBuffer_[1])) {
        lastCmdUnknown_ = true;
      } else {
        enqueue(CMD_MOTOR_HOME, rxBuffer_[1], 0, 0.0f);
      }
      break;

    case CMD_MOTOR_STOP: // [23][id]
      if (length < 2 || !motorValid(rxBuffer_[1])) {
        lastCmdUnknown_ = true;
      } else {
        enqueue(CMD_MOTOR_STOP, rxBuffer_[1], 0, 0.0f);
      }
      break;

    // ── S4：狀態回報（唯讀，於 callback 內同步作答）────────────────
    case CMD_GET_STATUS: // [26][id]
      if (length < 2 || !motorValid(rxBuffer_[1])) {
        lastCmdUnknown_ = true;
      } else {
        handleGetStatus(rxBuffer_[1]);
      }
      break;

    case CMD_GET_ALL_STATUS: // [27][page]
      handleGetAllStatus(length >= 2 ? rxBuffer_[1] : 0);
      break;

    // ── S5：bench 專屬區塊 ─────────────────────────────────────────
    case CMD_BENCH_SERVO: // [60][ch][deg i16 BE]
      if (length < 4 || rxBuffer_[1] < 1 ||
          rxBuffer_[1] > static_cast<uint8_t>(NUM_SERVOS)) {
        lastCmdUnknown_ = true;
      } else {
        enqueue(CMD_BENCH_SERVO, rxBuffer_[1], 0,
                static_cast<float>(bytesToInt16(&rxBuffer_[2])));
      }
      break;

    case CMD_BENCH_PWM: // [61][id][duty i16 BE]
      if (length < 4 || !motorValid(rxBuffer_[1])) {
        lastCmdUnknown_ = true;
      } else {
        enqueue(CMD_BENCH_PWM, rxBuffer_[1], 0,
                static_cast<float>(bytesToInt16(&rxBuffer_[2])));
      }
      break;

    case CMD_BENCH_GET_SERVO: // [62]
      handleGetServo();
      break;

    // ── Blockly 對外功能（SDD §4）────────────────────────────────
    case CMD_FUNC_TABLE: // [64]
      handleFuncTable();
      break;

    case CMD_FUNC_SET: // [65][idx][...]
      handleFuncSet(length);
      break;

    case CMD_FUNC_STATE_D: // [66]
      handleFuncStateDigital();
      break;

    case CMD_FUNC_STATE_A: // [67][page]
      if (length < 2) {
        lastCmdUnknown_ = true;
      } else {
        handleFuncStateAnalog(rxBuffer_[1]);
      }
      break;

    default:
      // 不適用本板的命令（0x20 四輪同步、0x24-0x2F 底盤位姿、
      // 0x40/0x41 氣壓與手臂、0x50 區塊分球器）一律 BAD_CMD——見 SDD §4.3。
      lastCmdUnknown_ = true;
      break;
    }

    // 在收到當下計數，而非等 onRequest。動作類命令是 fire-and-forget，
    // 主機不會回讀，若只在 onRequest 計數，這類 BAD_CMD 會完全看不見。
    if (lastCmdUnknown_) {
      badCmdCount_++;
    }
  }

  // ── handler ────────────────────────────────────────────────────────

  // [01][seq] -> [A5][seq^5A]，與 motorControl 完全一致
  void handlePing(int length) {
    txBuffer_[0] = 0xA5;
    txBuffer_[1] = (length >= 2) ? static_cast<uint8_t>(rxBuffer_[1] ^ 0x5A)
                                 : 0x5A;
    txLength_ = 2;
    txReady_ = true;
  }

  // 對無編碼器的 M1/M2 下閉迴路命令一律 BAD_CMD，**不得靜默退化成 PWM**：
  // 靜默退化會讓主機以為閉迴路生效，比明確拒絕更糟（SDD §6）。
  void handleSetMode(uint8_t motorId, uint8_t mode) {
    if (!motorValid(motorId)) {
      lastCmdUnknown_ = true;
      return;
    }
    if ((mode == 1 || mode == 2) && !motorHasEncoder(motorId)) {
      lastCmdUnknown_ = true;
      return;
    }
    if (mode > 2) {
      lastCmdUnknown_ = true;
      return;
    }
    enqueue(CMD_SET_MODE, motorId, mode, 0.0f);
  }

  void handleSetSpeed(uint8_t motorId, float rpm) {
    if (!motorHasEncoder(motorId)) {
      lastCmdUnknown_ = true;
      return;
    }
    enqueue(CMD_SET_SPEED, motorId, 0, rpm);
  }

  void handleSetPosition(uint8_t motorId, float deg) {
    if (!motorHasEncoder(motorId)) {
      lastCmdUnknown_ = true;
      return;
    }
    enqueue(CMD_SET_POSITION, motorId, 0, deg);
  }

  // [motorCount][protoVer][servoCount][addr]
  // motorControl 的 byte[2] 為保留的 0x00，本板借用回報舵機數；
  // 主機目前忽略此欄，屬安全擴充。
  void handleGetInfo() {
    txBuffer_[0] = static_cast<uint8_t>(NUM_MOTORS);
    txBuffer_[1] = 0x01;
    txBuffer_[2] = static_cast<uint8_t>(NUM_SERVOS);
    txBuffer_[3] = address_;
    txLength_ = 4;
    txReady_ = true;
  }

  // 11B：[motorId][mode][pos i16][tgt i16][rpm i16][tgt_rpm i16][flags]
  // 線上總長 11+2=13，遠低於 20B 上限（SDD §4.5）。
  void handleGetStatus(uint8_t motorId) {
    CommandProcessor::I2CMotorSnapshot snap;
    if (!proc_.i2cGetMotorSnapshot(motorId, snap)) {
      lastCmdUnknown_ = true;
      return;
    }

    size_t offset = 0;
    txBuffer_[offset++] = motorId;
    txBuffer_[offset++] = snap.mode;
    int16ToBytes(snap.posDeg, &txBuffer_[offset]);
    offset += 2;
    int16ToBytes(snap.targetDeg, &txBuffer_[offset]);
    offset += 2;
    int16ToBytes(snap.rpm, &txBuffer_[offset]);
    offset += 2;
    int16ToBytes(snap.targetRpm, &txBuffer_[offset]);
    offset += 2;
    txBuffer_[offset++] = snap.flags;

    txLength_ = offset; // 11
    txReady_ = true;
  }

  // 12B：[numMotors][page][slot0: mode,pos i16,rpm i16][slot1: 同上]
  // 每頁 2 顆：page=0 -> M1/M2，page=1 -> M3/M4。
  void handleGetAllStatus(uint8_t page) {
    const uint8_t safePage = (page > 1) ? 1 : page;
    const int startIdx = static_cast<int>(safePage) * 2;

    txBuffer_[0] = static_cast<uint8_t>(NUM_MOTORS);
    txBuffer_[1] = safePage;

    for (int slot = 0; slot < 2; ++slot) {
      const int motorId = startIdx + slot + 1; // 對外編號 1-based
      uint8_t mode = 0;
      int16_t pos = 0;
      int16_t rpm = 0;

      CommandProcessor::I2CMotorSnapshot snap;
      if (motorId <= NUM_MOTORS && proc_.i2cGetMotorSnapshot(motorId, snap)) {
        mode = snap.mode;
        pos = snap.posDeg;
        rpm = snap.rpm;
      }

      const size_t base = static_cast<size_t>(2 + slot * 5);
      txBuffer_[base] = mode;
      int16ToBytes(pos, &txBuffer_[base + 1]);
      int16ToBytes(rpm, &txBuffer_[base + 3]);
    }

    txLength_ = 12;
    txReady_ = true;
  }

  // 2B：[s1_deg][s2_deg]
  void handleGetServo() {
    txBuffer_[0] = static_cast<uint8_t>(proc_.i2cGetServoDeg(1));
    txBuffer_[1] = static_cast<uint8_t>(proc_.i2cGetServoDeg(2));
    txLength_ = 2;
    txReady_ = true;
  }

  // ── Blockly 對外功能處理器 ─────────────────────────────────────────
  // 全部只讀 funcs_ 的快照或排隊，不在 callback 內執行動作。

  // [64] -> [n_func][type_0]…[type_{n-1}]
  // 16 個功能時剛好 19 bytes（含 V2 框架），加 CRC8 為 20，貼齊硬上限。
  void handleFuncTable() {
    const uint8_t n = funcs_.count();
    txBuffer_[0] = n;
    for (uint8_t i = 0; i < n; ++i) {
      txBuffer_[1 + i] = funcs_.typeAt(i);
    }
    txLength_ = static_cast<size_t>(1 + n);
    txReady_ = true;
  }

  // [65][idx][0|1] 數位／[65][idx][hi][lo] 類比。
  // 長度必須與該功能的型態相符，不吻合一律 BAD_CMD——猜測會讓主機的錯誤靜默。
  void handleFuncSet(int length) {
    if (length < 3) {
      lastCmdUnknown_ = true;
      return;
    }
    const uint8_t idx = rxBuffer_[1];
    if (!funcs_.valid(idx)) {
      lastCmdUnknown_ = true;
      return;
    }
    int16_t value = 0;
    if (funcs_.isAnalog(idx)) {
      if (length < 4) {
        lastCmdUnknown_ = true;
        return;
      }
      value = static_cast<int16_t>((static_cast<uint16_t>(rxBuffer_[2]) << 8) |
                                   rxBuffer_[3]);
    } else {
      value = (rxBuffer_[2] != 0) ? 1 : 0;
    }
    // 排隊而非直接寫：值的落地與 Blockly 直譯器同在主迴圈，避免與其讀取競態。
    enqueue(CMD_FUNC_SET, idx, 0, static_cast<float>(value));
  }

  // [66] -> [gen][bitmap_hi][bitmap_lo]，共 5 bytes（含框架）。
  // gen 與狀態共用同一次請求，讓主機的「該重讀功能表了」偵測不必額外輪詢。
  void handleFuncStateDigital() {
    const uint16_t bits = funcs_.digitalBitmap();
    txBuffer_[0] = funcs_.generation();
    txBuffer_[1] = static_cast<uint8_t>((bits >> 8) & 0xFF);
    txBuffer_[2] = static_cast<uint8_t>(bits & 0xFF);
    txLength_ = 3;
    txReady_ = true;
  }

  // [67][page] -> [page][val i16 BE × 8]，共 19 bytes（含框架）。
  void handleFuncStateAnalog(uint8_t page) {
    int16_t values[BlocklyFunctions::kAnalogPerPage] = {0};
    const uint8_t filled =
        funcs_.analogPage(page, values, BlocklyFunctions::kAnalogPerPage);
    txBuffer_[0] = page;
    for (uint8_t i = 0; i < filled; ++i) {
      txBuffer_[1 + i * 2] = static_cast<uint8_t>((values[i] >> 8) & 0xFF);
      txBuffer_[2 + i * 2] = static_cast<uint8_t>(values[i] & 0xFF);
    }
    txLength_ = static_cast<size_t>(1 + filled * 2);
    txReady_ = true;
  }

  // ── 能力檢查（編譯期表格，callback 內呼叫安全）────────────────────
  static bool motorValid(uint8_t id) {
    return id >= 1 && id <= static_cast<uint8_t>(NUM_MOTORS);
  }

  static bool motorHasEncoder(uint8_t id) {
    return motorValid(id) && MOTOR_CAPS[id - 1].has_encoder;
  }

  // ── 延後執行佇列 ──────────────────────────────────────────────────
  // callback 端：只塞資料，不碰馬達、不列印。佇列滿時丟棄並計數——
  // 丟棄優於阻塞，阻塞會直接撐爆主機的讀取時窗。
  void enqueue(uint8_t cmd, uint8_t motor, uint8_t mode, float value) {
    const uint8_t next = static_cast<uint8_t>((qHead_ + 1) % kQueueSize);
    if (next == qTail_) {
      droppedCount_++;
      return;
    }
    queue_[qHead_].cmd = cmd;
    queue_[qHead_].motor = motor;
    queue_[qHead_].mode = mode;
    queue_[qHead_].value = value;
    qHead_ = next;
  }

  // 主迴圈端：此處允許 Serial 與較長的執行時間。
  void apply(const PendingCmd &c) {
    switch (c.cmd) {
    case CMD_FUNC_SET:
      // 主迴圈落地：此時 Blockly 直譯器不會同時讀取，無競態。
      funcs_.setValue(c.motor, static_cast<int16_t>(c.value));
      break;
    case CMD_SET_MODE:
      proc_.i2cSetMode(c.motor, c.mode);
      break;
    case CMD_SET_SPEED:
      proc_.i2cSetSpeed(c.motor, c.value);
      break;
    case CMD_SET_POSITION:
      proc_.i2cMoveToDeg(c.motor, c.value);
      break;
    case CMD_STOP_ALL:
      proc_.i2cStopAll();
      break;
    case CMD_MOTOR_HOME:
      proc_.i2cZero(c.motor);
      break;
    case CMD_MOTOR_STOP:
      proc_.i2cStopMotor(c.motor);
      break;
    case CMD_BENCH_SERVO:
      proc_.i2cSetServo(c.motor, static_cast<int>(c.value));
      break;
    case CMD_BENCH_PWM:
      proc_.i2cSetPwm(c.motor, static_cast<int>(c.value));
      break;
    default:
      break;
    }
  }

  // ── 位元組序 helper（SDD §4.2）──────────────────────────────────────
  // 直接對應 motorControl 的實作：int16 走 big-endian，float 走原生
  // little-endian memcpy。兩者不一致是既有協議的事實，不要「統一」它。
  static int16_t bytesToInt16(const uint8_t *bytes) {
    return static_cast<int16_t>((bytes[0] << 8) | bytes[1]);
  }

  static void int16ToBytes(int16_t value, uint8_t *bytes) {
    bytes[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
    bytes[1] = static_cast<uint8_t>(value & 0xFF);
  }

  static float bytesToFloat(const uint8_t *bytes) {
    float value;
    memcpy(&value, bytes, sizeof(float));
    return value;
  }

  static void floatToBytes(float value, uint8_t *bytes) {
    memcpy(bytes, &value, sizeof(float));
  }
};

#endif // I2C_SLAVE_BRIDGE_H
