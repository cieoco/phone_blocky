# phone_blocky

一塊 ESP32 桌上測試板的韌體與網頁介面，供機構組同事用**手機網頁**快速驗證馬達與舵機的機械行為。
提供 Blockly 積木程式、即時觸控搖桿、AI 自然語言指令與硬體一鍵測試等操作方式，
命令格式刻意與主控系統（motorControl）語意對齊，以利機械參數移交。

> 本板與主控板並存但**不互連**，定位為獨立的桌上驗證工具。

## 功能

- 🧩 **Blockly 積木程式** — 拖拉積木即時翻譯為指令並透過 WebSocket 送出（免編譯直譯）
- 🕹️ **觸控搖桿** — 手機即時控制馬達/舵機
- 🤖 **AI 自然語言** — 經 ai-relay 服務將中文描述轉為程式（選用）
- 🔧 **硬體測試頁** — 馬達、編碼器、舵機、感測器一鍵驗證
- 💾 程式存檔於 ESP32 LittleFS，支援開機自動執行

## 頁面一覽

韌體開機後由主選單 [`index.html`](data/index.html) 連結至各子頁面：

| 頁面 | 說明 |
|------|------|
| `index.html` | 主選單 |
| `blockly.html` | Blockly 積木程式編輯與執行 |
| `joy.html` | 即時觸控搖桿控制 |
| `hardware_test.html` | 馬達 / 編碼器 / 舵機 / 感測器功能檢測 |
| `ai.html` | AI 自然語言輸入（需 ai-relay 服務）|
| `set.html` | WiFi / AP 名稱 / 管理密碼設定 |
| `wiring.html` | 接線圖與腳位對照 |
| `plotter.html` | 即時資料繪圖輸出 |

## 硬體需求

- **主控**：ESP32 開發板（PlatformIO 環境 `esp32dev`）
- **馬達驅動**：AFMotor（Adafruit Motor Shield 相容）
- **馬達**：DC 馬達 ×4（M3 / M4 附增量式編碼器）
- **舵機**：S1 / S2（可選）
- **感測器**（測試頁用，可選）：LEGO 按鈕、超音波（HC-SR04）、數位/類比輸入

腳位對照如下（詳見 [docs/腳位.md](docs/腳位.md)）：

| 元件 | ID | 編碼器 | 腳位 |
|------|----|--------|------|
| M1 / M2 | afm=1,2 | ✗ | — |
| M3 | afm=3 | ✓ | A=35, B=34 |
| M4 | afm=4 | ✓ | A=36, B=39 |
| S1 / S2 | — | — | GPIO 5 / 13 |

## 快速開始

### 1. 編譯與燒錄（PlatformIO）

```powershell
pio run                      # 編譯
pio run --target upload      # 燒錄韌體
pio run --target uploadfs    # 上傳 data/ 網頁至 LittleFS（韌體與網頁需分別上傳）
pio device monitor           # 序列監視器（115200 baud）
```

> 首次或修改 `data/` 網頁後，記得執行 `uploadfs`，否則網頁不會更新。
> 安裝與環境設定詳見 [docs/安裝教程.md](docs/安裝教程.md)。

### 2. 連線使用

韌體以 **AP + STA 雙模式** 啟動：

1. 手機/電腦連上熱點 **`ESP32-XXXX`**（`XXXX` 為板子 MAC 末四碼），預設密碼 `12345678`
2. 瀏覽器開 **`http://192.168.4.1`**，即進入主選單
3. （選用）在 `set.html` 填入你的路由器 WiFi，板子便會同時連上區網；
   序列埠（115200 baud）會印出取得的 IP，之後也能用該 IP 從區網連線

操作細節見 [docs/操作說明書.md](docs/操作說明書.md)。

### Wi-Fi 設定

[`include/config.h`](include/config.h) 中的 `CONFIG_STA_SSID` / `CONFIG_STA_PASSWORD` **預設留空**，
請勿把真實憑證提交進版控。可於本地填入，或留空後透過 `set.html` 設定（存於 NVS/Preferences）。
AP 密碼與管理密碼亦定義於 `config.h`，正式部署請自行更換。

### AI 中繼服務（選用）

`ai.html` 需搭配 `ai-relay/` 的 Python 服務（Azure OpenAI）。設定方式見 [ai-relay/README.md](ai-relay/README.md)。

## 桌面調校工具（motor_tuner，選用）

[`motor_tuner/`](motor_tuner/) 是一個跑在 PC 上的 **PyQt6 桌面應用**，用於較深入的單馬達調參：
單馬達測試、速度/位置環 **PID 調校**、**自動估 PID**、即時 **RPM/位置曲線圖**、換板複製參數與內建燒錄。
走 phone_blocky 的 `cmd:` 協定，**WiFi(WS) 與 USB 序列**兩種傳輸等價。

- 網頁 UI（`data/`）跑在 ESP32 上、供手機快速驗證；motor_tuner 是 PC 端的工程調參工具，兩者互補。
- 閉迴路（轉速/位置）與曲線只對有編碼器的 **M3/M4** 有意義。
- 執行：雙擊 `motor_tuner/run.bat`（首次自動建 `.venv`），詳見 [motor_tuner/README.md](motor_tuner/README.md)。

## 文件

| 文件 | 說明 |
|------|------|
| [docs/專案角色說明.md](docs/專案角色說明.md) | 專案定位與設計背景 |
| [docs/操作說明書.md](docs/操作說明書.md) | 使用者操作說明 |
| [docs/安裝教程.md](docs/安裝教程.md) | 開發環境安裝與燒錄 |
| [docs/腳位.md](docs/腳位.md) | 硬體腳位圖 |
| [docs/prog_json_schema.md](docs/prog_json_schema.md) | 指令 JSON 格式（PROG Schema）|
| [docs/直譯器架構.md](docs/直譯器架構.md) | 積木免編譯直譯架構 |
| [docs/角度保持.md](docs/角度保持.md) | M3/M4 伺服式角度保持作法 |
| [CLAUDE.md](CLAUDE.md) | 開發規範與架構總覽 |

## 授權

本專案自有程式碼採 **MIT License**，見 [LICENSE](LICENSE)。

### 致謝 / Acknowledgements

本專案使用下列開源元件，完整聲明見 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)：

- **[JoyStick](https://github.com/bobboteck/JoyStick)** by Roberto D'Amico (Bobboteck) — MIT License
  （觸控搖桿 UI，`data/joy.js` / `data/joy.css`）
- **[Blockly](https://github.com/google/blockly)** by Google — Apache License 2.0
  （積木程式引擎，`data/blockly.min.js.gz`）
