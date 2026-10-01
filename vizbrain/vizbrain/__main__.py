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
    sub.add_parser("set-key")
    sub.add_parser("mcp")
    args = p.parse_args(argv)

    if args.cmd == "set-key":
        key = getpass.getpass("Anthropic API key: ").strip()
        if not key:
            return 1
        subprocess.run(["security", "add-generic-password", "-U", "-s", "vizbrain",
                        "-a", "anthropic", "-w", key], check=True)
        print("Saved to the Keychain (service vizbrain). Restart vizbrain to use it.")
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
