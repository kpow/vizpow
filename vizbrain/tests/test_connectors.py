"""What a connector's tools look like to Claude, and what their results become.
Run: python -m unittest discover -s tests"""

import json
import re
import sys
import unittest

from mcp import types as t

from vizbrain import connectors as c
from vizbrain.tools import Toolbox

from test_personal import toolbox

OBJ = {"type": "object", "properties": {}}


def tool(name, description="Look something up.", schema=OBJ, title=None):
    return t.Tool(name=name, description=description, input_schema=schema, title=title)


def conn(name="kpow", **kw):
    return {"name": name, "tools": None, "actions": [], **kw}


class Names(unittest.TestCase):
    def test_plain_names_get_the_connector_prefix(self):
        self.assertEqual(c.api_name("kpow", "recent_build_log"), "kpow__recent_build_log")

    def test_characters_the_api_refuses_are_replaced(self):
        self.assertEqual(c.api_name("site", "posts.latest/v2"), "site__posts_latest_v2")

    def test_long_names_are_cut_with_a_hash_and_stay_valid(self):
        name = c.api_name("kpow", "x" * 128)
        self.assertEqual(len(name), 64)
        self.assertRegex(name, r"^kpow__x+_[0-9a-f]{6}$")
        self.assertNotEqual(name, c.api_name("kpow", "x" * 127 + "y"))

    def test_a_clash_after_replacing_characters_gets_a_hash(self):
        offered, _ = c.catalog(conn(), [tool("a.b"), tool("a_b")])
        names = [o.api_name for o in offered]
        self.assertEqual(len(set(names)), 2)
        self.assertIn("kpow__a_b", names)

    def test_every_name_is_valid_for_the_api(self):
        offered, _ = c.catalog(conn(), [tool(n) for n in ("ok", "with space", "émoji✓", "a" * 200, "dots.and-dash")])
        for o in offered:
            self.assertRegex(o.api_name, r"^[a-zA-Z0-9_-]{1,64}$")

    def test_no_built_in_tool_uses_the_separator(self):
        for name in toolbox().tools:
            self.assertNotIn(c.SEPARATOR, name)


class Catalog(unittest.TestCase):
    def test_sorted_by_name_with_stable_schema_json(self):
        schema = {"type": "object", "required": ["q"], "properties": {"q": {"type": "string"}}}
        offered, skipped = c.catalog(conn(), [tool("zeta", schema=schema), tool("alpha")])
        self.assertEqual([o.api_name for o in offered], ["kpow__alpha", "kpow__zeta"])
        self.assertEqual(skipped, [])
        self.assertEqual(json.dumps(offered[1].api["input_schema"]),
                         '{"properties": {"q": {"type": "string"}}, "required": ["q"], "type": "object"}')

    def test_definition_matches_the_api_shape(self):
        (o,), _ = c.catalog(conn(), [tool("books", description="  Kevin's\n books.  ")])
        self.assertEqual(o.api, {"name": "kpow__books", "description": "Kevin's books.", "input_schema": OBJ})
        self.assertEqual((o.connector, o.original, o.reads), ("kpow", "books", True))

    def test_allowlist_keeps_only_the_listed_tools_and_names_missing_ones(self):
        offered, skipped = c.catalog(conn(tools=["books", "ghost"]), [tool("books"), tool("concerts")])
        self.assertEqual([o.original for o in offered], ["books"])
        self.assertEqual(skipped, ["kpow: ghost is in the tools list but the server doesn't offer it"])

    def test_actions_are_not_read_back(self):
        offered, _ = c.catalog(conn(actions=["lights_on"]), [tool("lights_on"), tool("status")])
        self.assertEqual({o.original: o.reads for o in offered}, {"lights_on": False, "status": True})

    def test_bad_schemas_are_skipped_with_a_reason(self):
        bad = [tool("list", schema={"type": "array"}), tool("union", schema={"type": "object", "anyOf": []}),
               tool("huge", schema={"type": "object", "description": "x" * 5000}), tool("good")]
        offered, skipped = c.catalog(conn(), bad)
        self.assertEqual([o.original for o in offered], ["good"])
        self.assertEqual(len(skipped), 3)
        self.assertIn("huge skipped, its input schema is too big", " ".join(skipped))

    def test_at_most_twelve_per_connector(self):
        offered, skipped = c.catalog(conn(), [tool(f"t{i:02}") for i in range(15)])
        self.assertEqual(len(offered), 12)
        self.assertEqual(skipped[0], "kpow: t12 skipped, over the 12-tool limit")

    def test_descriptions_are_cut_and_a_missing_one_is_made_up(self):
        (long, plain), _ = c.catalog(conn(), [tool("a", description="word " * 200), tool("b", description=None, title="Bee")])
        self.assertLessEqual(len(long.api["description"]), 400)
        self.assertTrue(long.api["description"].endswith("…"))
        self.assertEqual(plain.api["description"], "Bee (from the kpow connector)")

    def test_total_cap_across_connectors(self):
        a, _ = c.catalog(conn("a"), [tool(f"t{i:02}") for i in range(12)])
        b, _ = c.catalog(conn("b"), [tool(f"t{i:02}") for i in range(12)])
        z, _ = c.catalog(conn("z"), [tool("only")])
        kept, skipped = c.cap_total(z + b + a)
        self.assertEqual(len(kept), 24)
        self.assertEqual([k.api_name for k in kept], sorted(k.api_name for k in kept))
        self.assertEqual(skipped, ["z: only skipped, over the 24-tool total"])


class Results(unittest.TestCase):
    def result(self, *content, is_error=False, structured=None):
        return t.CallToolResult(content=list(content), is_error=is_error, structured_content=structured)

    def test_text_passes_through(self):
        r = self.result(t.TextContent(type="text", text="Oct 12: a battery meter."))
        self.assertEqual(c.to_tool_result(r, 6000), ("Oct 12: a battery meter.", False))

    def test_pictures_sound_and_files_are_named_not_sent(self):
        r = self.result(t.TextContent(type="text", text="Here:"),
                        t.ImageContent(type="image", data="AA==", mime_type="image/png"),
                        t.AudioContent(type="audio", data="AA==", mime_type="audio/wav"),
                        t.ResourceLink(type="resource_link", name="doc", uri="https://x/y"),
                        t.EmbeddedResource(type="resource", resource=t.TextResourceContents(uri="file:///a", text="inner")),
                        t.EmbeddedResource(type="resource", resource=t.BlobResourceContents(uri="file:///b", blob="AA==")))
        self.assertEqual(c.to_tool_result(r, 6000)[0],
                         "Here:\n[image omitted]\n[audio omitted]\n[link: doc https://x/y]\ninner\n[file omitted]")

    def test_structured_content_is_used_only_when_there_is_no_text(self):
        only = self.result(structured={"n": 1, "name": "Phish"})
        self.assertEqual(c.to_tool_result(only, 6000)[0], '{"n":1,"name":"Phish"}')
        both = self.result(t.TextContent(type="text", text="1 Phish"), structured={"n": 1})
        self.assertEqual(c.to_tool_result(both, 6000)[0], "1 Phish")

    def test_errors_are_marked(self):
        r = self.result(t.TextContent(type="text", text="No project called toaster."), is_error=True)
        self.assertEqual(c.to_tool_result(r, 6000), ("error: No project called toaster.", True))

    def test_long_results_are_cut_and_say_so(self):
        text, _ = c.to_tool_result(self.result(t.TextContent(type="text", text="x" * 7000)), 6000)
        self.assertEqual(text, "x" * 6000 + " [truncated, 1000 more characters]")

    def test_an_empty_result_still_says_something(self):
        self.assertEqual(c.to_tool_result(self.result(), 6000), ("(no result)", False))


class NoMcpImport(unittest.TestCase):
    def test_the_pure_half_does_not_need_the_mcp_library(self):
        src = (c.__file__)
        with open(src) as f:
            imports = [line for line in f if re.match(r"(from|import)\s+mcp\b", line)]
        self.assertEqual(imports, [], "connectors.py must import mcp lazily, inside the hub")


if __name__ == "__main__":
    unittest.main()
