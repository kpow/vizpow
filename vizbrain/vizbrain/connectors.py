"""Connectors: MCP servers listed in connectors.json whose tools vizBot can use.

Each tool is offered to Claude as "<connector>__<tool>" next to the built-in
tools. This module's pure half decides what gets offered and turns what comes
back into tool_result text; it never imports the MCP library, so an install
with no connectors never loads it.
"""

from __future__ import annotations

import asyncio
import concurrent.futures
import hashlib
import json
import math
import os
import re
import shutil
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path

MAX_TOOLS_PER_CONNECTOR = 12
MAX_TOOLS_TOTAL = 24
MAX_DESCRIPTION = 400
MAX_SCHEMA_CHARS = 4000
API_NAME_LIMIT = 64          # Anthropic tool names: ^[a-zA-Z0-9_-]{1,64}$
SEPARATOR = "__"             # built-in tool names never contain it


@dataclass(frozen=True)
class ConnectorTool:
    api_name: str            # what Claude calls it: "kpow__recent_build_log"
    connector: str           # "kpow"
    original: str            # what the server calls it: "recent_build_log"
    api: dict                # the tool definition sent to Claude
    reads: bool              # Claude must read the result (False only for listed "actions")


def api_name(connector: str, tool: str, taken: set[str] | frozenset[str] = frozenset()) -> str:
    """The name Claude sees. MCP allows dots and 128 characters; the API
    doesn't, so oddities are replaced and long or clashing names get a short
    hash of the original to stay unique."""
    base = f"{connector}{SEPARATOR}{re.sub(r'[^a-zA-Z0-9_-]', '_', tool)}"
    if len(base) <= API_NAME_LIMIT and base not in taken:
        return base
    tag = "_" + hashlib.sha1(tool.encode()).hexdigest()[:6]
    return base[:API_NAME_LIMIT - len(tag)] + tag


def schema_problem(schema) -> str | None:
    """Why this input schema can't be sent to Claude, or None. One bad schema
    would fail every request, so a doubtful tool is skipped instead."""
    if not isinstance(schema, dict) or schema.get("type") != "object":
        return "its input schema isn't an object"
    if any(k in schema for k in ("anyOf", "oneOf", "allOf")):
        return "its input schema has anyOf/oneOf/allOf at the top level"
    try:
        size = len(json.dumps(schema))
    except (TypeError, ValueError):
        return "its input schema isn't plain JSON"
    if size > MAX_SCHEMA_CHARS:
        return f"its input schema is too big ({size} characters)"
    return None


def describe(tool, connector: str) -> str:
    text = " ".join((getattr(tool, "description", None) or "").split())
    if not text:
        text = f"{getattr(tool, 'title', None) or tool.name} (from the {connector} connector)"
    if len(text) > MAX_DESCRIPTION:
        text = text[:MAX_DESCRIPTION - 1].rsplit(" ", 1)[0] + "…"
    return text


def catalog(conn: dict, tools) -> tuple[list[ConnectorTool], list[str]]:
    """The tools to offer from one connector, sorted by name, plus a line for
    each one left out. `tools` are MCP Tool objects from list_tools()."""
    name = conn["name"]
    allow = set(conn["tools"]) if conn.get("tools") is not None else None
    actions = set(conn.get("actions") or [])
    limit = min(int(conn.get("max_tools") or MAX_TOOLS_PER_CONNECTOR), 32)
    offered: list[ConnectorTool] = []
    skipped: list[str] = []
    taken: set[str] = set()
    for tool in sorted(tools, key=lambda t: t.name):
        if allow is not None and tool.name not in allow:
            continue
        problem = schema_problem(getattr(tool, "input_schema", None))
        if problem:
            skipped.append(f"{name}: {tool.name} skipped, {problem}")
            continue
        if len(offered) >= limit:
            skipped.append(f"{name}: {tool.name} skipped, over the {limit}-tool limit")
            continue
        api = api_name(name, tool.name, taken)
        taken.add(api)
        schema = json.loads(json.dumps(tool.input_schema, sort_keys=True))   # byte-stable for the prompt cache
        offered.append(ConnectorTool(api, name, tool.name,
                                     {"name": api, "description": describe(tool, name), "input_schema": schema},
                                     reads=tool.name not in actions))
    if allow:
        missing = sorted(allow - {t.name for t in tools})
        skipped += [f"{name}: {t} is in the tools list but the server doesn't offer it" for t in missing]
    return offered, skipped


def cap_total(tools: list[ConnectorTool], limit: int = MAX_TOOLS_TOTAL) -> tuple[list[ConnectorTool], list[str]]:
    """Every connector's tools together, sorted by name, at most `limit` of them."""
    tools = sorted(tools, key=lambda t: t.api_name)
    return tools[:limit], [f"{t.connector}: {t.original} skipped, over the {limit}-tool total" for t in tools[limit:]]


def to_tool_result(result, limit: int) -> tuple[str, bool]:
    """An MCP CallToolResult as (text for Claude, is_error). Only text goes
    through; pictures, sound and files are named, not sent."""
    parts: list[str] = []
    for item in getattr(result, "content", None) or []:
        kind = getattr(item, "type", "")
        if kind == "text":
            parts.append(item.text)
        elif kind == "image":
            parts.append("[image omitted]")
        elif kind == "audio":
            parts.append("[audio omitted]")
        elif kind == "resource_link":
            parts.append(f"[link: {getattr(item, 'name', '')} {getattr(item, 'uri', '')}]".replace("  ", " "))
        elif kind == "resource":
            res = getattr(item, "resource", None)
            parts.append(res.text if getattr(res, "text", None) is not None else "[file omitted]")
    text = "\n".join(p for p in parts if p)
    structured = getattr(result, "structured_content", None)
    if not text.strip() and structured is not None:
        # Servers send structured content as text too; only use it when they didn't.
        text = json.dumps(structured, separators=(",", ":"), ensure_ascii=False)
    text = text.strip() or "(no result)"
    if len(text) > limit:
        text = text[:limit] + f" [truncated, {len(text) - limit} more characters]"
    is_error = bool(getattr(result, "is_error", False))
    return (f"error: {text}" if is_error else text), is_error


# ---- The hub: live MCP sessions ----------------------------------------------
#
# vizbrain is plain threads; the MCP client is async. One daemon thread runs an
# asyncio loop, and each connector gets one long-lived task that holds its
# session open (the client must be entered and exited in the same task). The
# brain thread calls in with run_coroutine_threadsafe and waits a bounded time.

PROBE_EVERY_S = 60       # a dead server is only noticed when something talks to it; also refreshes vocabulary
PROBE_TIMEOUT_S = 5
GIVE_UP_AFTER = 5        # failed starts in a row before retrying only every 5 minutes
VOCABULARY_RESOURCE = "vocabulary"   # a connector's resource of names to expect in speech, one per line
NOW_PLAYING_RESOURCE = "now-playing"  # JSON {playing, title, artist, cover}: watched for song changes
WATCH_EVERY_S = 3
MAX_VOCABULARY = 100     # words kept per connector
HINT_CHARS = 500         # Whisper keeps only ~224 prompt tokens, and drops the start first


class ConnectorError(Exception):
    """A connector that can't start, in words for the status page."""


class _Live:
    """One connector's running state."""

    def __init__(self, conf: dict):
        self.conf = conf
        self.name = conf["name"]
        self.state = "starting"          # starting, ready, restarting, failed, stopped
        self.tools: list[ConnectorTool] = []
        self.skipped: list[str] = []
        self.vocabulary: list[str] = []
        self.now_playing_uri: str | None = None
        self.track: tuple | None = None     # the song last seen playing, as (title, artist, cover)
        self.client = None
        self.error = ""
        self.calls = self.errors = 0
        self.last_ms: int | None = None
        self.wake: asyncio.Event | None = None
        self.restart = False
        self.tried = False               # first start attempt finished, either way


class Hub:
    def __init__(self, conns: dict[str, dict], log_dir: Path | None = None):
        self.live = {name: _Live(conf) for name, conf in conns.items()}
        self.log_dir = log_dir
        self.problems: list[str] = []    # from connectors.json, shown with the status
        self.skipped: list[str] = []     # over the total cap
        self._tools: dict[str, ConnectorTool] = {}
        self._api: list[dict] = []
        self._lock = threading.Lock()
        self._ready = threading.Event()
        self._loop: asyncio.AbstractEventLoop | None = None
        self._thread: threading.Thread | None = None
        self._tasks: list[concurrent.futures.Future] = []
        self._stopping = False
        self.probe_every_s = PROBE_EVERY_S
        self.watch_every_s = WATCH_EVERY_S
        # Called (on the hub's thread) with {"title", "artist", "cover", "connector"} when a
        # connector's now-playing changes to a new song. Set by the app.
        self.on_track = None
        if not self.live:
            self._ready.set()

    # ---- lifecycle -------------------------------------------------------

    def start(self) -> None:
        """Start every connector in the background; returns at once."""
        if self._loop is not None or not self.live:
            return
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(target=self._loop.run_forever, name="connectors", daemon=True)
        self._thread.start()
        self._tasks = [asyncio.run_coroutine_threadsafe(self._supervise(live), self._loop)
                       for live in self.live.values()]

    def wait_ready(self, timeout: float) -> bool:
        """Block until every connector has had its first try at starting."""
        return self._ready.wait(timeout)

    def stop(self, timeout: float = 6.0) -> None:
        """Close every session; each server gets stdin closed, then SIGTERM, then SIGKILL."""
        if self._loop is None:
            return
        self._stopping = True

        def wake_all():
            for live in self.live.values():
                if live.wake:
                    live.wake.set()
        self._loop.call_soon_threadsafe(wake_all)
        concurrent.futures.wait(self._tasks, timeout)
        self._loop.call_soon_threadsafe(self._loop.stop)
        self._thread.join(2)
        self._loop = None

    # ---- what the Toolbox asks -------------------------------------------

    def api_list(self) -> list[dict]:
        with self._lock:
            return list(self._api)

    def owns(self, name: str) -> bool:
        return name in self._tools

    def reads(self, name: str) -> bool | None:
        tool = self._tools.get(name)
        return tool.reads if tool else None

    def run(self, name: str, args: dict | None, timeout: float | None = None) -> tuple[str, bool]:
        """Call a connector tool from the brain thread. Returns (text, is_error).
        timeout can only shorten the connector's own timeout_s."""
        tool = self._tools.get(name)
        if tool is None or self._loop is None:
            return f"error: unknown tool {name}", True
        live = self.live[tool.connector]
        timeout = min(live.conf["timeout_s"], timeout) if timeout else live.conf["timeout_s"]
        fut = asyncio.run_coroutine_threadsafe(self._call(live, tool, args or {}, timeout), self._loop)
        try:
            return fut.result(timeout + 1.5)
        except concurrent.futures.TimeoutError:
            fut.cancel()
            return f"error: the {live.name} connector didn't answer within {timeout:g} seconds", True

    def vocabulary(self) -> list[str]:
        """Every connector's names to expect in speech, without repeats. They take
        turns (each one's first name, then each one's second...), so a connector
        with a long list can't crowd the others out of Whisper's short prompt."""
        seen: set[str] = set()
        out: list[str] = []
        lists = [live.vocabulary for live in self.live.values()]
        for i in range(max((len(v) for v in lists), default=0)):
            for words in lists:
                if i < len(words) and words[i].lower() not in seen:
                    seen.add(words[i].lower())
                    out.append(words[i])
        return out

    def replaced(self) -> set[str]:
        """Built-in tools to hide: those a connector replaces while its tools are on offer."""
        with self._lock:
            offering = {t.connector for t in self._tools.values()}
        return {name for live in self.live.values() if live.name in offering
                for name in live.conf.get("replaces") or []}

    def status(self) -> list[dict]:
        return [{"name": l.name, "state": l.state, "tools": len(l.tools), "vocabulary": len(l.vocabulary),
                 "skipped": l.skipped,
                 "calls": l.calls, "errors": l.errors, "last_ms": l.last_ms, "last_error": l.error}
                for l in self.live.values()]

    # ---- inside the loop ---------------------------------------------------

    async def _supervise(self, live: _Live) -> None:
        live.wake = asyncio.Event()
        failures = 0
        while not self._stopping:
            live.restart = False
            try:
                await self._session(live)
            except Exception as e:  # noqa: BLE001 - any failure means: wait, then start it again
                live.error = _reason(e)
                print(f"[connectors] {live.name}: {live.error}")
            live.client = None
            self._tried(live)
            if self._stopping:
                break
            failures = 1 if live.state == "ready" else failures + 1
            wait = 300 if failures >= GIVE_UP_AFTER else min(60, 2 ** (failures - 1))
            live.state = "failed" if failures >= GIVE_UP_AFTER else "restarting"
            live.wake.clear()
            try:
                await asyncio.wait_for(live.wake.wait(), wait)
            except TimeoutError:
                pass
        live.state = "stopped"

    async def _session(self, live: _Live) -> None:
        import anyio
        from mcp import Client, StdioServerParameters
        from mcp.client.stdio import stdio_client
        from mcp.types import Implementation

        from . import __version__, config

        conf = live.conf
        errlog = None
        if conf["url"]:
            transport = conf["url"]
        else:
            env, problems = config.connector_env(conf)
            if problems:
                raise ConnectorError("; ".join(problems))
            command = resolve_command(conf["command"], env.get("PATH", ""))
            errlog = self._errlog(live.name)
            transport = stdio_client(StdioServerParameters(
                command=command, args=conf["args"], env=env, cwd=conf["cwd"] or str(Path.home())),
                errlog=errlog)
        try:
            # The deadline covers starting and listing tools, then is lifted for the session.
            with anyio.CancelScope(deadline=anyio.current_time() + conf["start_timeout_s"]) as scope:
                async with Client(transport, client_info=Implementation(name="vizbrain", version=__version__),
                                  cache=None) as client:
                    tools = await _list_tools(client)
                    live.vocabulary = await _vocabulary(client)
                    live.now_playing_uri = await _resource_uri(client, NOW_PLAYING_RESOURCE)
                    live.track = None   # the song already playing at start isn't a change
                    scope.deadline = math.inf
                    live.tools, live.skipped = catalog(conf, tools)
                    live.client, live.state, live.error = client, "ready", ""
                    self._publish()
                    self._tried(live)
                    print(f"[connectors] {live.name}: ready, {len(live.tools)} tools")
                    await self._hold(live, client)
            if scope.cancelled_caught:
                raise ConnectorError(f"didn't start within {conf['start_timeout_s']:g} seconds")
        finally:
            if errlog:
                errlog.close()

    async def _hold(self, live: _Live, client) -> None:
        """Keep the session open, checking every minute that the server is still there,
        and every few seconds what it's playing, if it says."""
        import anyio
        next_probe = time.monotonic() + self.probe_every_s
        while not self._stopping and not live.restart:
            live.wake.clear()
            wait = self.watch_every_s if live.now_playing_uri else self.probe_every_s
            with anyio.move_on_after(max(0.05, min(wait, next_probe - time.monotonic()))):
                await live.wake.wait()
            if self._stopping or live.restart:
                return
            if live.now_playing_uri:
                await self._watch(live, client)
            if time.monotonic() >= next_probe:
                next_probe = time.monotonic() + self.probe_every_s
                with anyio.fail_after(PROBE_TIMEOUT_S):
                    await client.list_tools()
                    live.vocabulary = await _vocabulary(client)   # it changes when the server's data does

    async def _watch(self, live: _Live, client) -> None:
        """Note a new song starting; the first one seen is only remembered."""
        import anyio
        try:
            with anyio.fail_after(PROBE_TIMEOUT_S):
                read = await client.read_resource(live.now_playing_uri)
            data = json.loads("".join(getattr(p, "text", "") or "" for p in read.contents) or "{}")
        except Exception:  # noqa: BLE001 - a missed look is fine; the minute probe catches a dead server
            return
        if not isinstance(data, dict) or not data.get("playing") or not data.get("title"):
            return   # paused or nothing: a song resuming after a pause isn't new
        track = (data.get("title"), data.get("artist"), data.get("cover"))
        seen, live.track = live.track, track
        if seen is not None and track != seen and data.get("cover") and self.on_track:
            try:
                self.on_track({"title": track[0], "artist": track[1], "cover": track[2], "connector": live.name})
            except Exception as e:  # noqa: BLE001
                print(f"[connectors] {live.name}: song change not shown: {e}")

    async def _call(self, live: _Live, tool: ConnectorTool, args: dict, timeout: float) -> tuple[str, bool]:
        from mcp import MCPError
        from mcp.types import CONNECTION_CLOSED, REQUEST_TIMEOUT

        client = live.client
        if client is None:
            return f"error: the {live.name} connector isn't running right now", True
        live.calls += 1
        started = time.monotonic()
        try:
            result = await client.call_tool(tool.original, args, read_timeout_seconds=timeout)
        except MCPError as e:
            live.errors += 1
            if e.code == REQUEST_TIMEOUT:
                return f"error: the {live.name} connector didn't answer within {timeout:g} seconds", True
            if e.code == CONNECTION_CLOSED:
                live.client, live.state, live.error = None, "restarting", "the server stopped"
                live.restart = True
                live.wake.set()
                return f"error: the {live.name} connector isn't running right now", True
            return f"error: {e.message[:200]}", True
        except Exception as e:  # noqa: BLE001 - e.g. a result that fails its own output schema
            live.errors += 1
            return f"error: {_reason(e)}", True
        finally:
            live.last_ms = int((time.monotonic() - started) * 1000)
        text, is_error = to_tool_result(result, live.conf["max_result_chars"])
        live.errors += is_error
        # Say where it came from: to Claude this is data from outside, not instructions.
        return (text if is_error else f"[data from the {live.name} connector] {text}"), is_error

    # ---- helpers -----------------------------------------------------------

    def _publish(self) -> None:
        """Rebuild the tool list Claude sees. Tools stay listed while their
        server restarts, so the prompt cache isn't thrown away by a blip."""
        every = [t for live in self.live.values() for t in live.tools]
        kept, self.skipped = cap_total(every)
        with self._lock:
            self._tools = {t.api_name: t for t in kept}
            self._api = [t.api for t in kept]

    def _tried(self, live: _Live) -> None:
        live.tried = True
        if all(l.tried for l in self.live.values()):
            self._ready.set()

    def _errlog(self, name: str):
        if self.log_dir is None:
            return open(os.devnull, "w")
        self.log_dir.mkdir(parents=True, exist_ok=True)
        return open(self.log_dir / f"connector-{name}.log", "a", buffering=1)


async def _list_tools(client) -> list:
    tools, cursor = [], None
    while True:
        page = await client.list_tools(cursor=cursor)
        tools += page.tools
        cursor = page.next_cursor
        if not cursor:
            return tools


async def _resource_uri(client, name: str) -> str | None:
    try:
        listed = (await client.list_resources()).resources
    except Exception:  # noqa: BLE001 - a server without resources
        return None
    found = next((r for r in listed if r.name == name), None)
    return str(found.uri) if found else None


async def _vocabulary(client) -> list[str]:
    """The lines of the server's "vocabulary" resource, if it has one."""
    try:
        listed = (await client.list_resources()).resources
        found = next((r for r in listed if r.name == VOCABULARY_RESOURCE), None)
        if found is None:
            return []
        read = await client.read_resource(str(found.uri))
    except Exception:  # noqa: BLE001 - no resources, or not this one: no vocabulary
        return []
    lines = [line.strip() for part in read.contents for line in (getattr(part, "text", "") or "").splitlines()]
    return [line for line in lines if line and len(line) <= 60][:MAX_VOCABULARY]


def fit_hints(base: list[str], extra: list[str], limit: int = HINT_CHARS,
              skip: tuple[str, ...] = ("vizbot", "stackchan")) -> list[str]:
    """base, then as many extra words as fit in limit characters (joined with
    ", "). Words already there, or always in the prompt (skip), aren't repeated."""
    out = list(base)
    taken = {w.lower() for w in out} | set(skip)
    used = len(", ".join(out))
    for word in extra:
        if word.lower() in taken:
            continue
        if used + len(word) + 2 > limit:
            break
        out.append(word)
        taken.add(word.lower())
        used += len(word) + 2
    return out


def resolve_command(command: str, path: str) -> str:
    """The executable to start: ${VIZBRAIN_PYTHON} is vizbrain's own Python
    (handy for a one-file server); a bare name is looked up on the widened PATH."""
    if command == "${VIZBRAIN_PYTHON}":
        return sys.executable
    if os.path.isabs(command):
        if os.access(command, os.X_OK):
            return command
        raise ConnectorError(f"command not found: {command}")
    found = shutil.which(command, path=path)
    if not found:
        raise ConnectorError(f"command not found: {command} (looked in {path})")
    return found


def _reason(e: BaseException) -> str:
    """One readable line, digging into the exception groups anyio raises."""
    while isinstance(e, BaseExceptionGroup) and e.exceptions:
        e = e.exceptions[0]
    text = str(e) or type(e).__name__
    return text if isinstance(e, ConnectorError) else f"{type(e).__name__}: {text}"[:200]
