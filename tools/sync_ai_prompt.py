"""Regenerate the browser prompt from ai-relay/prompts.py; no API calls."""
import ast
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sync():
    tree = ast.parse((ROOT / "ai-relay/prompts.py").read_text(encoding="utf-8"))
    prompt = next(ast.literal_eval(n.value) for n in tree.body
                  if isinstance(n, ast.Assign) and n.targets[0].id == "SYSTEM_PROMPT")
    path = ROOT / "data/ai.html"
    text = path.read_text(encoding="utf-8")
    start = text.index("    const SYSTEM_PROMPT =")
    end = text.index("\n\n    // ---------- 狀態", start)
    text = text[:start] + "    const SYSTEM_PROMPT = " + json.dumps(prompt, ensure_ascii=False) + ";" + text[end:]
    path.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    sync()
