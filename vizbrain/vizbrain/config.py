"""Settings and secrets.

Settings live in ~/Library/Application Support/vizbrain/settings.json. The
Anthropic API key is never stored there: it comes from the ANTHROPIC_API_KEY
environment variable or the macOS Keychain (service "vizbrain", account
"anthropic").

Connectors (MCP servers whose tools vizBot can use) are listed in their own
file, connectors.json, next to settings.json. It is a separate file because
save() keeps only the keys in DEFAULTS. Its shape is the one Claude Desktop
and Claude Code use:

    {"mcpServers": {"kpow": {"command": "kpow-mcp", "args": [], "timeout_s": 8}}}

Optional per connector: "enabled", "tools" (an allowlist), "actions" (tools
that change things, so a spoken confirmation is enough), "replaces" (built-in
tools it supersedes, e.g. a Spotify connector replacing music_play), "env",
"timeout_s", "start_timeout_s", "max_result_chars".
"""

from __future__ import annotations

import json
import os
import re
import secrets
import subprocess
import sys
import threading
from pathlib import Path

SETTINGS_DIR = Path.home() / "Library" / "Application Support" / "vizbrain"
SETTINGS_PATH = SETTINGS_DIR / "settings.json"
PAIRING_PATH = SETTINGS_DIR / "pairing.json"
CONNECTORS_PATH = SETTINGS_DIR / "connectors.json"

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
    # Extra voices for the picker on the bot's page: [{"id", "label", "group", "spec"}]
    # with a spec like "kokoro:bm_george*0.6+bm_lewis*0.4@0.9" (see voices.py).
    "custom_voices": [],
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
    return keychain_get("anthropic")


def keychain_get(account: str) -> str | None:
    """A secret from the macOS Keychain, service "vizbrain"."""
    try:
        out = subprocess.run(
            ["security", "find-generic-password", "-s", "vizbrain", "-a", account, "-w"],
            capture_output=True, text=True, timeout=5,
        )
        if out.returncode == 0 and out.stdout.strip():
            return out.stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        pass
    return None


# ---- Connectors -----------------------------------------------------------

# Lowercase, short, and no underscores: tools are offered to Claude as
# "<connector>__<tool>", and the double underscore must stay unambiguous.
CONNECTOR_NAME = re.compile(r"^[a-z][a-z0-9-]{0,15}$")
CONNECTOR_DEFAULTS = {"timeout_s": 8.0, "start_timeout_s": 30.0, "max_result_chars": 6000}
_CONNECTOR_LIMITS = {"timeout_s": (1.0, 20.0), "start_timeout_s": (5.0, 120.0), "max_result_chars": (500, 20000)}


def connectors_raw() -> dict:
    """connectors.json as written, for editing it (an empty one if there's no file)."""
    try:
        raw = json.loads(CONNECTORS_PATH.read_text())
    except FileNotFoundError:
        return {"mcpServers": {}}
    if not isinstance(raw, dict) or not isinstance(raw.get("mcpServers"), dict):
        raise ValueError('connectors.json needs an "mcpServers" object')
    return raw


def save_connectors(raw: dict) -> None:
    """Write connectors.json, readable only by this user: it decides what code runs."""
    with _lock:
        SETTINGS_DIR.mkdir(parents=True, exist_ok=True)
        tmp = CONNECTORS_PATH.with_suffix(".tmp")
        tmp.touch(mode=0o600)
        tmp.write_text(json.dumps(raw, indent=2) + "\n")
        tmp.replace(CONNECTORS_PATH)


def connector_secret_ids() -> list[str]:
    """The Keychain ids that connectors.json refers to ("keychain:<id>" env values)."""
    try:
        servers = connectors_raw()["mcpServers"]
    except (OSError, ValueError):
        return []
    return sorted({v.removeprefix("keychain:") for e in servers.values() if isinstance(e, dict)
                   for v in (e.get("env") or {}).values() if isinstance(v, str) and v.startswith("keychain:")})


def connectors() -> tuple[dict[str, dict], list[str]]:
    """The enabled connectors in connectors.json, checked and filled in with
    defaults, plus a line for each problem found. A missing file is no
    connectors and no problems; a broken entry is skipped, never fatal."""
    try:
        raw = json.loads(CONNECTORS_PATH.read_text())
    except FileNotFoundError:
        return {}, []
    except (OSError, json.JSONDecodeError) as e:
        return {}, [f"connectors.json is unreadable: {e}"]
    servers = raw.get("mcpServers") if isinstance(raw, dict) else None
    if not isinstance(servers, dict):
        return {}, ['connectors.json needs an "mcpServers" object']
    out: dict[str, dict] = {}
    problems: list[str] = []
    for name, entry in servers.items():
        if not CONNECTOR_NAME.match(name):
            problems.append(f"connector {name!r}: names are lowercase letters, digits and dashes, up to 16")
            continue
        if not isinstance(entry, dict):
            problems.append(f"connector {name}: should be an object")
            continue
        if entry.get("enabled") is False or entry.get("disabled") is True:
            continue
        try:
            out[name] = _connector(name, entry)
        except ValueError as e:
            problems.append(f"connector {name}: {e}")
    return out, problems


def _connector(name: str, e: dict) -> dict:
    command, url = e.get("command"), e.get("url")
    if bool(command) == bool(url):
        raise ValueError('needs a "command" (or, for an HTTP server, a "url"), not both')
    if command is not None and not isinstance(command, str):
        raise ValueError('"command" should be a string')
    if url is not None and not (isinstance(url, str) and re.match(r"^https?://", url)):
        raise ValueError('"url" should start with http:// or https://')
    args = e.get("args", [])
    if not (isinstance(args, list) and all(isinstance(a, str) for a in args)):
        raise ValueError('"args" should be a list of strings')
    env = e.get("env", {})
    if not (isinstance(env, dict) and all(isinstance(k, str) and isinstance(v, str) for k, v in env.items())):
        raise ValueError('"env" should map names to strings')
    lists = {}
    for key in ("tools", "actions", "replaces"):
        v = e.get(key)
        if v is not None and not (isinstance(v, list) and all(isinstance(x, str) for x in v)):
            raise ValueError(f'"{key}" should be a list of tool names')
        lists[key] = v
    conn = {"name": name, "command": command, "url": url, "args": args, "env": env,
            "cwd": e.get("cwd") if isinstance(e.get("cwd"), str) else None,
            "tools": lists["tools"],              # None = every tool the server offers
            "actions": lists["actions"] or [],    # tools that change things rather than look them up
            "replaces": lists["replaces"] or []}  # built-in tools to hide while this connector offers its own
    for key, default in CONNECTOR_DEFAULTS.items():
        lo, hi = _CONNECTOR_LIMITS[key]
        try:
            v = float(e.get(key, default))
        except (TypeError, ValueError):
            raise ValueError(f'"{key}" should be a number') from None
        v = min(max(v, lo), hi)
        conn[key] = int(v) if isinstance(default, int) else v
    return conn


# launchd starts vizbrain with PATH=/usr/bin:/bin:/usr/sbin:/sbin, where uv,
# npx, Homebrew tools and `uv tool install`ed servers are all missing.
def connector_path() -> str:
    dirs = [str(Path(sys.executable).parent), str(Path.home() / ".local" / "bin"),
            "/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/bin", "/usr/sbin", "/sbin"]
    return os.pathsep.join(dict.fromkeys(dirs))


def connector_env(conn: dict) -> tuple[dict[str, str], list[str]]:
    """The extra environment a connector starts with, plus problems. PATH is
    widened (unless the connector sets its own) and "keychain:<id>" values are
    read from the Keychain account "connector/<id>"."""
    env = {"PATH": connector_path()}
    problems: list[str] = []
    for key, value in conn["env"].items():
        if value.startswith("keychain:"):
            secret = keychain_get(f"connector/{value.removeprefix('keychain:')}")
            if secret is None:
                problems.append(f"connector {conn['name']}: no Keychain item for {key} "
                                f"(run vizbrain connectors set-secret {value.removeprefix('keychain:')})")
                continue
            value = secret
        env[key] = value
    return env, problems
