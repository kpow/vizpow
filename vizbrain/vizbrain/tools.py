"""The tool registry: everything Claude (and vizlab-mcp) can do.

Safety allow-list (plan D7): no tool reaches power, WiFi, firmware update,
servo re-init or device naming. Only looks, motion and lights.
"""

from __future__ import annotations

import base64
import threading
import time
from dataclasses import dataclass
from typing import Callable

from .bot import EXPRESSIONS, GESTURES, SOUNDS, Bot
from .lab import COLOR_NAMES, VizMac, Wled, parse_color, weather
from .memory import Memory, Routines

# Lab actions get an automatic synth chime: confirm when they work, error when not.
CHIME_TOOLS = {"wled_set", "keyboard_effect", "keyboard_flash", "set_base_leds"}

# Tools that need gear not every home has. Claude is only offered the ones
# whose gear answers (Toolbox.api_list), so he can't "turn on" lights that
# aren't there.
WLED_TOOLS = {"wled_set"}
VIZMAC_TOOLS = {"keyboard_effect", "keyboard_flash", "music_status", "music_control"}
SPOTIFY_TOOLS = {"music_play", "music_volume"}

LED_MODES = ["off", "breathing", "rainbow", "chase", "fire", "twinkle", "pulse", "aurora", "mood", "audio"]


@dataclass
class Tool:
    name: str
    description: str
    schema: dict
    fn: Callable[..., str]
    info: bool = False   # looks something up: the model must read the result before it can answer
    keeps: bool = False  # changes what he remembers or does later (memory, routines)

    def to_api(self) -> dict:
        return {"name": self.name, "description": self.description, "input_schema": self.schema}


def _obj(props: dict, required: list[str] | None = None) -> dict:
    return {"type": "object", "properties": props, "required": required or [],
            "additionalProperties": False}


class Toolbox:
    def __init__(self, get_bot: Callable[[], Bot | None], wled: Wled, vizmac: VizMac,
                 memory: Memory | None = None, routines: Routines | None = None,
                 location: dict | None = None, connectors=None):
        self.get_bot = get_bot
        self.connectors = connectors      # a connectors.Hub, or None when connectors.json lists none
        self.wled = wled
        self.vizmac = vizmac
        self.location = location or {}
        self.memory = memory or Memory()
        self.routines = routines or Routines()
        self.tools: dict[str, Tool] = {}
        self._wx, self._wx_at = "", 0.0
        self._register()

    def _bot(self) -> Bot:
        bot = self.get_bot()
        if bot is None:
            raise RuntimeError("vizBot is not reachable")
        return bot

    def _register(self) -> None:
        add = self._add

        add("set_expression",
            "Change vizBot's face to an expression. Use it to show how you feel about what you're saying.",
            _obj({"name": {"type": "string", "enum": EXPRESSIONS}}, ["name"]),
            lambda name: f"face is now {EXPRESSIONS[self._bot().expression(name)]}")

        add("move_head",
            "Point vizBot's head. yaw: degrees, positive = his left, 0 = straight ahead (-60..60). "
            "pitch: degrees, 25 = looking down, 48 = resting, 85 = looking up.",
            _obj({"yaw": {"type": "number"}, "pitch": {"type": "number"},
                  "ms": {"type": "integer", "description": "move time, 200-3000"}}),
            lambda yaw=None, pitch=None, ms=600: (self._bot().head(yaw, pitch, ms), "head moved")[1])

        add("head_gesture",
            "Do a head gesture: nod (yes), shake (no), lookup, lookdown, left, right, or recenter.",
            _obj({"name": {"type": "string", "enum": GESTURES}}, ["name"]),
            self._gesture)

        add("set_base_leds",
            "Light vizBot's 12-LED base ring. Give a color for a solid color, or a mode for an animation.",
            _obj({"color": {"type": "string", "description": f"a name ({', '.join(COLOR_NAMES)}) or #RRGGBB"},
                  "mode": {"type": "string", "enum": LED_MODES}}),
            self._base_leds)

        add("list_lab_devices",
            "List the lab devices you can control: WLED light names and whether the vizMac keyboard is online.",
            _obj({}),
            lambda: (f"WLED lights: {', '.join(self.wled.names()) or 'none found'}. "
                     f"Keyboard (vizMac): {'online' if self.vizmac.available() else 'offline'}."),
            info=True)

        add("wled_set",
            "Control a WLED light by name (or 'all'). Any combination of on/off, brightness 1-255, "
            "color (name or #RRGGBB, sets a solid color) and effect name (e.g. Rainbow, Fire 2012, Breathe).",
            _obj({"device": {"type": "string"}, "on": {"type": "boolean"},
                  "brightness": {"type": "integer"}, "color": {"type": "string"},
                  "effect": {"type": "string"}}, ["device"]),
            lambda device, on=None, brightness=None, color=None, effect=None:
                self.wled.set(device, on, brightness, color, effect))

        add("get_weather",
            "Get the local weather: current conditions plus today's and tomorrow's forecast.",
            _obj({}),
            self._weather, info=True)

        add("look",
            "Take a photo with your head camera and see it. Use it when asked what you see, what someone is "
            "holding or wearing, or who is there. Point your head first with move_head if needed.",
            _obj({}),
            self._look, info=True)

        add("play_sound",
            "Play a sound effect on your synth, for fun or emphasis (a laugh at a joke, a fanfare for good news). "
            "Use sparingly.",
            _obj({"name": {"type": "string", "enum": list(SOUNDS)}}, ["name"]),
            lambda name: (self._bot().sound(name), f"played {name}")[1])

        add("remember",
            "Save a short fact to remember across days: names, preferences, plans, things you're asked to "
            "keep. One fact per call, written so it makes sense later.",
            _obj({"fact": {"type": "string"}}, ["fact"]),
            lambda fact: self.memory.remember(fact), keeps=True)

        add("forget",
            "Forget remembered facts that contain the given words.",
            _obj({"about": {"type": "string"}}, ["about"]),
            lambda about: self.memory.forget(about), keeps=True)

        add("add_routine",
            "Schedule something you'll do every day or on certain days at a set time, e.g. a morning greeting "
            "with the weather, or turning the lights off in the evening. 'what' is an instruction to yourself.",
            _obj({"time": {"type": "string", "description": "like 08:30 or 5:15 pm"},
                  "days": {"type": "string", "description": "daily, weekdays, weekends, or e.g. mon,wed,fri"},
                  "what": {"type": "string"}}, ["time", "days", "what"]),
            lambda time, days, what: self.routines.add(time, days, what), keeps=True)

        add("remove_routine",
            "Remove a scheduled routine by its id (like r2) or words from its description.",
            _obj({"which": {"type": "string"}}, ["which"]),
            lambda which: self.routines.remove(which), keeps=True)

        add("music_status",
            "What's playing in Spotify or Apple Music right now (title, artist, playing or paused).",
            _obj({}),
            lambda: self.vizmac.now_playing(), info=True)

        add("music_control",
            "Control the music on the Mac: play, pause, next, previous, or restart the song. "
            "count skips several songs with next/previous.",
            _obj({"action": {"type": "string", "enum": ["play", "pause", "next", "previous", "restart"]},
                  "count": {"type": "integer"}}, ["action"]),
            lambda action, count=1: self.vizmac.music(action, count))

        add("music_play",
            "Play something specific on Spotify. Needs a Spotify link: if you don't know one, first use "
            "web_search to find an open.spotify.com playlist, album, artist or track link that fits, then pass it here.",
            _obj({"link": {"type": "string", "description": "open.spotify.com/... URL or spotify:... URI"}}, ["link"]),
            lambda link: self.vizmac.play_spotify(link))

        add("music_volume",
            "Set the Spotify volume, 0 to 100.",
            _obj({"level": {"type": "integer"}}, ["level"]),
            lambda level: self.vizmac.volume(level))

        add("keyboard_effect",
            "Set the vizMac keyboard lighting effect by name (e.g. plasma, rainbow, fire).",
            _obj({"name": {"type": "string"}}, ["name"]),
            lambda name: self.vizmac.set_effect(name))

        add("keyboard_flash",
            "Flash the whole vizMac keyboard a color for a moment, as a signal.",
            _obj({"color": {"type": "string"}, "ms": {"type": "integer"}}, ["color"]),
            lambda color, ms=2000: self.vizmac.flash(color, ms))

    def _add(self, name, description, schema, fn, info: bool = False, keeps: bool = False) -> None:
        self.tools[name] = Tool(name, description, schema, fn, info, keeps)

    def keeps(self, name: str) -> bool:
        """Whether this tool's effect outlives the turn (memory and routines
        are fed back into every prompt; routines run unattended)."""
        tool = self.tools.get(name)
        return bool(tool and tool.keeps)

    def reads(self, name: str) -> bool:
        """Whether the model has to read this tool's result before it can answer.
        True for look-up tools, and for any name that isn't a built-in, since a
        tool we know nothing about may well be answering a question."""
        tool = self.tools.get(name)
        if tool:
            return tool.info
        reads = self.connectors.reads(name) if self.connectors else None
        return True if reads is None else reads

    def cached_weather(self) -> str:
        """Weather for the prompt, refreshed in the background every 15 min."""
        now = time.time()
        if now - self._wx_at > 900:
            self._wx_at = now
            def refresh():
                try:
                    self._wx = self._weather()
                except Exception as e:  # noqa: BLE001
                    print(f"[tools] weather refresh failed: {e}")
            threading.Thread(target=refresh, daemon=True).start()
        return self._wx

    def _weather(self) -> str:
        # The location setting wins; without one, the bot's own (set in its web panel).
        lat, lon = self.location.get("lat"), self.location.get("lon")
        if lat is None or lon is None:
            bot = self.get_bot()
            if bot is not None:
                try:
                    st = bot.state()
                    lat, lon = st.get("weatherLat"), st.get("weatherLon")
                except Exception:  # noqa: BLE001 - reported below as no location
                    pass
        try:   # numbers only: these go into a URL
            lat, lon = f"{float(lat):.4f}", f"{float(lon):.4f}"
        except (TypeError, ValueError):
            raise RuntimeError("no location set yet (run vizbrain setup on the Mac)") from None
        # Fahrenheit where people use it; Celsius everywhere else that says where it is.
        metric = (self.location.get("country") or "US").upper() not in ("US", "LR", "MM", "BS", "BZ", "KY", "PW")
        return weather(lat, lon, metric)

    def _look(self) -> list:
        jpeg = self._bot().photo()
        return [
            {"type": "image", "source": {"type": "base64", "media_type": "image/jpeg",
                                         "data": base64.b64encode(jpeg).decode()}},
            {"type": "text", "text": "Photo from your head camera, just now (320x240)."},
        ]

    def _chime(self, name: str) -> None:
        bot = self.get_bot()
        if bot is not None:
            threading.Thread(target=lambda: _quiet(bot.sound, name), daemon=True).start()

    def _gesture(self, name: str) -> str:
        # The bot answers only after the move finishes (~1.7 s for a nod), so
        # fire it off and let the reply carry on in parallel.
        bot = self._bot()
        if name not in GESTURES:
            raise ValueError(f"unknown gesture {name}")
        threading.Thread(target=lambda: _quiet(bot.gesture, name), daemon=True).start()
        return f"doing {name}"

    def _base_leds(self, color: str | None = None, mode: str | None = None) -> str:
        bot = self._bot()
        if color:
            r, g, b = parse_color(color)
            bot.base_led_mode("off")
            bot.base_leds(r, g, b)
            return f"base ring {color}"
        if mode:
            bot.base_led_mode(mode)
            return f"base ring mode {mode}"
        return "nothing to change"

    def unavailable(self) -> set[str]:
        """Tools whose gear isn't there right now."""
        out: set[str] = set()
        if not self.wled.cached_names():
            out |= WLED_TOOLS
        if not self.vizmac.cached_available():
            out |= VIZMAC_TOOLS
        if not self.vizmac.spotify_installed():
            out |= SPOTIFY_TOOLS
        return out

    def api_list(self) -> list[dict]:
        """Built-in tools for the gear that's here, then every connector's tools."""
        hidden = self.unavailable()
        built_in = [t.to_api() for t in self.tools.values() if t.name not in hidden]
        return built_in + (self.connectors.api_list() if self.connectors else [])

    def run(self, name: str, args: dict, timeout: float | None = None) -> tuple[str | list, bool]:
        """Returns (result, is_error). Result is text, or content blocks (look's photo)."""
        tool = self.tools.get(name)
        if tool is None:
            if self.connectors and self.connectors.owns(name):
                return self.connectors.run(name, args, timeout)
            return f"unknown tool {name}", True
        try:
            out = tool.fn(**(args or {}))
            result, ok = (out if isinstance(out, list) else str(out)), True
        except Exception as e:  # noqa: BLE001 - tool errors go back to the model
            result, ok = f"error: {e}", False
        if name in CHIME_TOOLS:
            failed = (not ok) or (isinstance(result, str) and "failed:" in result)
            self._chime("error" if failed else "confirm")
        return result, not ok


def _quiet(fn, *args) -> None:
    try:
        fn(*args)
    except Exception as e:  # noqa: BLE001 - background gesture, log only
        print(f"[tools] {fn.__name__} failed: {e}")
