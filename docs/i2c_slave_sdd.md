# phone_blocky I2C 從機介面 SDD

> 目標：讓 phone_blocky 桌上測試板掛上 i2cESP32 主機的 I2C 匯流排，成為可被主機控制的從機，
> 控制語法沿用 motorControl 的既有 I2C 契約。
>
> 狀態：**設計定案，尚未實作**。日期 2026-08-15。

---

## 1. 範圍

| 項目 | 內容 |
|---|---|
| 涉及 repo | `phone_blocky`（新增從機）、`i2cESP32`（新增主機端登記） |
| 不涉及 | `motorControl`（完全不動；只作為協議來源被引用） |
| 新增能力 | 主機可透過 I2C 驅動測試板 M1-M4、S1-S2，並讀回狀態 |
| 保留能力 | 手機網頁 / Blockly / 搖桿 / PS4 全部維持（見 §7 WiFi 取捨） |

協議權威來源（**唯讀引用，不得修改**）：

- [motorControl/src/config.h](../../motorControl/src/config.h) L147-202 — 命令碼表
- [motorControl/docs/architecture/I2C_FRAME_V2.md](../../motorControl/docs/architecture/I2C_FRAME_V2.md) — 回應框架
- [motorControl/src/middleware/i2c_slave.cpp](../../motorControl/src/middleware/i2c_slave.cpp) — 參考實作

---

## 2. 決策紀錄

| # | 決策 | 結論 | 理由 |
|---|---|---|---|
| D1 | WiFi 與 I2C 並存 | **雙 env，先做 WiFi OFF 版拿基準**；AP-only 並存列第二階段，過 soak 才收 | 見 §7 |
| D2 | 舵機命令碼 | **新開 `0x60` 區塊**，不借用 `CMD_ARM_CONTROL` component | 角色專屬功能自己開一塊，比照 ball sorter 的 `0x50-0x56` 先例 |
| D3 | I2C 位址 | **0x36** | 延續 0x30/0x34/0x35 角色序列 |
| D4 | M1/M2 開迴路驅動 | **`CMD_BENCH_PWM 0x61`**，收進 D2 的同一區塊 | motorControl 全表無 PWM 命令，而 M1/M2 無編碼器只能開迴路；依 D2 原則處理 |

D4 是寫規格時才浮現的缺口，依 D2 已核可的原則推導而來。

---

## 3. 拓樸

```
i2cESP32 (NodeMCU-32S, Master, GPIO21/22, 100kHz)
  ├── 0x30  motorControl  ROLE_MECANUM_CHASSIS
  ├── 0x34  motorControl  ROLE_ARM
  ├── 0x35  motorControl  ROLE_BALL_SORTER
  ├── 0x36  phone_blocky  桌上測試板        ← 本案新增
  └── 0x3C  OLED (U8g2)
```

phone_blocky 為 Wemos D1 R32，SDA/SCL 使用 **GPIO21/22**（目前完全空置，見 [腳位.md](腳位.md)）。

> **接線前必檢**：AFMotor shield 實體是否讓出 Uno 排針的 SDA/SCL。這點無法由程式碼推論，
> 必須用萬用電表或 I2C scanner 實測確認。

---

## 4. I2C 契約

### 4.1 回應框架（沿用 V2，不得偏離）

所有從機回應統一為：

```
[STATUS][CMD_ECHO][payload ...][CRC8?]
```

- `STATUS`：`0x00` OK ／ `0x01` NOT_READY（無 staged 回應）／ `0x02` BAD_CMD
- `CMD_ECHO`：本回應對應的命令碼；主機 `I2CTransport` 會驗證，不符記為 `wrong_resp` 並重試
- `CRC8`：**目前兩端皆關閉**（`I2C_TRANSPORT_FRAME_CRC` 預設 0，無任何 env 開啟）。
  phone_blocky 必須使用同一個編譯開關，**嚴禁單邊硬啟用**
- staged 回應 50ms 過期窗口，逾期改回 NOT_READY

**線上回應總長 ≤ 20 bytes（硬上限，含框架標頭與 CRC）。** 違反此上限的歷史事故為
loop time 飆至 1014ms（CLAUDE.md 硬體禁區）。

### 4.2 位元組序（重要陷阱）

motorControl 既有實作中兩種編碼並存，**必須逐命令對照，不可整體套用單一規則**：

| 型別 | 編碼 | 來源 |
|---|---|---|
| `int16` | **Big-endian**（`bytes[0]=高位`） | `int16ToBytes()` |
| `float` | **Little-endian 原生 memcpy** | `bytesToFloat()` / `floatToBytes()` |
| `uint16LE` / `int16LE` | Little-endian（少數命令專用） | `bytesToUInt16LE()` 等 |

phone_blocky 實作時直接複製 motorControl 的四個 helper，不要自己重寫。

### 4.3 沿用命令（語意與 motorControl 完全一致）

| CMD | 請求封包 | 回應 payload | phone_blocky 行為 |
|---|---|---|---|
| `0x01` PING | `[01][seq]` | `[A5][seq^5A]` 2B | 直接照抄 |
| `0x02` GET_INFO | `[02]` | 馬達數等 | 回報 4 馬達 / 2 舵機 |
| `0x10` SET_MODE | `[10][id][mode]` | — | mode: 0=IDLE 1=SPEED 2=POSITION |
| `0x11` SET_SPEED | `[11][id][float LE ×4]` | — | RPM 閉迴路，**僅 M3/M4** |
| `0x12` SET_POSITION | `[12][id][float LE ×4]` | — | 絕對角度（度），**僅 M3/M4** |
| `0x21` STOP_ALL | `[21]` | — | 全部停 |
| `0x22` MOTOR_HOME | `[22][id]` | — | 對映 `zero` |
| `0x23` MOTOR_STOP | `[23][id]` | — | 單顆停 |
| `0x26` GET_STATUS | `[26][id]` | 11B（見下） | 完整填 |
| `0x27` GET_ALL_STATUS | `[27][page]` | 12B，每頁 2 馬達 | 完整填 |
| `0x42` HEARTBEAT | `[42]` | — | 餵看門狗 |

`0x26` payload 佈局（11B，全部 int16 為 BE）：

```
[motorId][mode][pos_deg i16][tgt_deg i16][rpm i16][tgt_rpm i16][flags]
```

精度 1°／1 RPM。高精度調參仍走原本的 WebSocket / Serial JSON，不受此限。

**不沿用**：`0x20 SET_ALL_MOTORS` 假設四顆馬達皆可 RPM 閉迴路，phone_blocky 只有兩顆，
語意不成立。收到時回 `BAD_CMD`。`0x24`-`0x2F` 底盤位姿系列、`0x40`/`0x41` 氣壓與手臂、
`0x50` 區塊分球器，全部與本板無關，一律 `BAD_CMD`。

### 4.4 新增 bench 區塊（0x60-0x6F）

於 phone_blocky 與 i2cESP32 兩端各自定義，**不寫回 motorControl**。

| CMD | 封包 | 說明 |
|---|---|---|
| `0x60` BENCH_SERVO | `[60][ch][deg i16 BE]` | ch=1\|2，deg 0-180，超界 clamp |
| `0x61` BENCH_PWM | `[61][id][duty i16 BE]` | id=1-4，duty ±100 簽號制 |
| `0x62` BENCH_GET_SERVO | `[62]` → `[s1_deg][s2_deg]` 2B | 舵機角度回讀 |
| `0x63`-`0x6F` | — | 保留 |

封包形狀刻意與 `CMD_ARM_CONTROL`（`[cmd][component][int16 BE]`）一致，沿用慣例但語意獨立。

`0x61` 的 duty 單位與 phone_blocky 既有 `cmd:"pwm"` 的 `duty` 完全相同（±100），
直接進 `MotorController::pwmSigned()`。

### 4.5 回應長度預算

| 回應 | payload | +標頭 | 合計 | 判定 |
|---|---|---|---|---|
| PING | 2 | 2 | 4 | OK |
| GET_STATUS | 11 | 2 | 13 | OK |
| GET_ALL_STATUS | 12 | 2 | 14 | OK |
| BENCH_GET_SERVO | 2 | 2 | 4 | OK |
| NOT_READY / BAD_CMD | 0 | 2 | 2 | OK |

全部合規，且無單筆逼近 20B 上限。

---

## 5. 單位換算對照

phone_blocky 內部與 I2C 線上的單位不同，橋接層必須換算：

| 量 | phone_blocky JSON | I2C 線上 | 換算 |
|---|---|---|---|
| 角度目標 | 整數「度 × 100」 | `float` 度 | **÷100 / ×100，漏做會差 100 倍** |
| 轉速 | `float` RPM | `float` RPM | 無 |
| PWM duty | 整數 ±100 | `int16` ±100 | 無 |
| 舵機角度 | 整數 0-180 | `int16` 0-180 | 無 |
| 狀態回報角度 | 內部 ticks | `int16` 度 | `ticksToCentiDeg() / 100` |

---

## 6. 硬體能力矩陣

| 馬達 | 編碼器 | PWM `0x61` | SPEED `0x11` | POSITION `0x12` |
|---|---|---|---|---|
| M1 | ✗ | ✓ | `BAD_CMD` | `BAD_CMD` |
| M2 | ✗ | ✓ | `BAD_CMD` | `BAD_CMD` |
| M3 | ✓ (35/34) | ✓ | ✓ | ✓ |
| M4 | ✓ (36/39) | ✓ | ✓ | ✓ |

對無編碼器馬達下閉迴路命令，一律回 `BAD_CMD`，**不得靜默退化成 PWM**——
靜默退化會讓上位機以為閉迴路生效，是比明確拒絕更糟的失敗模式。

---

## 7. WiFi 取捨（D1）

### 7.1 根因

Arduino core 2.0.17 `esp32-hal-i2c-slave.c` 實作：

- ISR 只把事件排入 queue（L623/700/712）
- `onReceive` 與 `onRequest` **兩者都在 `i2c_slave_task` 中執行**（L826/833）
- 該 task 由 `xTaskCreate(..., 20, ...)` 建立（L282）：**優先權 20，且未綁核心**
- ESP-IDF WiFi task 優先權為 **23**

結論：**WiFi task 可搶佔 I2C slave task，且無法用分核規避。** 這是排程層問題，
不是「callback 寫得夠輕量」能解決的。2026-07-07 實測底盤從機開 WiFi STA 造成
主機 loop time 1-2s、I2C rate 掉到 1-2Hz，即此機制。

### 7.2 對策

| env | WiFi | 用途 |
|---|---|---|
| `esp32dev` | AP_STA（現況） | 手機網頁 / Blockly / 桌上驗證，**不接主機** |
| `esp32dev_i2c` | OFF | 掛主機 I2C 匯流排 |

第一階段只做 `esp32dev_i2c`，取得無 WiFi 干擾的乾淨基準。
第二階段再評估 AP-only 並存，**須通過 §9 驗收才可收進正式 env**。

同時將 `platform = espressif32` 釘版（現況未釘；本機並存 3.0.0 / 6.13.0 / 舊 core 1.0.4）。

---

## 8. 實作設計

### 8.1 phone_blocky（新增）

| 檔案 | 動作 |
|---|---|
| `src/I2CSlaveBridge.h` | **新增**。框架層照抄 motorControl `i2c_slave.cpp`（onReceive/onRequest/staged/V2 標頭/byte helper），handler 改呼叫 `CommandProcessor` |
| `src/CommandProcessor.h` | 將 `setMotorDuty` / `startMotorPositionMove` / `stopMotorRuntime` / `zeroMotorPosition` 等既有 private 方法（L306-450）提升為 public 輕量介面 |
| `src/main.cpp` | `setup()` 內依編譯旗標初始化橋接層 |
| `include/config.h` | 新增 `I2C_SLAVE_ADDR` / `I2C_SDA_PIN` / `I2C_SCL_PIN`（**受限檔案**） |
| `platformio.ini` | 新增 `esp32dev_i2c` env、釘 platform 版本 |
| `docs/腳位.md` | 補 GPIO21/22 佔用 |

**設計約束**：橋接層**嚴禁**走「組 JSON 字串 → `processCommands()`」捷徑。該路徑會在
I2C callback 內配置 `DynamicJsonDocument(16384)`，且每個指令都會呼叫 `sendOk()` 推 WebSocket。
callback 內不得有 heap 配置、`Serial.printf`、WebSocket 推送。回應只填 `txBuffer_`。

**延後執行佇列（實作時新增的設計）**：`CommandProcessor` 的
`startMotorPositionMove()` / `zeroMotorPosition()` 內含 `Serial.printf`，
不能在 callback 直接呼叫。因此命令分兩類處理：

| 類別 | 命令 | 執行位置 |
|---|---|---|
| 動作類 | `0x10` `0x11` `0x12` `0x21` `0x22` `0x23` `0x60` `0x61` | callback 只解析 + 排隊，`jsonTask` 呼叫 `service()` 落地 |
| 唯讀類 | `0x01` `0x02` `0x26` `0x27` `0x62` | callback 內同步作答（純記憶體讀取） |

佇列為 16 格 SPSC 環形緩衝（生產者 = `i2c_slave_task`，消費者 = `jsonTask`），
無鎖。滿載時丟棄並計入 `dropped`——**丟棄優於阻塞**，阻塞會直接撐爆主機讀取時窗。
此作法沿用專案既有的 `applyPendingProgramIfAny()` 跨任務交接慣例。

代價：動作類命令有最多一拍（10ms）延遲，且回應語意是「已接受」而非「已執行」。
對 50-100ms 級的 teleop 串流可接受。

合法性檢查（馬達編號、是否具編碼器）仍在 callback 內完成，`BAD_CMD` 才能即時回報。

### 8.2 i2cESP32（新增）

| 檔案 | 動作 |
|---|---|
| `include/config.h` | 新增 `BENCH_I2C_ADDR 0x36`；加入 `I2C_STARTUP_ADDRS`；`I2C_STARTUP_ADDRS_COUNT` 3→4（**受限檔案**） |
| `lib/MotorControl/include/motor_control.h` | `MotorControllerRole` 新增 `Bench`；新增 `benchServo()` / `benchPwm()` / `readBenchServo()` 三個方法 |
| `lib/MotorControl/src/motor_control.cpp` | 三個方法實作，比照 `setArmState()`（約 30 行） |
| `src/RobotContainer.cpp` | 建構列表新增 `DriveMotorController m_benchController(m_transport, false, BENCH_I2C_ADDR)`；`configureMotors()` 依 `startupI2CDevicePresent()` 可選註冊，比照 ball sorter（**受限檔案**） |
| Dashboard | 第二階段再議 |

沿用命令（§4.3）在主機端**零新增程式碼**——`DriveMotorController` 已具備。
主機目前不送 `0x10`/`0x11`/`0x12`，若需單顆馬達閉迴路控制，須另補主機端方法。

---

## 9. 分階段計畫與驗收

| 階段 | 內容 | 程式 | 硬體驗收 |
|---|---|---|---|
| S1 | 協議定案（本文件） | ✅ | — |
| S2 | PING + GET_INFO + V2 框架 | ✅ | ⬜ 主機開機掃描列出 0x36 |
| S3 | 沿用馬達命令（0x10/0x11/0x12/0x21/0x22/0x23） | ✅ | ⬜ M3/M4 閉迴路可動；M1/M2 回 BAD_CMD |
| S4 | 狀態回報（0x26/0x27） | ✅ | ⬜ 主機讀值與網頁顯示一致 |
| S5 | bench 區塊（0x60/0x61/0x62） | ✅ | ⬜ S1/S2 可動；M1-M4 PWM 可動 |
| S6 | Soak 驗收 | — | ⬜ 見下 |

> **S2-S5 目前皆為「編譯通過、未經任何硬體驗證」。** 兩個 env（`esp32dev` /
> `esp32dev_i2c`）均 `pio run` 成功，但從未燒錄、從未上匯流排。
> 硬體驗收欄全部未打勾，不得視為完成。

S6 驗收標準（沿用 P1-A Gate）：

- 30s 以上 soak：`lost=0` / `ping_failed=0` / `mismatch=0` / `wrong_resp=0` / `brownout=0`
- 主機 loop time 無 >100ms 事件
- I2C 穩態 rate ≥ 50Hz
- 既有三個角色（0x30/0x34/0x35）功能不退化
- 兩端 `pio run` 全 env 編譯通過

---

## 10. 分支與 commit 邊界

| repo | 分支 |
|---|---|
| `phone_blocky` | `feat/i2c-slave-bridge` |
| `i2cESP32` | `feat/bench-board-i2c` |

**嚴禁跨 repo 混合提交。** 順序：先 phone_blocky 到可被掃描（S2），再開主機整合。

**開工前置**：CLAUDE.md 規範 #3 要求無未提交變動才能開始新任務。目前
`phone_blocky` 有一個 docs 檔未提交、`i2cESP32` 的 `platformio.ini` 標記為已修改，
須先處理。

---

## 11. 未決事項

1. 主機端是否需要單顆馬達閉迴路控制介面（目前主機無 `0x10`/`0x11`/`0x12` 送出路徑）
2. Dashboard 卡片與遙測欄位（第二階段）
3. AP-only 並存是否採用（S6 實測後決定）
4. 是否需要手動命令看門狗（比照底盤 500ms 停輪）
