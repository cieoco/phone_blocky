# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## 專案定位

**phone_blocky** 是一塊 ESP32 桌上測試板，供機構組同事用手機網頁快速驗證馬達與舵機機械行為。它與 `motorControl`（主控板）並存但不互連，目標是讓命令格式與主系統語意對齊，以利機械參數移交。

詳見 [docs/專案角色說明.md](docs/專案角色說明.md)。

---

## 開發環境與常用指令

### 韌體（ESP32 / PlatformIO）

```powershell
# 編譯
pio run

# 燒錄
pio run --target upload

# 上傳 LittleFS 網頁檔（data/ 目錄）
pio run --target uploadfs

# 開啟序列監視器（115200 baud）
pio device monitor

# 一次完成燒錄 + 監視
pio run --target upload && pio device monitor
```

### AI 中繼服務（Python / ai-relay/）

```powershell
cd ai-relay
pip install -r requirements.txt
cp .env.example .env  # 填入 Azure OpenAI 金鑰
uvicorn main:app --reload --port 8000
```

### 序列測試腳本（根目錄 Python 工具）

```powershell
python usb.py       # 互動式 USB 序列指令
python usb_plot.py  # 即時繪圖（SysId 資料）
python config.py    # 快速設定工具
```

---

## 架構總覽

### 韌體層（src/）

```
main.cpp
  ├── jsonTask  ← WebSocket 接收 JSON 指令 → CommandProcessor
  ├── ps4Task   ← PS4 藍芽輸入 → CommandProcessor
  └── serialTask ← USB 序列輸入（除錯用）

CommandProcessor.h   ← 核心指令解析與馬達/舵機/感測器執行
  ├── MotorController.h   ← AFMotor 驅動，介面：pwmSigned(±100)
  ├── ServoController.h   ← ESP32Servo，S1(GPIO5) / S2(GPIO13)
  ├── EncoderHandler.h    ← ESP32 PCNT 硬體計數，M3/M4 有編碼器
  └── PWMManager.h        ← 動態 LED/PWM 通道管理（最多 16 通道）

WebServerHandler.h   ← AsyncWebServer + WebSocket，HTTP API 端點
ProgramStore.h       ← PROG JSON 存檔（LittleFS），支援自動執行
CommandTranslator.h  ← 舊格式 command:"motor_control" → 新格式 cmd:"pwm"（相容層）
PS4ControllerHandler.h / PS4SettingsManager.h  ← PS4 藍芽手把
```

### 前端層（data/）

```
blockly.html  ← 頁面容器，載入以下三個 JS 模組
  appBlock.js   (1) 積木 UI 定義（外觀、欄位、下拉選單）
  appIr.js      (2) IR Node 類別 + 積木→IR 翻譯器
  appJson.js    (3) workspace 遍歷 → IR 陣列
  app.js        (4) IR 陣列 → PROG JSON → WebSocket 送出

joy.html + joy.js            ← 即時觸控搖桿 → cmd JSON
hardware_test.html           ← 馬達/感測器硬體一鍵測試
ai.html                      ← AI 自然語言輸入（接 ai-relay/ 服務）
set.html                     ← WiFi / AP / 管理員密碼設定
ps4_settings.html            ← PS4 手把按鍵映射設定
index.html                   ← 主選單，連結至各子頁面
```

#### Blockly 四步流程

```
使用者拖拉積木
  ↓ parseWorkspaceToIR()           [appJson.js]
    getTranslator(block.type)(block) → IR Node 物件   [appIr.js]
  ↓ node.toJson()                  [appIr.js 各 Node 類別]
    每個積木對應一條 JSON 指令
  ↓ runBlocklyCode()               [app.js]
    { mode:'PROG', setup:[...], loop:[...] }
  ↓ WebSocket
    CommandProcessor（韌體）
```

**新格式積木**（Phase A 對齊後，直接生成正確指令）：

| 積木名稱 | 生成 JSON |
|----------|-----------|
| `motor_pwm` | `{ cmd:"pwm", motor, duty:±100 }` |
| `motor_speed` | `{ cmd:"speed", motor, rpm:60-250 }` ← 閉迴路 RPM（M3/M4 有編碼器） |
| `motor_stop` | `{ cmd:"stop", motor }` |
| `motor_position` | `{ cmd:"move_to", motor, deg:度數×100 }` ← 固定小數點格式，到位後保持角度 |
| `motor_move_by` | `{ cmd:"move_by", motor, deg:度數×100 }` ← 相對角度移動，到位後保持角度 |
| `motor_zero` | `{ cmd:"zero", motor }` |
| `servo_set` | `{ cmd:"servo", ch, deg }` |

**舊格式積木**（`馬達`、`舵機`）保留為相容 shim，透過 `CommandTranslator` 轉換，新功能勿用。

> `move_to` 與 `move_by` 的 `deg` 欄位單位均為「度 × 100」的整數（固定小數點），韌體端除以 100 還原。
>
> **到位後保持角度**：`move_to` / `move_by`（含直接指令與 joy.html 角度模式）到位後一律進入 `POSITION_PHASE_HOLD`，閉迴路持續修正、被外力推動會夾回目標。保持力道/死區由 `holdSettleDeg` / `holdKi` / `holdMaxDuty` 決定（set.html 可調）。要解除保持，對該馬達下 `stop`（或 `pwm` / `speed` 切換模式）。

### 硬體能力（hw_config.json）

| 元件 | ID | 有編碼器 | 腳位 |
|------|----|----------|------|
| M1 | afm=1 | ✗ | — |
| M2 | afm=2 | ✗ | — |
| M3 | afm=3 | ✓ | A=35, B=34 |
| M4 | afm=4 | ✓ | A=36, B=39 |
| S1 | — | — | GPIO 5 |
| S2 | — | — | GPIO 13 |

---

## 指令格式（PROG JSON Schema）

權威定義在 [docs/prog_json_schema.md](docs/prog_json_schema.md)。

**直接指令（即時）：**
```json
{"cmd": "pwm",      "motor": 1, "duty": 50}
{"cmd": "speed",    "motor": 3, "rpm": 120}
{"cmd": "move_to",  "motor": 3, "deg": 90}
{"cmd": "move_by",  "motor": 4, "deg": -45}
{"cmd": "servo",    "servo": 1, "deg": 90}
{"cmd": "stop",     "motor": 3}
```

**PROG 程式（Blockly / AI 生成）：**
```json
{
  "type": "prog",
  "setup": [...],
  "loop":  [...]
}
```

`if` 指令與感測器讀取的 JSON 格式需在 Blockly（appBlock.js）和 AI relay（prompts.py）之間保持一致。

---

## 關鍵設計約定

- **PWM 單位（`duty` 欄位）**：JSON 指令用 ±100 簽號制，正值前進，負值後退；`pwmSigned()` 內部再映射至 AFMotor 的 0–255 硬體訊號。
- **角度單位**：度（°），`TICKS_PER_DEGREE = 2.0`（因 48:1 減速比 × 360 PPR × 2X 解碼）。
- **馬達速度**：RPM，有效範圍 60–250。
- **PID 預設**：Kp=2.0, Ki=0.0, Kd=0.1，可由 set.html 或 WebSocket API 即時調整。
- **舊格式相容**：`CommandTranslator` 自動轉換，新功能一律使用新格式。
- **硬體設定單一真相**：`include/config.h`（腳位/常數）和 `data/hw_config.json`（前端元資料）需同步。

---

## 重要檔案速查

| 需求 | 位置 |
|------|------|
| WiFi / 腳位 / PID 常數 | [include/config.h](include/config.h) |
| 指令解析邏輯 | [src/CommandProcessor.h](src/CommandProcessor.h) |
| 直譯器架構（積木為何免編譯即跑） | [docs/直譯器架構.md](docs/直譯器架構.md) |
| 角度保持（M3/M4 伺服式 hold）作法 | [docs/角度保持.md](docs/角度保持.md) |
| Blockly 積木定義 | [data/appBlock.js](data/appBlock.js) |
| AI 提示詞 | [ai-relay/prompts.py](ai-relay/prompts.py) |
| 5 階段重構計畫 | [docs/重新規畫計畫_2026-05-21.md](docs/重新規畫計畫_2026-05-21.md) |
| 硬體腳位圖 | [docs/腳位.md](docs/腳位.md) |
| 使用者操作說明 | [docs/操作說明書.md](docs/操作說明書.md) |
