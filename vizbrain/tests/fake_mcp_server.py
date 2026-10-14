"""A tiny MCP server over stdio for the connector tests. Run it with vizbrain's Python."""

import os
import time

from mcp.server.mcpserver import MCPServer
from mcp.server.mcpserver.exceptions import ToolError

server = MCPServer("fake")


@server.tool(structured_output=False)
def echo(text: str = "hi") -> str:
    """Say the text back."""
    return f"echo: {text}"


@server.tool(structured_output=False)
def slow(seconds: float = 5) -> str:
    """Sleep, then answer."""
    time.sleep(seconds)
    return "finally"


@server.tool(structured_output=False)
def boom() -> str:
    """Fail on purpose."""
    raise ToolError("the fake server says no")


@server.tool(structured_output=False)
def pid() -> str:
    """This server's process id."""
    return str(os.getpid())


@server.tool(name="dotted.name", structured_output=False)
def dotted() -> str:
    """A name the Claude API would refuse as-is."""
    return "dotted ok"


if __name__ == "__main__":
    server.run("stdio")
