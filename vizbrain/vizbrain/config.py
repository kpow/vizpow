"""Settings and secrets.

Settings live in ~/Library/Application Support/vizbrain/settings.json. The
Anthropic API key is never stored there: it comes from the ANTHROPIC_API_KEY
environment variable or the macOS Keychain (service "vizbrain", account
"anthropic").
"""

from __future__ import annotations

import json
import os
import secrets
import subprocess
import threading
from pathlib import Path

SETTINGS_DIR = Path.home() / "Library" / "Application Support" / "vizbrain"
SETTINGS_PATH = SETTINGS_DIR / "settings.json"
PAIRING_PATH = SETTINGS_DIR / "pairing.json"

DEFAULTS: dict = {
    "port": 4050,
    # What vizBot calls the person he lives with. "" = the first name on this Mac's account.
    "user_name": "",
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
    # Spend caps in dollars, from the running total in usage.json. Past either
    # one he says so instead of calling Claude; events and routines go quiet.
    # 0 = no cap.
    "daily_cap_usd": 2.0,
    "monthly_cap_usd": 20.0,
    # Write what was heard and said into the log file, and every wake-word
    # burst. Off by default: the log then holds counts and timings only.
    "debug_log": False,
    # "Hey vizBot": score (0-1) the wake model must reach to start a turn.
    "wake_enabled": True,
    "wake_threshold": 0.5,
    # Where he is: {"city", "region", "country", "timezone", "lat", "lon"}, any
    # of them. The timezone defaults to this Mac's; lat/lon are for the weather
    # (without them he uses the location saved on the bot).
    "location": {},
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


def user_name(settings: dict) -> str:
    name = (settings.get("user_name") or "").strip()
    if not name:
        try:
            full = subprocess.run(["id", "-F"], capture_output=True, text=True, timeout=3).stdout
        except (OSError, subprocess.TimeoutExpired):
            full = ""
        name = (full.split() or ["friend"])[0]
    return name


def location(settings: dict) -> dict:
    """The location setting, with this Mac's timezone filled in when unset."""
    loc = dict(settings.get("location") or {})
    if not loc.get("timezone"):
        try:   # /etc/localtime -> /var/db/timezone/zoneinfo/America/New_York
            loc["timezone"] = os.path.realpath("/etc/localtime").split("zoneinfo/", 1)[1]
        except IndexError:
            pass
    return loc


def pairing() -> dict:
    """{"token": "<id>.<secret>", "paired": bool}, created on first use.

    The token is the secret this brain shares with its bot. vizbrain hands it
    to the bot it drives; the bot sends it back on every call. "paired" turns
    true once a bot holds it, and from then on the bot's routes refuse callers
    without it. The id half is public (the bot reports it in /brain/status).
    Delete the file to pair again from scratch.
    """
    try:
        data = json.loads(PAIRING_PATH.read_text())
        if isinstance(data.get("token"), str) and "." in data["token"]:
            return {"token": data["token"], "paired": bool(data.get("paired"))}
    except (OSError, json.JSONDecodeError, AttributeError):
        pass
    data = {"token": f"{secrets.token_hex(4)}.{secrets.token_hex(16)}", "paired": False}
    save_pairing(data)
    return data


def save_pairing(data: dict) -> None:
    with _lock:
        SETTINGS_DIR.mkdir(parents=True, exist_ok=True)
        tmp = PAIRING_PATH.with_suffix(".tmp")
        tmp.touch(mode=0o600)
        tmp.write_text(json.dumps(data, indent=2))
        tmp.replace(PAIRING_PATH)


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
