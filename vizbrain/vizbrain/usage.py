"""Running Claude spend, persisted per day in Application Support/vizbrain/usage.json.

Prices are list prices per million tokens (platform.claude.com pricing,
October 2026). Cache writes use the 5-minute rate.
"""

from __future__ import annotations

import datetime as dt
import json
import threading

from .config import SETTINGS_DIR

PRICES = {  # model: (input, output, cache_read, cache_write_5m) $/MTok
    "claude-sonnet-5-5": (2.00, 10.00, 0.20, 2.50),
    "claude-haiku-4-5": (1.00, 5.00, 0.10, 1.25),
    "claude-opus-5-5": (4.00, 20.00, 0.20, 5.00),
}

PATH = SETTINGS_DIR / "usage.json"
_lock = threading.Lock()


def _load() -> dict:
    try:
        return json.loads(PATH.read_text())
    except (OSError, json.JSONDecodeError):
        return {}


def record(model: str, usage) -> float:
    """Add one response's usage; returns its cost in dollars."""
    p_in, p_out, p_cr, p_cw = PRICES.get(model, PRICES["claude-sonnet-5-5"])
    inp = getattr(usage, "input_tokens", 0) or 0
    out = getattr(usage, "output_tokens", 0) or 0
    cr = getattr(usage, "cache_read_input_tokens", 0) or 0
    cw = getattr(usage, "cache_creation_input_tokens", 0) or 0
    cost = (inp * p_in + out * p_out + cr * p_cr + cw * p_cw) / 1_000_000
    day = dt.date.today().isoformat()
    with _lock:
        data = _load()
        d = data.setdefault(day, {"usd": 0.0, "calls": 0, "input": 0, "output": 0, "cache_read": 0})
        d["usd"] += cost
        d["calls"] += 1
        d["input"] += inp + cw
        d["output"] += out
        d["cache_read"] += cr
        SETTINGS_DIR.mkdir(parents=True, exist_ok=True)
        PATH.write_text(json.dumps(data, indent=1))
    return cost


def summary() -> dict:
    data = _load()
    today = dt.date.today()
    month = today.strftime("%Y-%m")
    t = data.get(today.isoformat(), {})
    m = sum(v["usd"] for k, v in data.items() if k.startswith(month))
    return {"today_usd": round(t.get("usd", 0.0), 4), "today_calls": t.get("calls", 0),
            "month_usd": round(m, 4)}
