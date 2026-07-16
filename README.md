# phone_blocky

一塊 ESP32 桌上測試板的韌體與網頁介面，供機構組同事用**手機網頁**快速驗證馬達與舵機的機械行為。
提供 Blockly 積木程式、即時觸控搖桿、AI 自然語言指令與硬體一鍵測試等操作方式，
命令格式刻意與主控系統（motorControl）語意對齊，以利機械參數移交。

> 本板與主控板並存但**不互連**，定位為獨立的桌上驗證工具。

## 功能

- 🧩 **Blockly 積木程式** — 拖拉積木即時翻譯為指令並透過 WebSocket 送出（免編譯直譯）
- 🕹️ **觸控搖桿** — 手機即時控制馬達/舵機
- 🤖 **AI 自然語言** — 經 ai-relay 服務將中文描述轉為程式（選用）
- 🔧 **硬體測試頁** — 馬達、編碼器、感測器一鍵驗證
- 💾 程式存檔於 ESP32 LittleFS，支援開機自動執行

## 硬體

| 元件 | ID | 編碼器 | 腳位 |
|------|----|--------|------|
| M1 / M2 | afm=1,2 | ✗ | — |
| M3 | afm=3 | ✓ | A=35, B=34 |
| M4 | afm=4 | ✓ | A=36, B=39 |
| S1 / S2 | — | — | GPIO 5 / 13 |

## 建置（PlatformIO / ESP32 esp32dev）

```powershell
pio run                      # 編譯
pio run --target upload      # 燒錄韌體
pio run --target uploadfs    # 上傳 data/ 網頁至 LittleFS
pio device monitor           # 序列監視器（115200 baud）
```

### Wi-Fi 設定

`include/config.h` 中的 `CONFIG_STA_SSID` / `CONFIG_STA_PASSWORD` **預設留空**，
請勿把真實憑證提交進版控。可於本地填入，或留空後透過 `set.html` 設定（存於 NVS/Preferences）。

## 授權

本專案自有程式碼採 **MIT License**，見 [LICENSE](LICENSE)。

### 致謝 / Acknowledgements

本專案使用下列開源元件，完整聲明見 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)：

- **[JoyStick](https://github.com/bobboteck/JoyStick)** by Roberto D'Amico (Bobboteck) — MIT License
  （觸控搖桿 UI，`data/joy.js` / `data/joy.css`）
- **[Blockly](https://github.com/google/blockly)** by Google — Apache License 2.0
  （積木程式引擎，`data/blockly.min.js.gz`）
