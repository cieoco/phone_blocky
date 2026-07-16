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
- 巢狀僅限 `if.then` / `if.else` 兩個分支陣列 (見下方 `if` 一節)

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
| motor  | int  | 3..4   | **僅 M3/M4 有編碼器**,其餘回 `no_encoder`    |
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
- 在 `loop` 內每個 tick 都會重新讀 sensor、重新判斷
- `then` / `else` 內可再放 `if`,但建議不要超過 2 層,韌體記憶體有限
- 韌體執行單一 `if` 整支跑完 (同步) 才會回到 loop 排程

### `digitalWrite` / `analogWrite` — GPIO (進階,AI 預設不產生)

```json
{ "cmd": "digitalWrite", "pin": 5, "state": "HIGH" }
{ "cmd": "analogWrite",  "pin": 5, "value": 128 }
```

> AI 生成預設限制在 `pwm`/`stop`/`servo`/`delay` 四種,以免亂寫 GPIO 造成短路。

## 範例:讓 M1 前進 2 秒後停止

```json
{
  "mode": "PROG",
  "setup": [],
  "loop": [
    { "cmd": "pwm",   "motor": 1, "duty": 60 },
    { "cmd": "delay", "ms": 2000 },
    { "cmd": "stop",  "motor": 1 },
    { "cmd": "delay", "ms": 1000 }
  ]
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

- `json.mode` 必須是 `"PROG"`,否則回 400
- `xml` 是 optional,ai.html 不會附,Blockly 會附
- `source` 是字串標籤,顯示「這份檔是誰存的」

### 開機 autorun 行為

`main.cpp` 在 WiFi 模式啟動完後呼叫 `webServerHandler.runAutorunProgramIfEnabled()`:

```cpp
if (ProgramStore::isAutorun() && ProgramStore::exists()) {
    cmdProcessor.processCommands(ProgramStore::loadJson());
}
```

PS4 模式不執行 autorun (那時 Web 伺服器也不會啟動)。

## 驗證規則 (relay/ai.html 兩端皆套用)

1. 頂層 `mode == "PROG"`、`setup`/`loop` 為陣列
2. 每個指令物件必須有 `cmd` 字串
3. setup/loop 內每個物件必須是:
   - `cmd:` 動作指令 (`pwm`, `stop`, `servo`, `delay`),或
   - `command:` Blockly 形狀 (目前只 `if`)
4. `if.condition` 必須是 `logic_compare` 物件;`condition.left`/`right` 是感測器物件或整數,感測器目前只支援 `arduinoUltrasonic`
5. 各欄位範圍如上表
6. 多餘的鍵會被忽略 (韌體端使用 ArduinoJson 預設值機制)
7. 指令總數 (含 `then`/`else` 內的) 建議 ≤ 64,避免 ESP32 記憶體吃緊
