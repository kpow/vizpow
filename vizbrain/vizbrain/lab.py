"""Lab devices: WLED lights (JSON API) and vizMac (keyboard lights)."""

from __future__ import annotations

import json
import re
import socket
import subprocess
import threading
import time
import urllib.error
import urllib.request

from .bot import browse


def _http_json(method: str, url: str, body: dict | None = None, timeout: float = 2.5):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    if data is not None:
        req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        raw = r.read()
    return json.loads(raw) if raw else {}


COLOR_NAMES = {
    "red": (255, 0, 0), "orange": (255, 100, 0), "yellow": (255, 200, 0),
    "green": (0, 255, 0), "teal": (0, 200, 160), "cyan": (0, 255, 255),
    "blue": (0, 60, 255), "purple": (150, 0, 255), "violet": (130, 40, 255),
    "magenta": (255, 0, 200), "pink": (255, 60, 150), "white": (255, 255, 255),
    "warm white": (255, 180, 100), "gold": (255, 170, 0), "lime": (120, 255, 0),
}


def parse_color(value: str) -> tuple[int, int, int]:
    v = value.strip().lower()
    if v in COLOR_NAMES:
        return COLOR_NAMES[v]
    v = v.lstrip("#")
    if len(v) == 6:
        try:
            return int(v[0:2], 16), int(v[2:4], 16), int(v[4:6], 16)
        except ValueError:
            pass
    raise ValueError(f"unknown color '{value}' (use a name like purple or #RRGGBB)")


class Wled:
    """Discovers WLED devices over mDNS and remembers them by name."""

    REFRESH_S = 300

    def __init__(self, allow: list[str] | None):
        self.allow = [a.lower() for a in allow] if allow else None
        self.devices: dict[str, dict] = {}  # lowercased name -> {name, host, ip, effects}
        self._lock = threading.Lock()
        self._last_scan = 0.0

    def refresh(self, force: bool = False) -> None:
        if not force and time.time() - self._last_scan < self.REFRESH_S and self.devices:
            return
        found: dict[str, dict] = {}
        for inst in browse("_wled._tcp", seconds=2.0):
            host = f"{inst}.local"
            try:
                ip = socket.getaddrinfo(host, 80, socket.AF_INET)[0][4][0]
                info = _http_json("GET", f"http://{ip}/json/info", timeout=2.0)
            except (OSError, ValueError, urllib.error.URLError):
                continue
            if not info.get("leds", {}).get("count"):
                continue
            name = info.get("name") or inst
            if self.allow is not None and name.lower() not in self.allow:
                continue
            found[name.lower()] = {"name": name, "host": host, "ip": ip}
        with self._lock:
            self.devices = found
            self._last_scan = time.time()
        print(f"[wled] {len(found)} device(s): {', '.join(d['name'] for d in found.values())}")

    def cached_names(self) -> list[str]:
        """Names without blocking on a scan; kicks off a background refresh when stale."""
        if time.time() - self._last_scan >= self.REFRESH_S:
            self._last_scan = time.time()  # one refresh at a time
            threading.Thread(target=self.refresh, kwargs={"force": True}, daemon=True).start()
        with self._lock:
            return sorted(d["name"] for d in self.devices.values())

    def names(self) -> list[str]:
        self.refresh()
        with self._lock:
            return sorted(d["name"] for d in self.devices.values())

    def _targets(self, device: str) -> list[dict]:
        self.refresh()
        with self._lock:
            if device.strip().lower() in ("all", "*", "everything"):
                return list(self.devices.values())
            d = self.devices.get(device.strip().lower())
        if not d:
            raise ValueError(f"no WLED device named '{device}'. Known: {', '.join(self.names())}")
        return [d]

    def effects(self, device: str) -> list[str]:
        d = self._targets(device)[0]
        if "effects" not in d:
            d["effects"] = _http_json("GET", f"http://{d['ip']}/json/eff")
        return d["effects"]

    def set(self, device: str, on: bool | None = None, brightness: int | None = None,
            color: str | None = None, effect: str | None = None) -> str:
        targets = self._targets(device)
        if len(targets) > 1:
            # "all": in parallel, so one slow light can't time out the rest.
            from concurrent.futures import ThreadPoolExecutor
            with ThreadPoolExecutor(max_workers=8) as ex:
                futs = {d["name"]: ex.submit(self.set, d["name"], on, brightness, color, effect)
                        for d in targets}
            failed = []
            for name, f in futs.items():
                try:
                    f.result()
                except Exception as e:  # noqa: BLE001
                    failed.append(f"{name} ({e})")
            ok = len(targets) - len(failed)
            return f"updated {ok} of {len(targets)} lights" + (f"; failed: {', '.join(failed)}" if failed else "")
        done = []
        for d in targets:
            state: dict = {}
            if on is not None:
                state["on"] = bool(on)
            if brightness is not None:
                state["bri"] = max(1, min(255, int(brightness)))
                state.setdefault("on", True)
            seg: dict = {}
            if color:
                seg["col"] = [list(parse_color(color))]
                state.setdefault("on", True)
            if effect:
                names = [e.lower() for e in self.effects(d["name"])]
                key = effect.strip().lower()
                if key not in names:
                    import difflib
                    near = difflib.get_close_matches(key, names, n=5, cutoff=0.4)
                    raise ValueError(f"unknown WLED effect '{effect}'"
                                     + (f"; closest: {', '.join(near)}" if near else ""))
                seg["fx"] = names.index(key)
                state.setdefault("on", True)
            elif color:
                seg["fx"] = 0  # Solid, so the color shows as asked
            if seg:
                state["seg"] = [seg]
            _http_json("POST", f"http://{d['ip']}/json/state", state)
            done.append(d["name"])
        return f"updated {', '.join(done)}"


class VizMac:
    """vizMac's local HTTP API (keyboard lighting). Localhost needs no token."""

    def __init__(self, base_url: str):
        self.base = base_url.rstrip("/")

    def available(self) -> bool:
        try:
            _http_json("GET", f"{self.base}/api/state", timeout=1.0)
            return True
        except (OSError, ValueError, urllib.error.URLError):
            return False

    def effects(self) -> list[str]:
        data = _http_json("GET", f"{self.base}/api/effects")
        return [e["name"] for g in data.get("groups", []) for e in g.get("effects", [])]

    def set_effect(self, name: str) -> str:
        known = self.effects()
        match = next((e for e in known if e.lower() == name.lower()), None)
        if match is None:
            match = next((e for e in known if name.lower() in e.lower()), None)
        if match is None:
            raise ValueError(f"unknown keyboard effect '{name}'. Known: {', '.join(known[:40])}")
        _http_json("POST", f"{self.base}/api/effect", {"name": match})
        return f"keyboard effect {match}"

    # ---- music (vizMac reads Spotify/Music; vizbrain drives Spotify directly) ----

    def now_playing(self) -> str:
        m = _http_json("GET", f"{self.base}/api/state", timeout=2.0).get("media") or {}
        if not m.get("app"):
            return "Nothing is playing (Spotify and Music are closed)."
        if m.get("state") == "stopped" or not m.get("title"):
            return f"{m['app']} is open but stopped."
        mmss = lambda s: f"{int(s) // 60}:{int(s) % 60:02d}"
        return (f"{m['app']} is {m['state']}: \"{m['title']}\" by {m.get('artist', 'unknown')} "
                f"({mmss(m.get('pos', 0))} of {mmss(m.get('dur', 0))}).")

    def music(self, action: str, count: int = 1) -> str:
        state = (_http_json("GET", f"{self.base}/api/state", timeout=2.0).get("media") or {}).get("state")
        if action in ("play", "pause"):
            if (action == "play") == (state == "playing"):
                return f"already {'playing' if action == 'play' else 'paused'}"
            action = "playpause"
        _http_json("POST", f"{self.base}/api/media", {"action": action, "count": max(1, min(9, count))})
        return f"music: {action}"

    @staticmethod
    def _spotify(script: str) -> str:
        r = subprocess.run(["osascript", "-e", script], capture_output=True, text=True, timeout=8)
        if r.returncode != 0:
            raise RuntimeError(r.stderr.strip() or "Spotify didn't respond")
        return r.stdout.strip()

    def play_spotify(self, link: str) -> str:
        """Play a Spotify playlist/album/artist/track from a URI or open.spotify.com link."""
        m = re.search(r"(playlist|album|artist|track|show|episode)[/:]([A-Za-z0-9]{10,})", link)
        if not m:
            raise ValueError("need a Spotify link like https://open.spotify.com/playlist/... or spotify:playlist:...")
        uri = f"spotify:{m.group(1)}:{m.group(2)}"
        self._spotify(f'tell application "Spotify" to play track "{uri}"')
        return f"playing {uri}"

    def volume(self, level: int) -> str:
        level = max(0, min(100, int(level)))
        self._spotify(f'tell application "Spotify" to set sound volume to {level}')
        return f"Spotify volume {level}"

    def flash(self, color: str, ms: int = 2000) -> str:
        r, g, b = parse_color(color)
        _http_json("POST", f"{self.base}/api/overlay",
                   {"kind": "flash", "color": f"#{r:02x}{g:02x}{b:02x}", "ms": max(100, min(120000, int(ms)))})
        return f"keyboard flashed {color}"


# WMO weather codes -> words (Open-Meteo, same source the bot's firmware uses).
_WMO = {
    0: "clear", 1: "mostly clear", 2: "partly cloudy", 3: "overcast", 45: "foggy", 48: "foggy",
    51: "light drizzle", 53: "drizzle", 55: "heavy drizzle", 61: "light rain", 63: "rain",
    65: "heavy rain", 66: "freezing rain", 67: "freezing rain", 71: "light snow", 73: "snow",
    75: "heavy snow", 77: "snow grains", 80: "rain showers", 81: "rain showers",
    82: "heavy rain showers", 85: "snow showers", 86: "heavy snow showers",
    95: "thunderstorms", 96: "thunderstorms with hail", 99: "thunderstorms with hail",
}


def weather(lat: str, lon: str) -> str:
    """Current conditions plus today and tomorrow, in °F and mph."""
    url = ("https://api.open-meteo.com/v1/forecast"
           f"?latitude={lat}&longitude={lon}"
           "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m"
           "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code"
           "&temperature_unit=fahrenheit&wind_speed_unit=mph&timezone=auto&forecast_days=2")
    d = _http_json("GET", url, timeout=5.0)
    c, day = d["current"], d["daily"]
    parts = [f"Now: {round(c['temperature_2m'])}°F (feels like {round(c['apparent_temperature'])}), "
             f"{_WMO.get(c['weather_code'], 'unknown sky')}, wind {round(c['wind_speed_10m'])} mph."]
    for i, label in enumerate(("Today", "Tomorrow")):
        parts.append(f"{label}: high {round(day['temperature_2m_max'][i])}, low {round(day['temperature_2m_min'][i])}, "
                     f"{_WMO.get(day['weather_code'][i], 'unknown sky')}, "
                     f"{day['precipitation_probability_max'][i]}% chance of rain.")
    return " ".join(parts)
