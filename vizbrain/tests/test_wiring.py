"""Connectors inside vizbrain: the Toolbox offers and runs their tools, the
brain reads their results, and an install without connectors is untouched.
Run: python -m unittest discover -s tests"""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from vizbrain import brain, config, server
from vizbrain.connectors import Hub

from fakes import BrainTest, FakeClaude, make_brain, reply, text, tool_use
from test_hub import FAKE, conn
from test_personal import fake_vizmac, fake_wled, toolbox
from vizbrain import tools


class FakeHub:
    def __init__(self):
        self.calls = []

    def api_list(self):
        return [{"name": "kpow__books", "description": "Kevin's books.", "input_schema": {"type": "object"}},
                {"name": "kpow__lights", "description": "An action.", "input_schema": {"type": "object"}}]

    def owns(self, name):
        return name in ("kpow__books", "kpow__lights")

    def reads(self, name):
        return {"kpow__books": True, "kpow__lights": False}.get(name)

    def run(self, name, args, timeout=None):
        self.calls.append((name, args))
        return f"{name} ran", False


def box_with(hub):
    return tools.Toolbox(lambda: None, fake_wled(), fake_vizmac(),
                         memory=SimpleNamespace(prompt_text=lambda: ""),
                         routines=SimpleNamespace(prompt_text=lambda: ""), connectors=hub)


class ToolboxWithConnectors(unittest.TestCase):
    def test_connector_tools_come_after_the_built_ins(self):
        names = [t["name"] for t in box_with(FakeHub()).api_list()]
        self.assertEqual(names[-2:], ["kpow__books", "kpow__lights"])
        self.assertEqual(names[:-2], [t["name"] for t in toolbox().api_list()])

    def test_runs_go_to_the_hub_and_built_ins_stay_local(self):
        hub = FakeHub()
        box = box_with(hub)
        self.assertEqual(box.run("kpow__books", {"query": "dune"}), ("kpow__books ran", False))
        self.assertEqual(hub.calls, [("kpow__books", {"query": "dune"})])
        self.assertEqual(box.run("nope__x", {}), ("unknown tool nope__x", True))
        self.assertEqual(hub.calls, [("kpow__books", {"query": "dune"})])

    def test_reads_follows_the_connector_actions_list(self):
        box = box_with(FakeHub())
        self.assertTrue(box.reads("kpow__books"))
        self.assertFalse(box.reads("kpow__lights"))
        self.assertTrue(box.reads("get_weather"))
        self.assertFalse(box.reads("wled_set"))


class BrainWithConnectors(BrainTest):
    def test_a_connector_answer_is_read_back_after_the_filler(self):
        hub = FakeHub()
        claude = FakeClaude(reply(text("Let me check."), tool_use("kpow__books", {"query": "dune"})),
                            reply(text("You gave Dune five stars.")))
        result = make_brain(claude, box_with(hub)).turn("what did I think of dune?")
        self.assertEqual(len(claude.requests), 2)
        self.assertEqual(result["text"], "You gave Dune five stars.")
        self.assertIn("kpow__books", [t["name"] for t in claude.requests[0]["tools"]])

    def test_the_prompt_mentions_connectors_only_when_there_are_some(self):
        with_hub = make_brain(FakeClaude(), box_with(FakeHub()))
        without = make_brain(FakeClaude(), toolbox())
        self.assertIn("double underscore", with_hub.system_prompt)
        self.assertNotIn("double underscore", without.system_prompt)
        self.assertNotIn("{connectors}", without.system_prompt)


class AppSetup(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "connectors.json"
        patch = mock.patch.object(config, "CONNECTORS_PATH", self.path)
        patch.start()
        self.addCleanup(patch.stop)

    def test_no_connectors_file_means_no_hub_and_no_mcp_import(self):
        # A fresh interpreter, so other tests' imports don't count.
        code = ("import sys; from unittest import mock; from pathlib import Path; "
                "from vizbrain import config, tools; "
                f"config.CONNECTORS_PATH = Path({str(self.path)!r}); "
                "conns, problems = config.connectors(); "
                "box = tools.Toolbox(lambda: None, None, None, connectors=None); "
                "print(conns, problems, 'mcp' in sys.modules)")
        out = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True, timeout=60,
                             cwd=Path(__file__).parent.parent)
        self.assertEqual(out.stdout.strip(), "{} [] False", out.stderr[-500:])

    def test_listed_connectors_build_a_hub_that_the_toolbox_uses(self):
        self.path.write_text(json.dumps({"mcpServers": {"fake": {"command": sys.executable, "args": [FAKE]}},
                                         "extra": 1}))
        with mock.patch.object(server, "speech"), mock.patch.object(server, "WakeDetector"), \
                mock.patch.object(config, "pairing", return_value={"token": "a.b", "paired": False}), \
                mock.patch.object(config, "api_key", return_value=None), \
                mock.patch("sys.stdout"):
            app = server.App()
        self.assertIsInstance(app.connectors, Hub)
        self.assertIs(app.toolbox.connectors, app.connectors)
        self.assertEqual(app.connector_problems, [])
        self.assertIn("double underscore", app.brain.system_prompt)
        app.connectors.start()
        self.addCleanup(app.stop)
        self.assertTrue(app.connectors.wait_ready(30))
        self.assertEqual(app.toolbox.run("fake__echo", {"text": "via app"}),
                         ("[data from the fake connector] echo: via app", False))
        self.assertTrue(app.toolbox.reads("fake__echo"))


if __name__ == "__main__":
    unittest.main()
