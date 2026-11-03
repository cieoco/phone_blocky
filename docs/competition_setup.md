# ESP-NOW 相撲比賽：建置與操作

目前已完成軟體實作與離線驗證，尚未完成兩塊以上實體板的無線與機構驗收。裁判板與學生競賽板均以本專案 ESP32／4 MB Flash 配置建置；請先確認實際板型。

## 1. 準備控制板

一塊裁判 ESP32，另加每台車一塊學生競賽板。第一版一場可選 1–4 台車（單台供測試），名冊最多 64 隊。所有車使用相同場地頻道，預設 1，可由 USB 設為 1–11。

| 韌體角色 | 行為 | 網頁資料來源 |
|---|---|---|
| `esp32dev_referee` | 裁判計時、名單、ESP-NOW 授權，不初始化致動器 | `referee_data/` |
| `esp32dev_competitor` | 學生車；開機鎖定，手動與自動都需要授權 | `data/` |
| 原有 `esp32dev`／`esp32dev_i2c` | 保持既有教學／I2C 模式，不能當成已受裁判限制的競賽車 | 原有 `data/` |

先只編譯與打包，不連板也可執行：

```powershell
pio run -e esp32dev_referee -e esp32dev_competitor
pio run -e esp32dev_referee -e esp32dev_competitor --target buildfs
```

確認板子、序列埠及馬達供電狀態後，分別燒錄。以下 COM5／COM6 是範例，需換成實際值：

```powershell
pio run -e esp32dev_referee --target upload --upload-port COM5
pio run -e esp32dev_referee --target uploadfs --upload-port COM5
pio run -e esp32dev_competitor --target upload --upload-port COM6
pio run -e esp32dev_competitor --target uploadfs --upload-port COM6
```

兩種角色的 `uploadfs` 不可互換。裁判的建置腳本會自動使用 `referee_data/`，包含四個原始 WAV；學生板保留 Blockly。這次未更動分割表。

## 2. USB 配對與隊伍登記

Python 環境需要 `pyserial`。關閉占用序列埠的監視器後執行：

```powershell
python tools/competition_pair.py --referee COM5 --student COM6 --name "A 隊" --channel 1 --show-credentials
```

工具會檢查角色、讀取 STA MAC、產生每車隨機金鑰、寫入兩板 NVS，並在裁判名冊登記隊名。同一 MAC 再配對會更新該隊，不建立重複記錄。金鑰不保存至本機檔案。完成後重新啟動兩塊板；換下一個學生板重複操作即可。

`--show-credentials` 會在本機終端顯示各板 Wi-Fi 名稱與 AP／網頁共用密碼，供操作者使用。不要把這段輸出貼進 repo。只要查看原有板子的連線資料：

```powershell
python tools/competition_pair.py --info COM5 --show-credentials
```

不加 `--show-credentials` 時不輸出密碼。若中途寫入失敗，重新執行整個配對命令，不要只更新其中一板。

配對後不連校園或家用路由器；裁判與學生板各自提供 AP，全部固定同一場地頻道。ESP-NOW 使用加密單播，只對本場名單建立 peers。

## 3. 學生準備與操作

手機連接 `Sumo-Car-…`，使用配對工具顯示的密碼，瀏覽 `http://192.168.4.1/`。輸入本車密碼後按「登入並取得操控權」。每車同時只有一個操控連線。

手動控制頁可指定左右輪為 M1–M4、反轉方向與調整動力；按住方向鍵行駛，放開即送零輸出。未授權、自動階段、過期控制 token、換段或背景分頁均不會延續先前按住的方向。頁面另提供「停止本車並回報裁判故障」。

自動程式：登入後開啟同一控制板的 Blockly，將程式「儲存到 ESP32」。競賽板不接受 Blockly 的直接執行，也不執行 autorun。裁判準備時驗證並鎖定存檔快照；比賽中不能更換程式。混合模式即使先手動，也需先存好自動程式。

自動階段被裁判暫停後，恢復會從 setup 重新執行，保留該階段剩餘時間。請勿假設能從原本 delay、迴圈或副程式的位置接續。

競賽角色只使用自己的 `/drive` 控制入口；舊 joy.html、USB M/S、USB JSON、hardwareTest、PID 寫入與 I2C 控制入口不啟用。已有的馬達 NVS 調校值會在開機讀入。需要調校時，先用教學角色完成；競賽角色不提供現場邊跑邊調參。

### 選用 GPIO 輸出

第一版預設不允許自動程式使用一般 GPIO 作致動輸出。需要時可在無活躍比賽時透過 USB JSON 登記安全電位為 LOW 的腳位：

```json
{"cmd":"competition_outputs","pins":[15,32]}
```

允許登記 4、15、18、26、32；完成後重開機。這些腳位會在開機／撤權時拉低，請勿把原本需要輸入或上拉的感測器腳位誤登記為輸出。超音波固定 TRIG 2／ECHO 33，不能任意改到馬達或舵機腳位。舵機撤權時取消脈波，不回 90°；實際機構在失去脈波後的行為必須另測。

## 4. 裁判操作

裁判電腦或手機連接 `Sumo-Ref-…`，瀏覽 `http://192.168.4.1/`，輸入裁判密碼並取得控制權。每個頁面有獨立操作者識別；其他頁只觀看。重新整理後需重新取得控制權，原控制權最多約 10 秒後可接手；比賽計時繼續。

1. 勾選本場隊伍。未勾選車不受本場操作影響。
2. 選手動、自動或混合；混合可選先手動或先自動。
3. 設定時間：手動／自動各預設 30 秒，中間暫停預設 5 秒。
4. 按「準備本場」，等待每車時鐘同步、程式驗證與 READY。
5. 在接喇叭的裁判頁按「啟用並試播號角」，或明確勾選靜音。
6. 按開始。所有參賽板先確認共同啟動排程，約 1.2 秒後進第一段。
7. 混合模式在第一段結束後全車停止，間隔倒數完自動切入第二段。
8. 正常結束或中止後，等待每車回覆「輸出已鎖定」，再按清場／下一場。

「裁判暫停」一定等待裁判按繼續；「階段間暫停」會自動繼續。恢復時另有約 1.2 秒準備窗口；若在 GAP 暫停，窗口結束後再消耗保存的 GAP 剩餘時間。

關閉裁判網頁不改變主控板計時，但該頁無法繼續播放音效。裁判板斷電、學生板重啟、換頻、租約逾時或同步失效都會使輸出鎖定，且不會因遲到封包自行恢復。

時間、模式與勾選名單目前屬本次開機的操作設定，重新開機回預設；隊伍、配對、頻道、密碼與 GPIO 設定存在 NVS。停止未收到回覆時畫面保持未確認，不能假報已停，也不能直接清場。

## 5. 音效與驗收

四個 WAV 取自 Team 254 Cheesy Arena 的固定 commit `248d9de497e701223e2cdbc986a19bbfea054c21`，未轉碼或合成。來源原始碼的事件映射與每檔 SHA-256 已核對；參考 [音效清單](../referee_data/audio/manifest.json) 與 [來源授權文字](../referee_data/audio/LICENSE.txt)。仍需在場地喇叭試聽，確認是預期版本與音量。

目前驗證包括原有 Python／Node 回歸、新增前端假環境測試、實際 C++ 核心的假時鐘／故障注入，以及四角色編譯與兩個檔案系統建置。以下是上板後才能完成的驗收：

- 兩台及四台車共同啟停差、無線遺失下的最壞停止時間。
- 裁判斷電、學生板重啟、未參賽車與鄰場控制器隔離。
- 馬達慣性、舵機停脈波、長 delay／while／超音波／OLED 下的停止。
- 真實手機、音效授權、喇叭輸出及至少 30 分鐘負載運行。

工程目標與完整案例見 [SDD](espnow_competition_sdd.md)。尚未量測的 30／50／400 ms 數字不是已證實能力。

## 6. 離線檢查

依 AGENTS.md 執行既有測試，另外執行：

```powershell
node tests/check_competition.cjs
wsl --exec bash /mnt/c/project/ROBOT/phone_blocky/tools/check_competition_core.sh
```

Linux／macOS 或 WSL 中也可在 repo 執行 `bash tools/check_competition_core.sh`，需 g++。此測試直接編譯韌體共用的 Core.h／Protocol.h，並啟用 undefined-behavior sanitizer。
