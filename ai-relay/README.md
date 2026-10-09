# ai-relay

phone_blocky 的「自然語言 → PROG JSON」中繼服務。

```
ai.html (ESP32) ──fetch──> ai-relay ──Azure OpenAI──> GPT-4o
                      <───validated PROG JSON───┘
```

**目前連線方式**：`data/ai.html` 直接呼叫使用者設定的 Azure endpoint，未接本 relay；relay 是可獨立呼叫的選用服務。兩端共用相同提示詞與 AI 子集契約。若要讓瀏覽器改走 relay，需另做連線設定整合。

**為什麼要 relay**:Azure OpenAI 的 API key 不能放在 ESP32 的 `ai.html` 裡 (任何打開頁面的人都看得到)。relay 可把 key 留在服務端；目前 ai.html 的直接模式則由使用者在瀏覽器輸入 key。

## 安裝

```bash
cd ai-relay
python -m venv .venv
.venv\Scripts\activate          # PowerShell: .\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
copy .env.example .env
# 編輯 .env 填入你的 Azure 資訊
```

## 啟動

```bash
uvicorn main:app --host 0.0.0.0 --port 8000 --reload
```

- 監聽 `0.0.0.0:8000`,ESP32 在同網段就能打到 (例如 `http://192.168.x.x:8000/generate`)
- `--reload` 讓你改 Python 自動重啟,正式使用拿掉

## API

### `GET /health`
回傳 relay 是否正常、Azure 是否設定完成。

```json
{ "ok": true, "azure_configured": true, "deployment": "gpt-4o" }
```

### `POST /generate`
輸入自然語言,回傳驗證過的 PROG JSON。

Request:
```json
{ "prompt": "讓馬達1前進2秒後停止", "temperature": 0.2 }
```

Response (成功):
```json
{
  "ok": true,
  "prog": {
    "mode": "PROG",
    "loop": [],
    "setup": [
      {"cmd": "pwm", "motor": 1, "duty": 60},
      {"cmd": "delay", "ms": 2000},
      {"cmd": "stop", "motor": 1}
    ]
  },
  "raw": "...AI 原始輸出..."
}
```

Response (AI 輸出不合法):
```json
{
  "ok": false,
  "error": "PROG schema 驗證失敗: ...",
  "raw": "...AI 原始輸出..."
}
```

### `POST /validate`
單純跑 schema 驗證，不呼叫 Azure。目前 ai.html 手動編輯後使用本地 JS 驗證器；外部客戶端可呼叫此 API。

Request body 直接是要驗證的 PROG 物件。

## Schema

詳見 [../docs/prog_json_schema.md](../docs/prog_json_schema.md)。

支援的指令:
- `pwm`  — `motor` 1-4, `duty` -100..100
- `stop` — `motor` 0-4 (0=全部)
- `servo`— `ch` 1-2, `deg` 0-180
- `delay`— `ms` 0-10000
- `move_to` / `move_by` — M3/M4，deg 為度×100
- `zero` / `speed` — M3/M4，rpm 為 0 或 60..250
- `if` + `logic_compare`，讀值支援超音波與 Lego 按鈕（原始電位）
- 不支援 while/until/repeat；指令數含巢狀分支，預設上限 64

驗證採用 [schema.py](schema.py) 的 Pydantic 模型,relay 強制 AI 輸出符合此格式。

## 不測硬體怎麼驗

```bash
# 1. 確認 relay 起來
curl http://localhost:8000/health

# 2. 試打一個 prompt
curl -X POST http://localhost:8000/generate ^
  -H "Content-Type: application/json" ^
  -d "{\"prompt\":\"讓馬達3順時針轉一秒\"}"

# 3. 看回傳是不是合法 PROG JSON
```

## 安全提示

- `.env` 已加入 `.gitignore`,不要 commit
- 目前 CORS 全開 (`allow_origins=["*"]`),只適合本機/區網。要上公網請改成 ESP32 的實際來源
- relay 本身不做認證,假設你只在內網跑

## 離線契約檢查

在專案根目錄執行 `python -m unittest discover -s tests -v` 與 `node tests/check_frontend.cjs`（Python 環境需安裝 requirements.txt）。更新 prompts.py 後執行 `python tools/sync_ai_prompt.py` 同步 ai.html。測試不呼叫 Azure 或 ESP32。
