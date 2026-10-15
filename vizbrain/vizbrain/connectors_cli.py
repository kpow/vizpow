"""`vizbrain connectors`: see, test, add and remove the MCP servers this brain uses.

  vizbrain connectors                         list them, with live state if the service is running
  vizbrain connectors test [NAME] [--call TOOL JSON]
                                              start them here, list their tools, optionally call one
  vizbrain connectors add NAME -- CMD [ARGS...]
  vizbrain connectors add NAME --url URL
        [--action TOOL]... [--replaces TOOL]... [--timeout S] [--env KEY=VALUE]...
  vizbrain connectors remove NAME
  vizbrain connectors set-secret ID           store a secret; use "keychain:ID" as an env value

Adding or removing one restarts the service, which reads the file at startup.
"""

from __future__ import annotations

import argparse
import getpass
import json
import subprocess
import time
import urllib.request

from . import config


def add_parser(sub) -> None:
    c = sub.add_parser("connectors", help="MCP servers whose tools vizBot can use")
    cs = c.add_subparsers(dest="connectors_cmd")
    t = cs.add_parser("test", help="start connectors here and list their tools")
    t.add_argument("name", nargs="?")
    t.add_argument("--call", nargs=2, metavar=("TOOL", "JSON"), help="also call one tool, e.g. --call now_playing '{}'")
    a = cs.add_parser("add", help="add or replace a connector")
    a.add_argument("name")
    a.add_argument("--url", help="an HTTP MCP server instead of a command")
    a.add_argument("--action", dest="actions", action="append", default=[], help="a tool that changes things (repeatable)")
    a.add_argument("--replaces", action="append", default=[], help="a built-in tool it supersedes (repeatable)")
    a.add_argument("--timeout", type=float, help="seconds a call may take (default 8)")
    a.add_argument("--env", action="append", default=[], metavar="KEY=VALUE",
                   help='environment for the server; VALUE "keychain:ID" reads a set-secret')
    a.add_argument("command", nargs="*", help="-- then the command and its arguments")
    r = cs.add_parser("remove", help="remove a connector")
    r.add_argument("name")
    s = cs.add_parser("set-secret", help="store a secret in the Keychain for a connector's env")
    s.add_argument("id")


def run(args) -> int:
    action = args.connectors_cmd or "list"
    return {"list": cmd_list, "test": cmd_test, "add": cmd_add, "remove": cmd_remove,
            "set-secret": cmd_set_secret}[action](args)


# ---- list --------------------------------------------------------------------

def cmd_list(args) -> int:
    conns, problems = config.connectors()
    try:
        disabled = [n for n, e in config.connectors_raw()["mcpServers"].items()
                    if isinstance(e, dict) and (e.get("enabled") is False or e.get("disabled") is True)]
    except (OSError, ValueError):
        disabled = []
    live = {c["name"]: c for c in (_health() or {}).get("connectors", [])}
    if not conns and not disabled and not problems:
        print(f"No connectors. Add one with: vizbrain connectors add NAME -- COMMAND\n(file: {config.CONNECTORS_PATH})")
        return 0
    for name, c in conns.items():
        what = c["url"] or " ".join([c["command"], *c["args"]])
        print(f"{name}: {what}")
        if c["actions"]:
            print(f"    actions: {', '.join(c['actions'])}")
        if c["replaces"]:
            print(f"    replaces: {', '.join(c['replaces'])}")
        l = live.get(name)
        if l:
            line = f"    running: {l['state']}, {l['tools']} tools, {l.get('vocabulary', 0)} names, {l['calls']} calls, {l['errors']} errors"
            print(line + (f", last error: {l['last_error']}" if l.get("last_error") else ""))
        elif live:
            print("    running: not started (restart vizbrain to pick it up)")
    for name in disabled:
        print(f"{name}: switched off")
    for p in problems:
        print(f"problem: {p}")
    if not live:
        print("(vizbrain isn't running, so there's no live state; `vizbrain connectors test` starts them here)")
    return 1 if problems else 0


def _health() -> dict | None:
    port = config.load()["port"]
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/v1/health", timeout=2) as r:
            return json.loads(r.read())
    except (OSError, ValueError):
        return None


# ---- test --------------------------------------------------------------------

def cmd_test(args) -> int:
    from .connectors import Hub
    conns, problems = config.connectors()
    for p in problems:
        print(f"problem: {p}")
    if args.name:
        if args.name not in conns:
            print(f"No connector called {args.name}. Listed: {', '.join(conns) or 'none'}")
            return 1
        conns = {args.name: conns[args.name]}
    if not conns:
        print("No connectors to test.")
        return 1
    hub = Hub(conns, log_dir=None)
    started = time.monotonic()
    hub.start()
    hub.wait_ready(max(c["start_timeout_s"] for c in conns.values()) + 5)
    ok = True
    try:
        for s in hub.status():
            print(f"{s['name']}: {s['state']} after {time.monotonic() - started:.1f} s, "
                  f"{s['tools']} tools, {s.get('vocabulary', 0)} names"
                  + (f" ({s['last_error']})" if s["last_error"] else ""))
            ok = ok and s["state"] == "ready"
            for line in s["skipped"]:
                print(f"    skipped: {line}")
        for t in hub.api_list():
            print(f"    {t['name']}: {t['description'][:90]}")
        for line in hub.skipped:
            print(f"    skipped: {line}")
        if args.call and ok:
            tool, raw = args.call
            name = tool if "__" in tool else f"{next(iter(conns))}__{tool}"
            t0 = time.monotonic()
            text, is_error = hub.run(name, json.loads(raw))
            print(f"\n{name} -> {'ERROR ' if is_error else ''}({int((time.monotonic() - t0) * 1000)} ms)\n{text}")
            ok = ok and not is_error
    finally:
        hub.stop()
    return 0 if ok else 1


# ---- add / remove / set-secret ------------------------------------------------

def cmd_add(args) -> int:
    if not config.CONNECTOR_NAME.match(args.name):
        print("A connector name is lowercase letters, digits and dashes, up to 16 (no underscores).")
        return 1
    command = [c for c in args.command if c != "--"]
    if bool(command) == bool(args.url):
        print("Give the server as `-- COMMAND [ARGS...]` or as --url URL (one of them).")
        return 1
    entry: dict = {"url": args.url} if args.url else {"command": command[0], "args": command[1:]}
    if args.timeout:
        entry["timeout_s"] = args.timeout
    if args.actions:
        entry["actions"] = args.actions
    if args.replaces:
        entry["replaces"] = args.replaces
    env = {}
    for kv in args.env:
        if "=" not in kv:
            print(f"--env wants KEY=VALUE, got {kv!r}")
            return 1
        k, v = kv.split("=", 1)
        env[k] = v
    if env:
        entry["env"] = env
    try:
        config._connector(args.name, entry)          # same checks as at startup
        raw = config.connectors_raw()
    except ValueError as e:
        print(f"Not added: {e}")
        return 1
    replaced = args.name in raw["mcpServers"]
    raw["mcpServers"][args.name] = entry
    config.save_connectors(raw)
    print(f"{'Updated' if replaced else 'Added'} {args.name}. Check it with: vizbrain connectors test {args.name}")
    return _restart()


def cmd_remove(args) -> int:
    try:
        raw = config.connectors_raw()
    except ValueError as e:
        print(e)
        return 1
    if args.name not in raw["mcpServers"]:
        print(f"No connector called {args.name}.")
        return 1
    del raw["mcpServers"][args.name]
    config.save_connectors(raw)
    print(f"Removed {args.name}.")
    return _restart()


def cmd_set_secret(args) -> int:
    value = getpass.getpass(f"Secret for {args.id} (input is hidden): ").strip()
    if not value:
        print("Nothing saved.")
        return 1
    subprocess.run(["security", "add-generic-password", "-U", "-s", "vizbrain", "-a", f"connector/{args.id}",
                    "-w", value], check=True, capture_output=True)
    print(f'Saved. Use it in a connector\'s env as "keychain:{args.id}", e.g.\n'
          f"  vizbrain connectors add NAME --env TOKEN=keychain:{args.id} -- COMMAND")
    return 0


def _restart() -> int:
    from . import install
    if install.service_loaded():
        install.restart()
        print("Restarted vizbrain to pick it up.")
    return 0
