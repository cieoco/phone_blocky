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
  };

  explicit I2CSlaveBridge(CommandProcessor &processor,
                          TwoWire &wireInstance = Wire)
      : proc_(processor), wire_(wireInstance) {
    instanceRef() = this;
  }

  static I2CSlaveBridge *instance() { return instanceRef(); }

  void begin(uint8_t address, int sdaPin, int sclPin) {
    wire_.begin(address, sdaPin, sclPin, I2C_SLAVE_FREQ);
    wire_.onReceive(onReceiveCallback);
    wire_.onRequest(onRequestCallback);
    address_ = address;
    lastCommMs_ = millis();
    initialized_ = true;
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

  // 主迴圈用：取診斷計數（callback 內不得列印，一律由此取出）
  Stats getStats() const {
    Stats s;
    s.rxCount = rxCount_;
    s.badCmdCount = badCmdCount_;
    s.notReadyCount = notReadyCount_;
    s.droppedCount = droppedCount_;
    s.lastCommMs = lastCommMs_;
    s.lastCommand = lastCommand_;
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
