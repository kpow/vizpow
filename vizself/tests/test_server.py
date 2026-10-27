"""The server through the real MCP protocol (in-process client)."""

import asyncio
import unittest

from mcp import Client

from vizself.index import Index
from vizself.server import build
from vizself.source import Repo

from .helpers import make_repo

TOOLS = ["hardware", "open_work", "read_code", "recent_changes", "search_code", "self_map"]


def text(result) -> str:
    return "\n".join(c.text for c in result.content if getattr(c, "type", "") == "text")


class ServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ix = Index.build(Repo(make_repo()))

    def call(self, name, args=None):
        async def go():
            async with Client(build(self.ix)) as client:
                return await client.call_tool(name, args or {})
        return asyncio.run(go())

    def test_lists_every_tool_with_a_description_and_object_schema(self):
        async def go():
            async with Client(build(self.ix)) as client:
                return (await client.list_tools()).tools
        tools = asyncio.run(go())
        self.assertEqual(sorted(t.name for t in tools), TOOLS)
        for t in tools:
            self.assertGreater(len(t.description), 40, t.name)
            self.assertLessEqual(len(t.description), 400, t.name)   # vizbrain cuts descriptions at 400
            self.assertIsNone(t.output_schema, t.name)

    def test_search_shows_file_purpose_and_numbered_code(self):
        r = self.call("search_code", {"query": "blink"})
        self.assertFalse(r.is_error)
        out = text(r)
        self.assertIn("firmware 1.1.0", out)
        self.assertIn("vizbot/bot_eyes.h, lines", out)
        self.assertIn("Draws the eyes and makes them blink.", out)
        self.assertRegex(out, r"\n\s+5  void updateBlink")

    def test_read_code_by_file_name(self):
        out = text(self.call("read_code", {"path": "bot_eyes.h", "start": 4, "lines": 2}))
        self.assertTrue(out.startswith("vizbot/bot_eyes.h, lines 4-5 of"))
        self.assertIn("close fast, open slow", out)

    def test_unknown_file_is_a_tool_error(self):
        r = self.call("read_code", {"path": "toaster.h"})
        self.assertTrue(r.is_error)
        self.assertIn("No file called toaster.h", text(r))

    def test_self_map_overview_and_topic(self):
        self.assertIn("A small desk robot", text(self.call("self_map")))
        self.assertIn("Version and pins", text(self.call("self_map", {"topic": "config.h"})))
        self.assertIn("trust the code", text(self.call("self_map", {"topic": "config.h"})))

    def test_hardware_defaults_to_stackchan(self):
        self.assertIn("SCS0009", text(self.call("hardware")))

    def test_recent_changes_stop_at_the_release(self):
        out = text(self.call("recent_changes", {"limit": 5}))
        self.assertIn("chore: version 1.1.0", out)
        self.assertNotIn("wip", out)


if __name__ == "__main__":
    unittest.main()
