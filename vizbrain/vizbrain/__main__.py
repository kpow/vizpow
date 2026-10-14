"""vizbrain command line.

  vizbrain serve [--port N]    run the brain (default)
  vizbrain setup               first-run questions and model downloads
  vizbrain set-key             store the Anthropic API key in the Keychain
  vizbrain install-service     run at login from now on, and start now
  vizbrain restart | stop      restart or stop the login agent
  vizbrain doctor [--copy]     check everything and print a report to send
  vizbrain update              install the newest version
  vizbrain uninstall [--purge] remove vizbrain (--purge: its data too)
  vizbrain mcp                 run the lab tools as an MCP server (stdio)
"""

from __future__ import annotations

import argparse
import getpass
import subprocess
import sys

from . import __version__


def set_key(clipboard: bool) -> int:
    if clipboard:
        key = subprocess.run(["pbpaste"], capture_output=True, text=True).stdout.strip()
    else:
        key = getpass.getpass("Anthropic API key (input is hidden): ").strip()
    if not key.startswith("sk-ant-") or len(key) < 60:
        print(f"That isn't an API key (got {len(key)} characters). Create one at "
              "https://platform.claude.com/settings/keys and copy the full key it shows once "
              "(it starts with sk-ant-).")
        return 1
    subprocess.run(["security", "add-generic-password", "-U", "-s", "vizbrain",
                    "-a", "anthropic", "-w", key], check=True)
    print(f"Saved {key[:13]}… ({len(key)} chars) to the Keychain (service vizbrain).")
    from . import config, install
    if key.startswith("sk-ant-usr") and not ask_workspace(config):
        return 1
    if install.service_loaded():     # the key is read at startup
        install.restart()
    return 0


def ask_workspace(config) -> bool:
    """A personal key (sk-ant-usr...) isn't tied to one workspace, so every
    request must name one. Ask for it once and keep it in settings."""
    settings = config.load()
    if settings.get("workspace_id"):
        return True
    print("This is a personal key, so Claude needs to know which workspace to bill.\n"
          "Paste the workspace ID (it starts with wrkspc_; whoever gave you the key can send it).")
    try:
        ws = input("Workspace ID: ").strip()
    except EOFError:
        ws = ""
    if not ws.startswith("wrkspc_"):
        print("No workspace ID saved, so Claude will refuse this key. Run vizbrain set-key again to add it.")
        return False
    settings["workspace_id"] = ws
    config.save(settings)
    print("Saved the workspace ID.")
    return True


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="vizbrain")
    p.add_argument("--version", action="version", version=f"vizbrain {__version__}")
    sub = p.add_subparsers(dest="cmd")
    s = sub.add_parser("serve")
    s.add_argument("--port", type=int)
    st = sub.add_parser("setup")
    st.add_argument("--defaults", action="store_true", help="ask nothing; keep what is already set")
    k = sub.add_parser("set-key")
    k.add_argument("--clipboard", action="store_true", help="read the key from the clipboard")
    sub.add_parser("install-service")
    sub.add_parser("restart")
    sub.add_parser("stop")
    sub.add_parser("update")
    u = sub.add_parser("uninstall")
    u.add_argument("--purge", action="store_true", help="also delete settings, memory, the key and models")
    u.add_argument("--yes", action="store_true", help="don't ask before deleting")
    sub.add_parser("mcp")
    d = sub.add_parser("doctor")
    d.add_argument("--copy", action="store_true", help="also put the report on the clipboard")
    args = p.parse_args(argv)

    if args.cmd == "set-key":
        return set_key(args.clipboard)
    if args.cmd in ("setup", "install-service", "restart", "stop", "update", "uninstall"):
        from . import install
        if args.cmd == "setup":
            return install.setup(interactive=not args.defaults)
        if args.cmd == "uninstall":
            return install.uninstall(args.purge, args.yes)
        return {"install-service": install.install_service, "restart": install.restart,
                "stop": install.stop, "update": install.update}[args.cmd]()
    if args.cmd == "doctor":
        from . import doctor
        return doctor.run(args.copy)
    if args.cmd == "mcp":
        try:
            from .mcp_server import run
        except ImportError:
            print("The MCP server needs one extra package. In vizbrain's environment, run:\n"
                  "  pip install 'mcp>=2.0'", file=sys.stderr)
            return 1
        run()
        return 0

    from .server import serve
    serve(getattr(args, "port", None))
    return 0


if __name__ == "__main__":
    sys.exit(main())
