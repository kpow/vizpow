"""HTTP client for a vizBot, plus mDNS discovery of the Stackchan."""

from __future__ import annotations

import json
import re
import socket
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request

# Index order matches the firmware's BotExpressionIndex (vizbot/bot_faces.h).
EXPRESSIONS = [
    "neutral", "happy", "sad", "surprised", "chill", "angry", "love", "dizzy",
    "thinking", "excited", "mischief", "skeptical", "worried", "confused",
    "proud", "shy", "annoyed", "focused", "winking", "devious", "shocked",
    "kissing", "nervous", "glitching", "sassy",
]

GESTURES = ["nod", "shake", "lookup", "lookdown", "left", "right", "recenter"]

# Pitch limits in degrees (vizbot/config.h SC_SERVO_Y_MIN_DEG / MAX).
PITCH_MIN, PITCH_MAX, PITCH_HOME = 25, 85, 48
YAW_LIMIT = 60


class BotError(RuntimeError):
    pass


class Bot:
    def __init__(self, host: str):
        self.host = host

    def _get(self, path: str, params: dict | None = None, timeout: float = 3.0):
        url = f"http://{self.host}{path}"
        if params:
            url += "?" + urllib.parse.urlencode(params)
        try:
            with urllib.request.urlopen(url, timeout=timeout) as r:
                body = r.read().decode("utf-8", "replace")
        except (urllib.error.URLError, OSError) as e:
            raise BotError(f"{path}: {e}") from e
        try:
            return json.loads(body)
        except json.JSONDecodeError:
            return body

    def state(self) -> dict:
        s = self._get("/state", timeout=4.0)
        if not isinstance(s, dict):
            raise BotError("bad /state")
        return s

    def say(self, text: str, ms: int = 4000):
        return self._get("/bot/say", {"text": text[:95], "dur": max(1000, min(10000, ms))})

    def expression(self, name_or_index) -> int:
        idx = expression_index(name_or_index)
        self._get("/bot/expression", {"v": idx})
        return idx

    def head(self, yaw_deg: float | None, pitch_deg: float | None, ms: int = 600):
        params: dict = {"time": max(20, min(5000, int(ms)))}
        if yaw_deg is not None:
            params["yaw"] = int(max(-YAW_LIMIT, min(YAW_LIMIT, yaw_deg)) * 10)
        if pitch_deg is not None:
            params["pitch"] = int(max(PITCH_MIN, min(PITCH_MAX, pitch_deg)) * 10)
        return self._get("/bot/head/set_angles", params)

    def gesture(self, name: str):
        if name == "recenter":
            return self._get("/bot/head/recenter")
        if name not in GESTURES:
            raise BotError(f"unknown gesture {name}")
        return self._get("/bot/head/preset", {"name": name})

    def base_leds(self, r: int, g: int, b: int):
        return self._get("/bot/base_leds/set", {"r": r, "g": g, "b": b})

    def base_led_mode(self, name: str, brightness: int | None = None):
        params: dict = {"name": name}
        if brightness is not None:
            params["brightness"] = max(0, min(255, int(brightness)))
        return self._get("/bot/base_leds/mode", params)

    def personality(self) -> str:
        p = self._get("/bot/personality")
        try:
            return p["personalities"][p["current"]]["name"]
        except (KeyError, IndexError, TypeError):
            return "Chill"

    def play(self, audio_id: str, text: str, audio_ms: int, expression: int = -1):
        """Ask the bot to fetch a reply clip from us and speak it."""
        r = self._get("/brain/play", {
            "id": audio_id, "text": text[:95], "ms": audio_ms, "expr": expression,
        }, timeout=4.0)
        if not (isinstance(r, dict) and r.get("ok")):
            raise BotError("bot firmware has no /brain/play")
        return r


def expression_index(name_or_index) -> int:
    if isinstance(name_or_index, int):
        return max(0, min(len(EXPRESSIONS) - 1, name_or_index))
    key = str(name_or_index).strip().lower()
    if key.isdigit():
        return expression_index(int(key))
    if key in EXPRESSIONS:
        return EXPRESSIONS.index(key)
    raise BotError(f"unknown expression '{name_or_index}'")


def browse(service: str, seconds: float = 2.5) -> list[str]:
    """Instance names advertised for a DNS-SD service type (e.g. _wled._tcp)."""
    try:
        p = subprocess.Popen(["dns-sd", "-B", service, "local."],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    except OSError:
        return []
    time.sleep(seconds)
    p.terminate()
    out = p.communicate(timeout=2)[0] if p.stdout else ""
    names = []
    for line in out.splitlines():
        m = re.search(r"\sAdd\s+\d+\s+\d+\s+\S+\s+\S+\s+(.+)$", line)
        if m and m.group(1).strip() not in names:
            names.append(m.group(1).strip())
    return names


def discover_stackchan() -> str | None:
    """Host of the first vizBot on the LAN whose board is a Stackchan."""
    for name in browse("_http._tcp"):
        if not name.lower().startswith("vizbot"):
            continue
        try:
            # Resolve once, IPv4: urllib's own .local lookup can stall ~5 s.
            host = socket.getaddrinfo(f"{name}.local", 80, socket.AF_INET)[0][4][0]
            if Bot(host).state().get("boardType") == "stackchan":
                return host
        except (BotError, OSError):
            continue
    return None
