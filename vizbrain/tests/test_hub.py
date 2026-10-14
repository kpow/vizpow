"""The connector hub with a real MCP server process: start, call, time out,
crash and come back, stop with nothing left running.
Run: python -m unittest discover -s tests"""

import contextlib
import io
import os
import signal
import sys
import tempfile
import time
import unittest
from pathlib import Path

from vizbrain import config
from vizbrain.connectors import Hub

FAKE = str(Path(__file__).parent / "fake_mcp_server.py")


def conn(name="fake", **kw):
    base = {"name": name, "command": sys.executable, "url": None, "args": [FAKE], "env": {}, "cwd": None,
            "tools": None, "actions": [], **config.CONNECTOR_DEFAULTS}
    return {**base, **kw}


def alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    return True


LABEL = "[data from the fake connector] "


def data(text: str) -> tuple[str, bool]:
    """A successful result as the hub returns it: labelled with where it came from."""
    return LABEL + text, False


def pid_of(hub) -> int:
    text, is_error = hub.run("fake__pid", {})
    assert not is_error, text
    return int(text.removeprefix(LABEL))


def until(check, timeout=15.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if check():
            return True
        time.sleep(0.05)
    return False


class HubTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        quiet = contextlib.redirect_stdout(io.StringIO())
        quiet.__enter__()
        self.addCleanup(quiet.__exit__, None, None, None)

    def hub(self, *conns, probe_every_s=None) -> Hub:
        hub = Hub({c["name"]: c for c in conns}, log_dir=Path(self.tmp.name))
        if probe_every_s:
            hub.probe_every_s = probe_every_s
        hub.start()
        self.addCleanup(hub.stop)
        self.assertTrue(hub.wait_ready(30), "connectors never finished starting")
        return hub

    def test_tools_are_listed_sorted_and_named_for_the_api(self):
        hub = self.hub(conn())
        self.assertEqual([t["name"] for t in hub.api_list()],
                         ["fake__boom", "fake__dotted_name", "fake__echo", "fake__pid", "fake__slow"])
        self.assertEqual(hub.status()[0]["state"], "ready")
        self.assertTrue(hub.reads("fake__echo"))
        self.assertIsNone(hub.reads("get_weather"))

    def test_a_call_returns_the_text(self):
        hub = self.hub(conn())
        self.assertEqual(hub.run("fake__echo", {"text": "yo"}), data("echo: yo"))
        self.assertEqual(hub.run("fake__dotted_name", {}), data("dotted ok"))
        self.assertEqual(hub.status()[0]["calls"], 2)

    def test_a_server_side_failure_comes_back_as_an_error_with_its_reason(self):
        text, is_error = self.hub(conn()).run("fake__boom", {})
        self.assertTrue(is_error)
        self.assertIn("the fake server says no", text)

    def test_a_slow_call_times_out_and_the_session_keeps_working(self):
        hub = self.hub(conn(timeout_s=1.0))
        started = time.monotonic()
        self.assertEqual(hub.run("fake__slow", {"seconds": 5}),
                         ("error: the fake connector didn't answer within 1 seconds", True))
        self.assertLess(time.monotonic() - started, 3)
        self.assertEqual(hub.run("fake__slow", {"seconds": 5}, timeout=0.5)[0],
                         "error: the fake connector didn't answer within 0.5 seconds")   # a caller can only shorten it
        self.assertEqual(hub.run("fake__echo", {}), data("echo: hi"))

    def test_a_crashed_server_is_restarted_and_its_tools_stay_listed(self):
        hub = self.hub(conn())
        first = pid_of(hub)
        os.kill(first, signal.SIGKILL)
        until(lambda: not alive(first), 5)
        self.assertEqual(hub.run("fake__echo", {}), ("error: the fake connector isn't running right now", True))
        self.assertEqual(hub.status()[0]["state"], "restarting")
        self.assertIn("fake__echo", [t["name"] for t in hub.api_list()])   # still offered while it restarts
        self.assertTrue(until(lambda: hub.status()[0]["state"] == "ready"), hub.status())
        second = pid_of(hub)
        self.assertNotEqual(first, second)
        self.assertTrue(alive(second))
        self.assertEqual(hub.run("fake__echo", {}), data("echo: hi"))

    def test_stop_leaves_no_server_running(self):
        hub = self.hub(conn())
        pid = pid_of(hub)
        hub.stop()
        self.assertTrue(until(lambda: not alive(pid), 6), f"server {pid} still running after stop")
        self.assertEqual(hub.status()[0]["state"], "stopped")

    def test_a_missing_command_is_reported_and_offers_nothing(self):
        hub = self.hub(conn("nope", command="no-such-mcp-server-anywhere", args=[]))
        status = hub.status()[0]
        self.assertIn(status["state"], ("restarting", "failed"))
        self.assertIn("command not found: no-such-mcp-server-anywhere", status["last_error"])
        self.assertEqual(hub.api_list(), [])
        self.assertEqual(hub.run("nope__x", {}), ("error: unknown tool nope__x", True))

    def test_one_broken_connector_does_not_hold_up_the_other(self):
        hub = self.hub(conn("nope", command="no-such-mcp-server-anywhere"), conn("fake"))
        self.assertEqual(hub.run("fake__echo", {}), data("echo: hi"))

    def test_allowlist_and_actions_apply(self):
        hub = self.hub(conn(tools=["echo", "pid"], actions=["pid"]))
        self.assertEqual([t["name"] for t in hub.api_list()], ["fake__echo", "fake__pid"])
        self.assertFalse(hub.reads("fake__pid"))

    def test_server_stderr_goes_to_its_own_log(self):
        self.hub(conn())
        self.assertTrue((Path(self.tmp.name) / "connector-fake.log").exists())

    def test_vocabulary_is_read_at_start(self):
        hub = self.hub(conn())
        self.assertEqual(hub.vocabulary(), ["Soul Siblings", "Maeby", "Noodle 2K"])
        self.assertEqual(hub.status()[0]["vocabulary"], 3)

    def test_vocabulary_follows_the_server_as_its_data_changes(self):
        words = Path(self.tmp.name) / "vocab.txt"
        words.write_text("Alpha\nBeta\n")
        hub = self.hub(conn(env={"FAKE_VOCAB_FILE": str(words)}), probe_every_s=0.2)
        self.assertEqual(hub.vocabulary(), ["Alpha", "Beta"])
        words.write_text("Gamma\n")
        self.assertTrue(until(lambda: hub.vocabulary() == ["Gamma"], 10), hub.vocabulary())

    def test_vocabularies_merge_without_repeats(self):
        words = Path(self.tmp.name) / "vocab.txt"
        words.write_text("maeby\nPhish\n")
        hub = self.hub(conn("one"), conn("two", env={"FAKE_VOCAB_FILE": str(words)}))
        self.assertEqual(hub.vocabulary(), ["Soul Siblings", "Maeby", "Noodle 2K", "Phish"])

    def test_an_empty_hub_is_ready_at_once_and_stops_cleanly(self):
        hub = Hub({})
        hub.start()
        self.assertTrue(hub.wait_ready(0))
        self.assertEqual(hub.api_list(), [])
        hub.stop()


if __name__ == "__main__":
    unittest.main()
