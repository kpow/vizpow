"""vizbrain command line.

  vizbrain serve [--port N]    run the brain (default)
  vizbrain set-key             store the Anthropic API key in the Keychain
  vizbrain mcp                 run the lab tools as an MCP server (stdio)
"""

from __future__ import annotations

import argparse
import getpass
import subprocess
import sys


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="vizbrain")
    sub = p.add_subparsers(dest="cmd")
    s = sub.add_parser("serve")
    s.add_argument("--port", type=int)
    k = sub.add_parser("set-key")
    k.add_argument("--clipboard", action="store_true", help="read the key from the clipboard")
    sub.add_parser("mcp")
    args = p.parse_args(argv)

    if args.cmd == "set-key":
        if args.clipboard:
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
        print(f"Saved {key[:13]}… ({len(key)} chars) to the Keychain (service vizbrain). Restart vizbrain to use it.")
        return 0
    if args.cmd == "mcp":
        from .mcp_server import run
        run()
        return 0

    from .server import serve
    serve(getattr(args, "port", None))
    return 0


if __name__ == "__main__":
    sys.exit(main())
