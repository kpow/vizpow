"""The tool registry: everything Claude (and vizlab-mcp) can do.

Safety allow-list (plan D7): no tool reaches power, WiFi, firmware update,
servo re-init or device naming. Only looks, motion and lights.
"""

from __future__ import annotations

import threading
from dataclasses import dataclass
from typing import Callable

from .bot import EXPRESSIONS, GESTURES, Bot
from .lab import COLOR_NAMES, VizMac, Wled, parse_color

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
    def __init__(self, get_bot: Callable[[], Bot | None], wled: Wled, vizmac: VizMac):
        self.get_bot = get_bot
        self.wled = wled
        self.vizmac = vizmac
        self.tools: dict[str, Tool] = {}
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

    def run(self, name: str, args: dict) -> tuple[str, bool]:
        """Returns (result text, is_error)."""
        tool = self.tools.get(name)
        if tool is None:
            return f"unknown tool {name}", True
        try:
            return str(tool.fn(**(args or {}))), False
        except Exception as e:  # noqa: BLE001 - tool errors go back to the model
            return f"error: {e}", True


def _quiet(fn, *args) -> None:
    try:
        fn(*args)
    except Exception as e:  # noqa: BLE001 - background gesture, log only
        print(f"[tools] {fn.__name__} failed: {e}")
