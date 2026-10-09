"""Settings and secrets.

Settings live in ~/Library/Application Support/vizbrain/settings.json. The
Anthropic API key is never stored there: it comes from the ANTHROPIC_API_KEY
environment variable or the macOS Keychain (service "vizbrain", account
"anthropic").
"""

from __future__ import annotations

import json
import os
import subprocess
import threading
from pathlib import Path

SETTINGS_DIR = Path.home() / "Library" / "Application Support" / "vizbrain"
SETTINGS_PATH = SETTINGS_DIR / "settings.json"

DEFAULTS: dict = {
    "port": 4050,
    # Bot address. Empty = discover the first Stackchan vizBot over mDNS, and
    # adopt whichever bot calls /v1/voice.
    "bot_host": "",
    "model": "claude-sonnet-5-5",
    "event_model": "claude-haiku-4-5",
    "effort": "low",
    "history_turns": 10,
    "session_idle_s": 300,
    "stt_model": "mlx-community/whisper-small.en-mlx",
    "tts_voice": "",                 # "" = default (Kokoro am_puck); e.g. "kokoro:af_heart@1.0" or a macOS voice
    "tts_engine": "kokoro",          # "kokoro" (local neural) or "say" (macOS voices)
    "tts_rate": 190,
    # WLED devices the brain may control, by name. null = every device found.
    "wled_allow": None,
    "vizmac_url": "http://127.0.0.1:4049",
    # Workspace for a personal API key that isn't scoped to one workspace.
    "workspace_id": "",
    # Claude's built-in web search (about 1 cent per search, plus the result tokens).
    "web_search": True,
    "web_search_max_uses": 2,
    # "Hey vizBot": score (0-1) the wake model must reach to start a turn.
    "wake_enabled": True,
    "wake_threshold": 0.5,
    "location": {"type": "approximate", "city": "Richmond", "region": "Virginia",
                 "country": "US", "timezone": "America/New_York"},
}

_lock = threading.Lock()


def load() -> dict:
    data = dict(DEFAULTS)
    try:
        data.update(json.loads(SETTINGS_PATH.read_text()))
    except FileNotFoundError:
        pass
    except (OSError, json.JSONDecodeError) as e:
        print(f"[config] ignoring unreadable settings: {e}")
    return data


def save(data: dict) -> None:
    with _lock:
        SETTINGS_DIR.mkdir(parents=True, exist_ok=True)
        keep = {k: v for k, v in data.items() if k in DEFAULTS and v != DEFAULTS[k]}
        tmp = SETTINGS_PATH.with_suffix(".tmp")
        tmp.write_text(json.dumps(keep, indent=2))
        tmp.replace(SETTINGS_PATH)


def api_key() -> str | None:
    key = os.environ.get("ANTHROPIC_API_KEY")
    if key:
        return key.strip()
    try:
        out = subprocess.run(
            ["security", "find-generic-password", "-s", "vizbrain", "-a", "anthropic", "-w"],
            capture_output=True, text=True, timeout=5,
        )
        if out.returncode == 0 and out.stdout.strip():
            return out.stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        pass
    return None
