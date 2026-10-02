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

LED_MODES = ["off", "breathing", "rainbow", "chase", "fire", "twinkle", "pulse", "aurora", "mood", "audio"]


@dataclass
class Tool:
    name: str
    description: str
    schema: dict
    fn: Callable[..., str]

    def to_api(self) -> dict:
        return {"name": self.name, "description": self.description, "input_schema": self.schema}


def _obj(props: dict, required: list[str] | None = None) -> dict:
    return {"type": "object", "properties": props, "required": required or [],
            "additionalProperties": False}


class Toolbox:
    def __init__(self, get_bot: Callable[[], Bot | None], wled: Wled, vizmac: VizMac,
                 memory: Memory | None = None, routines: Routines | None = None):
        self.get_bot = get_bot
        self.wled = wled
        self.vizmac = vizmac
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
                     f"Keyboard (vizMac): {'online' if self.vizmac.available() else 'offline'}."))

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
            self._weather)

        add("look",
            "Take a photo with your head camera and see it. Use it when asked what you see, what Kevin is "
            "holding or wearing, or who is there. Point your head first with move_head if needed.",
            _obj({}),
            self._look)

        add("play_sound",
            "Play a sound effect on your synth, for fun or emphasis (a laugh at a joke, a fanfare for good news). "
            "Use sparingly.",
            _obj({"name": {"type": "string", "enum": list(SOUNDS)}}, ["name"]),
            lambda name: (self._bot().sound(name), f"played {name}")[1])

        add("remember",
            "Save a short fact to remember across days: names, preferences, plans, things Kevin asks you to "
            "keep. One fact per call, written so it makes sense later.",
            _obj({"fact": {"type": "string"}}, ["fact"]),
            lambda fact: self.memory.remember(fact))

        add("forget",
            "Forget remembered facts that contain the given words.",
            _obj({"about": {"type": "string"}}, ["about"]),
            lambda about: self.memory.forget(about))

        add("add_routine",
            "Schedule something you'll do every day or on certain days at a set time, e.g. a morning greeting "
            "with the weather, or turning the lab lights off in the evening. 'what' is an instruction to yourself.",
            _obj({"time": {"type": "string", "description": "like 08:30 or 5:15 pm"},
                  "days": {"type": "string", "description": "daily, weekdays, weekends, or e.g. mon,wed,fri"},
                  "what": {"type": "string"}}, ["time", "days", "what"]),
            lambda time, days, what: self.routines.add(time, days, what))

        add("remove_routine",
            "Remove a scheduled routine by its id (like r2) or words from its description.",
            _obj({"which": {"type": "string"}}, ["which"]),
            lambda which: self.routines.remove(which))

        add("keyboard_effect",
            "Set the vizMac keyboard lighting effect by name (e.g. plasma, rainbow, fire).",
            _obj({"name": {"type": "string"}}, ["name"]),
            lambda name: self.vizmac.set_effect(name))

        add("keyboard_flash",
            "Flash the whole vizMac keyboard a color for a moment, as a signal.",
            _obj({"color": {"type": "string"}, "ms": {"type": "integer"}}, ["color"]),
            lambda color, ms=2000: self.vizmac.flash(color, ms))

    def _add(self, name, description, schema, fn) -> None:
        self.tools[name] = Tool(name, description, schema, fn)

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
        # The bot stores its location (set in its web panel); default Richmond, VA.
        lat, lon = "37.54", "-77.43"
        bot = self.get_bot()
        if bot is not None:
            try:
                st = bot.state()
                lat, lon = st.get("weatherLat") or lat, st.get("weatherLon") or lon
            except Exception:  # noqa: BLE001 - fall back to the default location
                pass
        return weather(lat, lon)

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

    def api_list(self) -> list[dict]:
        return [t.to_api() for t in self.tools.values()]

    def run(self, name: str, args: dict) -> tuple[str | list, bool]:
        """Returns (result, is_error). Result is text, or content blocks (look's photo)."""
        tool = self.tools.get(name)
        if tool is None:
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
