"""ai-relay — phone_blocky 的自然語言 → PROG JSON 中繼服務.

架構:
    ai.html (ESP32) ──fetch──> 這裡 ──Azure OpenAI──> GPT-4o
                          <───validated PROG JSON───┘

啟動:
    uvicorn main:app --host 0.0.0.0 --port 8000 --reload
"""
from __future__ import annotations

import json
import os
from typing import Optional

from dotenv import load_dotenv
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from openai import AzureOpenAI
from pydantic import BaseModel, Field, ValidationError

from prompts import SYSTEM_PROMPT
from schema import ProgProgram

load_dotenv()

AZURE_ENDPOINT = os.getenv("AZURE_OPENAI_ENDPOINT", "").rstrip("/")
AZURE_API_KEY = os.getenv("AZURE_OPENAI_API_KEY", "")
AZURE_DEPLOYMENT = os.getenv("AZURE_OPENAI_DEPLOYMENT", "")
AZURE_API_VERSION = os.getenv("AZURE_OPENAI_API_VERSION", "2024-08-01-preview")
MAX_COMMANDS = int(os.getenv("MAX_COMMANDS", "64"))

app = FastAPI(title="phone_blocky ai-relay", version="1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["GET", "POST", "OPTIONS"],
    allow_headers=["*"],
)


def get_client() -> AzureOpenAI:
    if not (AZURE_ENDPOINT and AZURE_API_KEY and AZURE_DEPLOYMENT):
        raise HTTPException(
            status_code=500,
            detail="Azure OpenAI 未設定。請在 .env 設定 AZURE_OPENAI_ENDPOINT / API_KEY / DEPLOYMENT",
        )
    return AzureOpenAI(
        api_key=AZURE_API_KEY,
        api_version=AZURE_API_VERSION,
        azure_endpoint=AZURE_ENDPOINT,
    )


class GenerateRequest(BaseModel):
    prompt: str = Field(..., min_length=1, max_length=2000)
    temperature: float = Field(0.2, ge=0.0, le=1.0)


class GenerateResponse(BaseModel):
    ok: bool
    prog: Optional[dict] = None
    error: Optional[str] = None
    raw: Optional[str] = None


@app.get("/health")
def health():
    return {
        "ok": True,
        "azure_configured": bool(AZURE_ENDPOINT and AZURE_API_KEY and AZURE_DEPLOYMENT),
        "deployment": AZURE_DEPLOYMENT or None,
    }


@app.post("/validate", response_model=GenerateResponse)
def validate(prog: dict):
    """讓 ai.html 在使用者手動編輯後也能再驗證一次。"""
    try:
        validated = ProgProgram.model_validate(prog)
    except ValidationError as e:
        return GenerateResponse(ok=False, error=str(e), raw=json.dumps(prog))
    if validated.total_commands() > MAX_COMMANDS:
        return GenerateResponse(
            ok=False,
            error=f"指令總數 {validated.total_commands()} 超過上限 {MAX_COMMANDS}",
            raw=json.dumps(prog),
        )
    return GenerateResponse(ok=True, prog=validated.model_dump(by_alias=True))


@app.post("/generate", response_model=GenerateResponse)
def generate(req: GenerateRequest):
    client = get_client()

    try:
        completion = client.chat.completions.create(
            model=AZURE_DEPLOYMENT,
            temperature=req.temperature,
            response_format={"type": "json_object"},
            messages=[
                {"role": "system", "content": SYSTEM_PROMPT},
                {"role": "user", "content": req.prompt},
            ],
        )
    except Exception as e:
        raise HTTPException(status_code=502, detail=f"Azure OpenAI 呼叫失敗: {e}")

    raw = completion.choices[0].message.content or ""

    try:
        parsed = json.loads(raw)
    except json.JSONDecodeError as e:
        return GenerateResponse(ok=False, error=f"AI 輸出非合法 JSON: {e}", raw=raw)

    try:
        validated = ProgProgram.model_validate(parsed)
    except ValidationError as e:
        return GenerateResponse(ok=False, error=f"PROG schema 驗證失敗: {e}", raw=raw)

    if validated.total_commands() > MAX_COMMANDS:
        return GenerateResponse(
            ok=False,
            error=f"指令總數 {validated.total_commands()} 超過上限 {MAX_COMMANDS}",
            raw=raw,
        )

    return GenerateResponse(ok=True, prog=validated.model_dump(by_alias=True), raw=raw)
