# PROG JSON Schema (v1)

> phone_blocky `mode:"PROG"` 程式片段的權威格式。AI relay 與 ai.html 都以此為準。

## Top-level

```json
{
  "mode": "PROG",
  "setup": [<cmd>, ...],
  "loop":  [<cmd>, ...]
}
```

- `mode` 必須為字串 `"PROG"`
- `setup` 與 `loop` 皆為陣列；可空但必須存在
- `setup` 只在程式載入時跑一次
- `loop` 內元素依序執行,跑完最後一個會回到第一個 (見 `CommandProcessor::executeLoopCommands`)
- 巢狀動作僅限 `if.then` / `if.else`；不支援 `while` / `until` / `repeat`。舊 XML 可讀取，但含這些積木時無法執行或存成可執行程式。
- 一次性動作全部放 `setup`，`loop: []`；`stop` 只停馬達，**不會終止 loop**。
- 新 Blockly 輸出扁平陣列；韌體仍接受舊 `{command:"arduino_setup"|"arduino_loop", body:[...]}` 包裝。

## 指令(cmd)

每個指令是一個物件,以 `cmd` 字串選擇類型,其他鍵依類型而定。

### `pwm` — 馬達 PWM 直接驅動

```json
{ "cmd": "pwm", "motor": 1, "duty": 78 }
```

| 欄位    | 型別 | 範圍       | 說明                |
|--------|------|-----------|---------------------|
| motor  | int  | 1..4      | 馬達編號            |
| duty   | int  | -100..100 | 帶正負號 PWM 百分比 |

- `duty > 0` 正轉、`duty < 0` 反轉、`duty == 0` 視為停止
- M1/M2 無編碼器,M3/M4 有 — `pwm` 對四顆都可用

### `stop` — 停止馬達

```json
{ "cmd": "stop", "motor": 1 }
```
```json
{ "cmd": "stop" }
```

| 欄位    | 型別 | 範圍   | 說明                          |
|--------|------|--------|-------------------------------|
| motor  | int  | 0..4   | 馬達編號;0 或省略 = 停全部     |

### `move_to` / `move_by` — 角度定位（含到位後保持）

```json
{ "cmd": "move_to", "motor": 3, "deg": 9000 }
{ "cmd": "move_by", "motor": 4, "deg": -4500 }
```

| 欄位    | 型別 | 範圍   | 說明                                        |
|--------|------|--------|---------------------------------------------|
| motor  | int  | 3..4   | **僅 M3/M4 有編碼器**,直接指令回 `no_encoder`，PROG 於解析時拒絕    |
| deg    | int  | —      | 角度 × 100（固定小數點整數）,韌體除以 100 還原 |

- `move_to` 為絕對角度（相對零點）、`move_by` 為相對目前位置
- **到位後會主動保持角度**：閉迴路持續修正,被外力推動也會夾回目標（與 joy.html 角度模式同一機制 `POSITION_PHASE_HOLD`）
- 保持力道/死區由 `holdSettleDeg` / `holdKi` / `holdMaxDuty` 決定（可在 set.html 調整）
- **解除保持**：對該馬達下 `stop`（或 `pwm` / `speed` 切換模式）
- 不阻塞 loop：指令送出後立即排程,馬達在背景行進與保持,loop 接續執行下一條

### `speed` — 閉迴路轉速（RPM）

```json
{ "cmd": "speed", "motor": 3, "rpm": 120 }
```

| 欄位    | 型別 | 範圍     | 說明                              |
|--------|------|----------|-----------------------------------|
| motor  | int  | 3..4     | 僅 M3/M4 有編碼器                  |
| rpm    | int  | 60..250  | 目標轉速;`0` 視為停止              |

### `zero` — 重設角度零點

```json
{ "cmd": "zero", "motor": 3 }
```

| 欄位    | 型別 | 範圍   | 說明                          |
|--------|------|--------|-------------------------------|
| motor  | int  | 3..4   | 把目前位置設為 0°（清編碼器）  |

### `servo` — 舵機角度

```json
{ "cmd": "servo", "ch": 1, "deg": 90 }
```

| 欄位    | 型別 | 範圍   | 說明           |
|--------|------|--------|----------------|
| ch     | int  | 1..2   | 舵機通道       |
| deg    | int  | 0..180 | 目標角度       |

### `delay` — 等待

```json
{ "cmd": "delay", "ms": 1000 }
```

| 欄位    | 型別 | 範圍       | 說明           |
|--------|------|-----------|----------------|
| ms     | int  | 0..10000  | 等待毫秒數     |

- 在 `loop` 內使用 `delay` 會讓該次 loop iteration 阻塞指定時間
- `setup` 中的 `delay` 會阻塞啟動

### 感測器讀值 — `arduinoUltrasonic`

```json
{ "command": "arduinoUltrasonic", "trigPin": "2", "echoPin": "33" }
```

| 欄位 | 型別 | 範圍 | 說明 |
|------|------|------|------|
| trigPin | string \| int | 0..39 | Trig GPIO |
| echoPin | string \| int | 0..39 | Echo GPIO |

- **value-producing**:只能放在 `if.condition.left` 或 `.right`,不能單獨出現在 setup/loop
- 韌體回 -1 表示 timeout (沒回波)
- **使用 Blockly 既有形狀** (`command:` key + 字串腳位),AI 端與 Blockly 端共用同一個 parser

### `if` — 條件分支 (沿用 Blockly 形狀)

```json
{
  "command": "if",
  "condition": {
    "command": "logic_compare",
    "operator": "LT",
    "left":  { "command": "arduinoUltrasonic", "trigPin": "2", "echoPin": "33" },
    "right": 20
  },
  "then": [{ "cmd": "stop", "motor": 1 }],
  "else": [{ "cmd": "pwm",  "motor": 1, "duty": 60 }]
}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| condition.operator | string | `LT` `GT` `EQ` `NEQ` `LTE` `GTE` (Blockly 字串代號) |
| condition.left  | object \| int | 感測器讀值物件或整數常數 |
| condition.right | object \| int | 同上 |
| then | array | 條件成立時依序執行 |
| else | array | 條件不成立時執行,可省略 = 空陣列 |

- **動作指令用 `cmd:`、if/感測器用 `command:`** — 兩種 key 在同一個程式裡可混用,parser 會自動分辨
- 每次輪到這個 `if` 指令時重新讀 sensor、重新判斷；不是所有 if 在每個 tick 同時執行
- `then` / `else` 內可再放 `if`,但建議不要超過 2 層,韌體記憶體有限
- 韌體執行單一 `if` 整支跑完 (同步) 才會回到 loop 排程

### `digitalWrite` / `analogWrite` — GPIO (進階,AI 預設不產生)

```json
{ "cmd": "digitalWrite", "pin": 2, "state": "HIGH" }
{ "cmd": "analogWrite",  "pin": 26, "value": 128 }
```

> AI 支援 `pwm`、`stop`、`servo`、`delay`、`speed`、`move_to`、`move_by`、`zero` 與 `if`；不產生 GPIO 寫入。一般 analogWrite 限 GPIO 0/2/4/15/18/21/22/26/32/33，使用 timer 0 的 ch 0/1/8/9，避開馬達 timer 1/2 與舵機 timer 3。

## 範例:讓 M1 前進 2 秒後停止（只執行一次）

```json
{
  "mode": "PROG",
  "setup": [
    { "cmd": "pwm", "motor": 1, "duty": 60 },
    { "cmd": "delay", "ms": 2000 },
    { "cmd": "stop", "motor": 1 }
  ],
  "loop": []
}
```

## 範例:夾爪開關循環

```json
{
  "mode": "PROG",
  "setup": [
    { "cmd": "servo", "ch": 1, "deg": 47 }
  ],
  "loop": [
    { "cmd": "servo", "ch": 1, "deg": 138 },
    { "cmd": "delay", "ms": 1500 },
    { "cmd": "servo", "ch": 1, "deg": 47 },
    { "cmd": "delay", "ms": 1500 }
  ]
}
```

## 持久化儲存 (NVS,Blockly 與 AI 共用)

韌體用 [src/ProgramStore.h](../src/ProgramStore.h) 把目前的程式存到 ESP32 NVS,
不論是從 Blockly 還是 ai.html 存的,都是同一份資料。NVS 不受
`pio run --target uploadfs` 影響；LittleFS 僅用來存放網頁檔案。

### NVS 配置

| NVS key | 用途 |
|------|------|
| `program/json` | PROG JSON,ESP32 執行用 (autorun 讀這個) |
| `program/xml` | Blockly workspace XML,**只有從 Blockly 存才有** |
| `program/meta` | `{source: "blockly"|"ai", has_xml: bool}` |
| `program/autorun` | 是否開機自動執行 |

> 韌體首次啟動時，若偵測到舊版 LittleFS 的 `/program.*` 檔案，會自動搬移到 NVS。

### HTTP API

| Method | Path | Body | 回傳 |
|--------|------|------|------|
| `GET` | `/api/program` | — | `{ok, has_program, json, xml, meta, autorun}` |
| `POST` | `/api/program` | `{json, xml?, source}` | `{ok, source, has_xml}` |
| `DELETE` | `/api/program` | — | `{ok}` |
| `GET` | `/api/program/autorun` | — | `{ok, on}` |
| `POST` | `/api/program/autorun` | `{on: bool}` | `{ok, on}` |

POST `/api/program` 的 body 範例:

```json
{
  "json": { "mode": "PROG", "setup": [], "loop": [...] },
  "xml":  "<xml>...</xml>",
  "source": "blockly"
}
```

- `json.mode` 必須是 `"PROG"`，setup/loop 必須是陣列，且通過韌體解析與 16KB JSON 容量檢查，否則回 400；檢查不執行任何指令
- `xml` 是 optional,ai.html 不會附,Blockly 會附
- `source` 是字串標籤,顯示「這份檔是誰存的」

### 開機 autorun 行為

`main.cpp` 在 WiFi 模式啟動完後呼叫 `webServerHandler.runAutorunProgramIfEnabled()`:

```cpp
if (ProgramStore::isAutorun() && ProgramStore::exists()) {
    cmdProcessor.processCommands(ProgramStore::loadJson());
}
```

目前 `main.cpp` 啟動 WiFi/Web 後呼叫 autorun，沒有 PS4 任務／模式分支。舊 LittleFS 遷移只搬 JSON/XML/來源標籤，autorun 須透過目前 API 明確設定。

## AI 子集驗證（relay 與 ai.html）

1. `mode == "PROG"`，`setup` / `loop` 必須存在且為陣列。
2. 動作用 `cmd`，條件／讀值用 `command`；同一物件不得同時帶兩者。
3. 動作支援 `pwm`、`stop`、`servo`、`delay`、`speed`、`move_to`、`move_by`、`zero`。範圍如前表；定位 deg 限 signed 32-bit 整數。
4. `if.condition` 必須是 `logic_compare`；左右為整數、`arduinoUltrasonic` 或 `legoButton`。`then` 必填，`else` 可省略。
5. `{ "command":"legoButton", "pin":"4" }` 讀取 INPUT_PULLUP 的原始 0/1，按下的值由接線決定。超音波無回波為 -1。
6. 腳位接受 0..39 整數或只含 ASCII 數字的字串；這是格式範圍，不代表每個 GPIO 都可接外設，接線依 [腳位.md](腳位.md)。拒絕 `2junk`、空字串與布林值。
7. 指令總數含 then/else，前端最多 64；relay 預設 64（環境變數 MAX_COMMANDS 可調整，配合前端時應維持 64）。
8. 其餘多餘鍵忽略。布林與數字字串不可冒充動作的整數欄位。

AI 子集不是整個 Blockly 語言。Blockly 比較的數字積木會輸出 `{command:"math_number",number:20}`，韌體可求值；AI 直接輸出常數 `20`。兩者在韌體中等價，但 Blockly 延伸程式不必通過 AI 子集驗證。

## Blockly 延伸與必要舊格式

韌體另接受以下 `command` 形狀，供 Blockly 使用：

| 指令／表達式 | 欄位與行為 |
|---|---|
| `pinMode` | pin、mode（INPUT／OUTPUT／INPUT_PULLUP） |
| `digitalWrite` / `analogWrite` | pin + state（HIGH／LOW）或 value（0..255） |
| `delay`（舊） | delayTime；新 Blockly 輸出 cmd:delay/ms |
| `motor_control`（舊） | motor、direction（F/B/R）、speed（0..255）；舊 XML 的「馬達」積木自身使用 0..100 並轉 cmd:pwm |
| `servo_control`（舊） | servo、angle；新格式為 cmd:servo/ch/deg |
| `serial_println` / `message_print` | content 數值表達式；分別輸出序列／網頁訊息 |
| `plot_print` | series、unit、value 數值表達式 |
| `variable_declare` / `variable_set` / `math_change` | variableName；後兩者帶 value |
| `math_number` / `variable_get` | number / variableName |
| `math_arithmetic` | operator（ADD/MINUS/MULTIPLY/DIVIDE）、left、right；整數運算 |
| `logic_boolean` / `logic_negate` | value 布林 / content 表達式 |
| `logic_compare` | operator、left、right；可巢狀數值表達式 |
| `digitalRead` / `analogRead` / `legoButton` | pin |
| `arduinoUltrasonic` / `arduino_millis` | trigPin、echoPin / 無參數 |

韌體保留舊數字字串與預設值相容性，並非完整的 AI 嚴格驗證器。未知動作、非法馬達／舵機 ID、未支援迴圈會拒絕整份 PROG（`unsupported_or_invalid_program_command`），不取代正在執行的程式。缺 setup/loop 陣列回 `invalid_program_arrays`。若需要停止舊程式，送出完整空 PROG。

setup 與 if 分支同步執行；delay 以短片段等待並更新控制。新 PROG 到達時跳出等待與剩餘分支／setup，下一安全點套用新程式。這不等於即時硬體急停，實際延遲仍需實機驗證。
