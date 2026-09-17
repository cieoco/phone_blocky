# motor_tuner — phone_blocky 單馬達調校工具

把 `motorControl/python_gui/motor_control_v4` 的「單馬達測試 + PID 調教 + 輸出曲線圖」
機制移植到 phone_blocky，走 phone_blocky 的 **`cmd:` JSON 協定**，支援 **WiFi(WS) 與 USB(序列)** 兩種傳輸。

> **通訊層解耦後**：phone_blocky 韌體已把「傳輸 / 協定 / 回應出口」三層解耦
> （見 `src/CommChannel.h`），`cmd:` JSON 協定現在 **WebSocket 與 USB 序列雙通道共用
> 同一個 router**，回應/telemetry 會走回指令來源通道。故本工具兩種傳輸功能完全等價。
>
> - **WiFi(WS)**：`ws://<ip>/ws`（埠 80）
> - **USB(序列)**：115200 baud，送出 `{json}\n`、逐行收 JSON（韌體 debug log 自動略過）

## 功能

| 功能 | 說明 |
|---|---|
| 單馬達測試 | `pwm`（全部馬達，開迴路 ±100）/ `speed`（閉迴路 RPM）/ `move_to`、`move_by`、`zero`（位置） |
| PID 調教 | 速度環（kp/ki/kd、min/maxSpeed）與位置環（posKp/posKi/posKd、posMaxDuty、posToleranceDeg）的完整生命週期：**讀取 / 套用RAM / 寫入NVS / 還原預設**（見下） |
| 自動估 PID | 開迴路掃描多個 PWM 準位，用韌體 **50Hz 高速擷取緩存**（`chart_buffer_*`）量上升段，自動擬合前饋 kS/kV 與速度環 Kp/Ki；前饋自動套 RAM、PID 填欄位待確認。角度環可依速度環 τ 串級推算 |
| 輸出曲線圖 | `subscribe` 開 10Hz telemetry，記錄期累積 → 停止後延遲 2 秒 → 繪實際(實線) vs 目標(虛線) |
| 燒錄韌體 | 內建「燒錄」分頁，用 esptool 寫入四個分區 bin（Offset 預設帶入）；詳見 [安裝教程](../docs/安裝教程.md) |

### 參數生命週期（read / apply-RAM / write-NVS / reset）

韌體已把這套生命週期接進 `cmd:` 協定（WS+USB 共用），對應四個操作：

| 介面按鈕 | cmd | 行為 |
|---|---|---|
| 讀取 | `read_config` | 回 `type:"config"`，介面回填韌體目前值（連線後自動執行一次） |
| 套用RAM | `apply_config` | 套用到 RAM，不寫 NVS（重開機會丟失） |
| 寫入NVS | `write_config` | 套用 RAM 並寫入 NVS；**寫入有檢查**，失敗回 `err:"nvs_write_failed"` 並在介面標示 |
| 還原預設 | `reset_config` | 還原 `config.h` 預設值到 RAM（不動 NVS），回送 config 回填介面 |

**只有 M3、M4 有編碼器** → 轉速/位置閉迴路與曲線只對這兩顆有意義；M1/M2 僅 PWM。
連線後工具會自動以 `capabilities` 查詢，無編碼器的馬達會停用閉迴路控制。

## 分頁

UI 分五個分頁（連線列與 log 共用）：

- **馬達調校**：單馬達測試 + 速度/位置環 PID + **自動估 PID** + 輸出曲線圖。
- **設定**（移植自 `data/set.html`，走 cmd: 協定 → WS+USB 皆可）：**左右兩欄**，對應兩份獨立 NVS。
  - **左欄＝馬達參數**（NVS `motor`，即時生效、不重啟）：
    - **馬達種類 + 硬體參數**：樂高/霍爾切換（→ encoderPos / posCtrlMode）、編碼器 PPR、減速比。
    - **角度保持 Hold**：死區 settle / 創爬 KI / 夾持上限。
    - 與 PID 共用同一份 NVS，故共用「讀取/套用RAM/寫入NVS/還原預設」。
    - **參數檔（換板移植）**：匯出參數 / 匯入參數 / 匯入並寫入NVS（見下〔換板複製參數〕）。
  - **右欄＝控制板設定**（NVS `wifi`，送出後**自動重啟**、連線會中斷）：
    - **WiFi / 控制板名稱**：SSID / 密碼 / AP 名稱 / 管理密碼 → `set_wifi`、`set_name`（需正確管理密碼）。
- **感測器**（參考 `data/hardware_test.html`，純即時測試、不做報表）：數位按鈕 / 數位輸入 / 類比(ADC) /
  超音波(Trig+Echo) 各一列，可設腳位、開始/停止輪詢（約 4Hz，`mode:"hardwareTest"`）、即時顯示讀值。
- **接線**（移植自 `data/wiring.html`，純顯示）：板子圖 + 舵機/編碼器/使用者腳位/樂高 RJ12 接頭/禁用腳位的腳位參考表。
  圖檔以搜尋路徑指向 repo 的 `data/wemos.jpg`（不複製二進位進工具）。
- **燒錄**：用 esptool 把四個分區 bin 寫入 ESP32（直接使用最上方 USB 連線列選擇的 COM 埠、Baud 預設 921600、Offset 預設帶入、可選「燒錄前清除 flash」）。
  燒錄前需先「斷線」釋放 USB 序列埠。完整步驟見 [安裝教程](../docs/安裝教程.md)。

## 限制

- 閉迴路（轉速/位置）與曲線只對 **M3/M4**（有編碼器）有意義；M1/M2 僅 PWM。
- `read_config` / `write_config` 操作的是全域 PID 參數（非分馬達）；NVS 以 `motor`
  命名空間整批存取（與 Dashboard、開機 `loadPIDSettings` 共用同一份）。

## 執行

### 方法一：雙擊 run.bat（建議）

首次執行會自動建立 `.venv` 並安裝相依，之後直接啟動。

### 方法二：手動

```powershell
cd c:\project\robot\phone_blocky\motor_tuner
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe main.py
```

相依：`PyQt6`（含 QtWebSockets）、`matplotlib`、`pyserial`。

## 打包成獨立 EXE

執行：

```powershell
cd c:\project\robot\phone_blocky\motor_tuner
.\build_exe.bat
```

腳本會自動安裝 Python 相依、用 PlatformIO 編譯韌體與 LittleFS 映像，接著打包 EXE。

完成後會產生：

```text
motor_tuner\dist\PhoneBlockyMotorTuner.exe
```

EXE 會內建 `.pio\build\esp32dev\` 中的 `bootloader.bin`、`partitions.bin`、
`firmware.bin`、`littlefs.bin`，因此可直接帶到其他 Windows 電腦執行與燒錄。
若要更新內建韌體，請重新執行 PlatformIO 編譯與 `build_exe.bat`。

## 使用流程

1. 選傳輸：**WiFi(WS)** 輸入 ESP32 IP（預設 AP 模式 `192.168.4.1`）；或 **USB(序列)** 選 COM 埠。按「連線」。
2. 選擇馬達（M1–M4）。
3. 送 PWM / 轉速 / 位置指令測試；勾「顯示圖表」即時記錄。
4. 調 PID → 套用 → 再送轉速指令，從曲線比較實際 vs 目標收斂情形。
5. 按「停止」後圖表會再記錄 2 秒，完整捕捉減速段才繪製。

### 自動估 PID（M3／M4）

1. 連線後選 M3 或 M4，到「自動估速度 PID」區設「高準位 PWM(%)」與「響應」，按「開始自動估」。
2. 工具開迴路掃描 → 韌體高速緩存回傳 → 自動算出前饋 kS/kV 與 Kp/Ki：**前饋自動套 RAM，PID 填入欄位**。
3. 按「套用RAM」讓 PID 生效 → 送轉速驗證曲線 → 滿意後「寫入NVS」保存。
4. 角度環可在速度自動估完成後，按「自動估角度 PID」依內環 τ 串級推算。

### 換板複製參數（教具換新控制器用）

「設定」分頁左欄提供參數檔匯出／匯入，把一塊板調好的馬達參數（PID、前饋、硬體種類、
角度保持，共 21 項）複製到新板。**只含馬達參數（NVS `motor`），不含 WiFi 與板名（NVS `wifi`）**，
所以複製參數不會讓新板被改名。

1. **匯出**：連線已調好的板 → 連線後自動 `read_config` 回填欄位 → 按「匯出參數」存成
   `motor_params_<日期時間>.json`（建議當標準範本保存）。
2. **匯入並寫入NVS**：連線新板 → 按「匯入並寫入NVS」選該檔 → 工具 `_fill_config` 後送一次
   `write_config`（韌體端 `applyConfigFromDoc` + 存 NVS，**不重啟**）。
3. 想先核對再寫，改按「匯入參數」只填欄位、不動板子，再手動「套用RAM／寫入NVS」。

> 建議順序：先在右欄改名／設 WiFi（會重啟）→ 重連 → 再匯入參數（不重啟）。
> 檔案格式容錯：接受 `{config:{...}}` 或直接的參數物件；缺鍵保留現值、多餘鍵忽略。
> 完整圖文見 [安裝教程 — 換板：複製調校參數](../docs/安裝教程.md#換板複製調校參數到新控制器)。

## 檔案

```
motor_tuner/
├── main.py           # PyQt6 主視窗（傳輸切換、單馬達、PID、遙測分派）
├── ws_worker.py      # QWebSocket 客戶端（WiFi 傳輸）
├── serial_worker.py  # pyserial 客戶端（USB 傳輸，訊號介面與 ws_worker 一致）
├── chart_widget.py   # RPM/位置曲線（自 motor_control_v4 精簡移植）
├── requirements.txt
├── run.bat
└── README.md
```
