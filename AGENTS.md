# AGENTS.md

本檔是 phone_blocky 給 AI 程式助理（Codex、Claude Code 等）的共用專案說明。
`CLAUDE.md` 透過 `@AGENTS.md` 引用本檔，**請只修改本檔**，避免兩份內容分歧。

---

## 專案定位

**phone_blocky** 是一塊 ESP32 桌上測試板，供機構組同事用手機網頁快速驗證馬達與舵機機械行為。它與 `motorControl`（主控板）並存但不互連，目標是讓命令格式與主系統語意對齊，以利機械參數移交。

詳見 [docs/專案角色說明.md](docs/專案角色說明.md)。

---

## 開發環境與常用指令

### 韌體（ESP32 / PlatformIO）

```powershell
pio run                                         # 編譯
pio run --target upload                         # 燒錄
pio run --target uploadfs                       # 上傳 LittleFS 網頁檔（data/ 目錄）
pio device monitor                              # 序列監視器（115200 baud）
pio run --target upload && pio device monitor   # 燒錄 + 監視
```

### AI 中繼服務（Python / ai-relay/，選用）

```powershell
cd ai-relay
pip install -r requirements.txt
cp .env.example .env  # 填入 Azure OpenAI 金鑰（.env 不進版控）
uvicorn main:app --reload --port 8000
```

### 根目錄 Python 工具

```powershell
python usb.py       # Tkinter 互動式 USB 序列指令
python usb_plot.py  # Tkinter + matplotlib 即時繪圖
python inst.py      # Azure 語音辨識 → 序列指令（實驗用，需本機 config.py）
```

`config.py` 只存放 `inst.py` 用的 Azure 金鑰（`KEY`、`END`），**已列入 `.gitignore`，不可再加回版控**；
新環境請自行建立。任何金鑰、密碼一律不得寫進會被 commit 的檔案。

### 桌面調校工具（motor_tuner/，選用）

PC 端 PyQt6 單馬達調參工具（PID、自動估參、曲線圖），WiFi(WS) 與 USB 兩種傳輸皆可。
執行 `motor_tuner/run.bat`，詳見 [motor_tuner/README.md](motor_tuner/README.md)。
`motor_tuner/build*/`、`dist/`、`.venv/` 為建置產物，不要修改。

---

## 驗證（修改後必跑）

全部離線執行，不需連板子：

```powershell
.venv-check/Scripts/python -m unittest discover -s tests -v   # schema / prompt / 對齊測試
node tests/check_frontend.cjs                                 # 前端（ai.html 等）邏輯
node tests/check_realtime.cjs                                 # joy.html 即時指令契約
```

- 改了 `ai-relay/prompts.py` 的 `SYSTEM_PROMPT` → 必須執行 `python tools/sync_ai_prompt.py`，把提示詞同步進 `data/ai.html`。
- 改了韌體 → 至少 `pio run` 編譯通過。
- 改了 `data/` 網頁 → 需 `uploadfs` 才會到板子上；更新 `blockly.html` 內 script 的 `?v=` 版本字串以避免手機快取。

---

## 架構總覽

### 韌體層（src/）

```
main.cpp
  ├── jsonTask   ← 套用新 PROG、執行 loop 指令、角度控制（每 10 ms）
  └── serialTask ← USB 序列：`{...}` JSON 行與 WebSocket 共用同一個 router；
                   另支援舊 M/S 純文字指令（僅 serial mode）

CommChannel.h        ← 傳輸/協定/回應出口解耦；回應與 telemetry 走回指令來源通道（WS 或 SERIAL）
CommandProcessor.h   ← 核心指令解析與馬達/舵機/感測器執行、PROG 直譯器
  ├── MotorController.h   ← AFMotor 驅動，介面：pwmSigned(±100)
  ├── ServoController.h   ← ESP32Servo，S1(GPIO5) / S2(GPIO13)
  ├── EncoderHandler.h    ← ESP32 PCNT 硬體計數，M3/M4 有編碼器
  └── PWMManager.h        ← 一般 PWM 只用 timer 0 的 0/1/8/9 通道（馬達用 timer 1/2、舵機用 timer 3）
HardwareConfig.h     ← 馬達/舵機能力表（MotorCap 等），對應 capabilities 指令

WebServerHandler.h/.cpp ← AsyncWebServer + WebSocket（ws://<ip>/ws），HTTP API 端點
ProgramStore.h       ← PROG JSON 存檔（NVS，含舊 LittleFS 遷移），支援開機自動執行
CommandTranslator.h  ← 舊格式 command:"motor_control" → 新格式 cmd:"pwm"（相容層）
```

目前沒有 PS4 手把功能；舊文件中的 PS4 描述不適用。

### 前端層（data/）

```
index.html            ← 主選單，連結至各子頁面
blockly.html          ← 頁面容器，載入以下四個 JS 模組（blockly.min.js 以 .gz 提供）
  appBlock.js   (1) 積木 UI 定義（外觀、欄位、下拉選單）
  appIr.js      (2) IR Node 類別 + 積木→IR 翻譯器
  appJson.js    (3) workspace 遍歷 → IR 陣列
  app.js        (4) IR 陣列 → PROG JSON → WebSocket 送出
blockly_test.html     ← Blockly 測試頁
joy.html + joy.js     ← 即時觸控搖桿 → cmd JSON（含角度模式）
hardware_test.html    ← 馬達/編碼器/舵機/感測器一鍵測試
ai.html               ← AI 自然語言 → PROG（瀏覽器直接呼叫 Azure；ai-relay/ 為選用獨立服務）
set.html              ← WiFi 設定、PID 與編碼器/減速比/保持參數
plotter.html          ← 即時資料繪圖
wiring.html           ← 接線圖與腳位對照
hw_config.json        ← 前端硬體元資料
```

`app_combined.js` 未被任何頁面載入，屬舊檔；修改請以四個模組為準。

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

**新格式積木**（直接生成正確指令）：

| 積木名稱 | 生成 JSON |
|----------|-----------|
| `motor_pwm` | `{ cmd:"pwm", motor, duty:±100 }` |
| `motor_speed` | `{ cmd:"speed", motor, rpm:60-250 }` ← 閉迴路 RPM（M3/M4 有編碼器） |
| `motor_stop` | `{ cmd:"stop", motor }` |
| `motor_position` | `{ cmd:"move_to", motor, deg:度數×100 }` ← 固定小數點格式，到位後保持角度 |
| `motor_move_by` | `{ cmd:"move_by", motor, deg:度數×100 }` ← 相對角度移動，到位後保持角度 |
| `motor_zero` | `{ cmd:"zero", motor }` |
| `servo_set` | `{ cmd:"servo", ch, deg }` |

**流程控制積木**（容器，可互相包，**最多 8 層**）：

| 積木名稱 | 生成 JSON |
|----------|-----------|
| `controls_if` | `{ command:"if", condition, then:[...], else:[...] }` |
| `controls_repeat_ext` | `{ command:"repeat", times:<值>, do:[...] }` ← 次數進入時求值一次 |
| `controls_whileUntil` | `{ command:"while", mode:"WHILE"\|"UNTIL", condition, do:[...] }` ← 每圈重新判斷 |

層數上限三處需一致：韌體 `PROG_MAX_BLOCK_DEPTH`（CommandProcessor.h）、前端 `MAX_BLOCK_DEPTH`（app.js）、
`platformio.ini` 的 `ARDUINOJSON_DEFAULT_NESTING_LIMIT=32`（每層積木佔 2 層 JSON，拿掉會退回只剩 3 層）。
容器同步執行；`delay` 與每圈迴圈經 `programYield()` 讓出 CPU 並維持 10ms 控制節拍，新程式到達時全部跳出。
**不要在 `programYield()` 裡呼叫 WebSocket 逾時檢查**：Blockly 頁沒有心跳，會提早停掉正常的延遲動作。
AI 子集（`prompts.py` / `schema.py` / `ai.html`）目前不產生迴圈。

其餘積木：`arduino_setup/loop`、`arduino_delay`、`logic_*`（含 `logic_operation` 且／或）、
`math_*`（`math_arithmetic` 含 MODULO、`math_abs`、`math_random_int`→`math_random`、`math_constrain`、`math_map`，整數運算）、
`arduino_ultrasonic`、`lego_button`、`arduino_digitalRead/Write`、`arduino_analogRead/Write`、`arduino_pinMode`、
`arduino_millis`、`arduino_serial_println`、`message_print`、`plot_print`。

**數值插槽**：上表積木的動力／角度／RPM，以及延遲毫秒，都是可接積木的插槽（預設放數字積木）。
只放數字時輸出整數常數（前端檢查範圍，JSON 與舊版相同）；接變數／算式時輸出表達式物件，由韌體
`parseNumericArg` / `numericArg` 在執行時求值並夾限。舊存檔（數值是 field）載入時由 `app.js`
的 `upgradeLegacyXml` 轉成插槽，新增這類積木時要一併登記到 `LEGACY_NUMBER_FIELDS`。

**副程式**：工具箱「副程式」分類由 `app.js` 的 `subroutineFlyout()` 動態產生（只放無回傳值的
`procedures_defnoreturn` / `procedures_callnoreturn`）。PROG 頂層選用 `procedures:[{name,params,body}]`，
呼叫為 `{command:"call",name,args}`；參數用 Blockly 變數 ID。韌體 `parseProgramDoc` 統一解析
setup／loop／procedures（stage 與存檔驗證共用），拒絕遞迴，呼叫算一層計入 8 層上限。
被停用（disabled）的積木不翻譯、不執行。

**不默默略過積木**：`getTranslator` 遇到不認得的積木會丟出錯誤；沒接在 setup／loop 裡的積木
不翻譯，只在執行／存檔時顯示「有 N 個積木不會執行」。新增積木時務必在 `getTranslator` 登記。

舊版「馬達」「舵機」積木已於 2026-10-09 刪除（不需讀取舊程式），含此兩積木的舊 XML 無法載入。
韌體端的舊 JSON 相容層（`CommandTranslator`、`motor_control` / `servo_control`）仍保留。

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

使用者腳位：LEGO 按鈕 4、超音波 TRIG 2 / ECHO 33、數位輸入 15、類比輸入 32。

---

## 指令格式（PROG JSON Schema）

權威定義在 [docs/prog_json_schema.md](docs/prog_json_schema.md)，改指令格式時先改它。

**直接指令（即時）：**
```json
{"cmd": "pwm",      "motor": 1, "duty": 50}
{"cmd": "speed",    "motor": 3, "rpm": 120}
{"cmd": "move_to",  "motor": 3, "deg": 9000}
{"cmd": "move_by",  "motor": 4, "deg": -4500}
{"cmd": "servo",    "ch": 1, "deg": 90}
{"cmd": "stop",     "motor": 3}
{"cmd": "zero",     "motor": 3}
```

**系統/調校指令**（見 `CommandProcessor.h` 的 router）：`ping`、`capabilities`、`read`、
`pid` / `apply_config`、`read_config`、`write_config`、`reset_config`、`set_wifi`、`set_name`、
`subscribe`（10Hz telemetry）、`chart_buffer_start` / `chart_buffer_stop`（50Hz 擷取）。

**PROG 程式（Blockly / AI 生成）：**
```json
{
  "mode": "PROG",
  "setup": [...],
  "loop":  [...]
}
```

**三處必須保持一致**：Blockly（`appBlock.js` / `appIr.js`）、AI relay（`ai-relay/prompts.py`、`ai-relay/schema.py`）、
`data/ai.html` 內嵌的 `SYSTEM_PROMPT`（由 `tools/sync_ai_prompt.py` 產生，勿手改）。
`if` 指令與感測器讀取格式尤其容易分歧。

---

## 關鍵設計約定

- **PWM 單位（`duty` 欄位）**：JSON 指令用 ±100 簽號制，正值前進，負值後退；`pwmSigned()` 內部再映射至 AFMotor 的 0–255 硬體訊號。
- **角度單位**：度（°），`TICKS_PER_DEGREE = 2.0`（輸出軸 360 PPR × 2X ÷ 360；PPR 已含減速比，不再乘 48）。程式中請用執行期 `ticksPerDegree()`，因減速比/編碼器參數可由 set.html 修改。
- **馬達速度**：RPM；Blockly 提供正向 60–250，AI 子集另允許 0 停止。底層 direct 指令保留帶符號浮點 RPM，PROG 以整數解析，兩者未硬性限制 60–250；詳見 schema 的 speed 一節。
- **PID 預設**：速度環 Kp=2.0, Ki=0.0, Kd=0.1；位置環 Kp=1.0, Ki=0.0, Kd=0.05；可由 set.html、motor_tuner 或 WebSocket API 調整。
- **舊格式相容**：`CommandTranslator` 自動轉換，新功能一律使用新格式。
- **硬體設定單一真相**：`include/config.h`（腳位/常數）和 `data/hw_config.json`（前端元資料）需同步。
- **機密**：不得 commit 金鑰；Azure 金鑰在 `ai-relay/.env`（relay）或使用者瀏覽器 localStorage（ai.html）。

---

## 重要檔案速查

| 需求 | 位置 |
|------|------|
| WiFi / 腳位 / PID 常數 | [include/config.h](include/config.h) |
| 指令解析邏輯 | [src/CommandProcessor.h](src/CommandProcessor.h) |
| 通訊通道抽象 | [src/CommChannel.h](src/CommChannel.h) |
| 指令格式（權威） | [docs/prog_json_schema.md](docs/prog_json_schema.md) |
| 直譯器架構（積木為何免編譯即跑） | [docs/直譯器架構.md](docs/直譯器架構.md) |
| 角度保持（M3/M4 伺服式 hold）作法 | [docs/角度保持.md](docs/角度保持.md) |
| Blockly 積木定義 | [data/appBlock.js](data/appBlock.js) |
| AI 提示詞 / 驗證 schema | [ai-relay/prompts.py](ai-relay/prompts.py)、[ai-relay/schema.py](ai-relay/schema.py) |
| 提示詞同步工具 | [tools/sync_ai_prompt.py](tools/sync_ai_prompt.py) |
| 5 階段重構計畫 | [docs/重新規畫計畫_2026-05-21.md](docs/重新規畫計畫_2026-05-21.md) |
| 收尾對齊清單 | [docs/收尾對齊清單.md](docs/收尾對齊清單.md) |
| 更新紀錄 | [docs/更新紀錄.md](docs/更新紀錄.md) |
| 硬體腳位圖 | [docs/腳位.md](docs/腳位.md) |
| 使用者操作說明 | [docs/操作說明書.md](docs/操作說明書.md) |
| 疑難排解 | [docs/卡住了怎麼辦.md](docs/卡住了怎麼辦.md) |
