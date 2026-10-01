"""vizlab-mcp: the lab tools as an MCP server (stdio), for Claude Code / Desktop.

Same Toolbox the brain uses, so the safety allow-list (plan D7) holds here
too. Register with Claude Code:

    claude mcp add vizlab -- /Users/kpow/projects/vizpow/vizbrain/.venv/bin/vizbrain mcp
"""

from __future__ import annotations

import socket

from mcp.server.mcpserver import MCPServer

from . import config
from .bot import Bot, discover_stackchan
from .lab import VizMac, Wled
from .tools import Toolbox


def build() -> MCPServer:
    settings = config.load()
    state: dict = {"bot": None}

    def get_bot() -> Bot | None:
        if state["bot"] is None:
            host = settings["bot_host"] or discover_stackchan()
            if host and host.endswith(".local"):
                host = socket.getaddrinfo(host, 80, socket.AF_INET)[0][4][0]
            state["bot"] = Bot(host) if host else None
        return state["bot"]

    box = Toolbox(get_bot, Wled(settings["wled_allow"]), VizMac(settings["vizmac_url"]))

    def call(tool: str, **args) -> str:
        out, is_err = box.run(tool, {k: v for k, v in args.items() if v is not None})
        if is_err:
            raise RuntimeError(out)
        return out

    server = MCPServer("vizlab", instructions=(
        "Controls Kevin's office lab: the vizBot Stackchan robot (face, head, base LEDs, "
        "speech bubble), WLED lights, and the vizMac keyboard lighting."))

    @server.tool()
    def list_lab_devices() -> str:
        """List WLED light names and whether the vizMac keyboard is online."""
        return call("list_lab_devices")

    @server.tool()
    def wled_set(device: str, on: bool | None = None, brightness: int | None = None,
                 color: str | None = None, effect: str | None = None) -> str:
        """Control a WLED light by name (or 'all'): on/off, brightness 1-255, color (name or #RRGGBB), effect name."""
        return call("wled_set", device=device, on=on, brightness=brightness, color=color, effect=effect)

    @server.tool()
    def keyboard_effect(name: str) -> str:
        """Set the vizMac keyboard lighting effect by name (e.g. plasma, rainbow)."""
        return call("keyboard_effect", name=name)

    @server.tool()
    def keyboard_flash(color: str, ms: int = 2000) -> str:
        """Flash the vizMac keyboard a color for ms milliseconds."""
        return call("keyboard_flash", color=color, ms=ms)

    @server.tool()
    def bot_expression(name: str) -> str:
        """Set vizBot's face (neutral, happy, sad, surprised, thinking, excited, love, ...)."""
        return call("set_expression", name=name)

    @server.tool()
    def bot_head(yaw: float | None = None, pitch: float | None = None, ms: int = 600) -> str:
        """Point vizBot's head. yaw -60..60 degrees (positive = his left), pitch 25 (down) .. 85 (up)."""
        return call("move_head", yaw=yaw, pitch=pitch, ms=ms)

    @server.tool()
    def bot_gesture(name: str) -> str:
        """Head gesture: nod, shake, lookup, lookdown, left, right, recenter."""
        return call("head_gesture", name=name)

    @server.tool()
    def bot_base_leds(color: str | None = None, mode: str | None = None) -> str:
        """vizBot's base LED ring: a solid color, or a mode (off, breathing, rainbow, chase, fire, twinkle, pulse, aurora, mood, audio)."""
        return call("set_base_leds", color=color, mode=mode)

    @server.tool()
    def bot_say(text: str, seconds: float = 4) -> str:
        """Show text in vizBot's speech bubble (silent)."""
        bot = get_bot()
        if bot is None:
            raise RuntimeError("vizBot is not reachable")
        bot.say(text, int(seconds * 1000))
        return "shown"

    return server


def run() -> None:
    build().run("stdio")
