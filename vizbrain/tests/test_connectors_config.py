"""connectors.json: what each install lists, checked before anything is started.
Run: python -m unittest discover -s tests"""

import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from vizbrain import config


class Load(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "connectors.json"
        patch = mock.patch.object(config, "CONNECTORS_PATH", self.path)
        patch.start()
        self.addCleanup(patch.stop)

    def write(self, data) -> None:
        self.path.write_text(data if isinstance(data, str) else json.dumps(data))

    def servers(self, **entries):
        self.write({"mcpServers": entries})
        return config.connectors()

    def test_no_file_is_no_connectors_and_no_problems(self):
        self.assertEqual(config.connectors(), ({}, []))

    def test_unreadable_file_is_reported_not_fatal(self):
        self.write("{nope")
        conns, problems = config.connectors()
        self.assertEqual(conns, {})
        self.assertIn("connectors.json is unreadable", problems[0])

    def test_wrong_shape_is_reported(self):
        self.write({"servers": {}})
        self.assertEqual(config.connectors(), ({}, ['connectors.json needs an "mcpServers" object']))

    def test_a_command_entry_is_filled_in_with_defaults(self):
        conns, problems = self.servers(kpow={"command": "kpow-mcp"})
        self.assertEqual(problems, [])
        self.assertEqual(conns["kpow"], {
            "name": "kpow", "command": "kpow-mcp", "url": None, "args": [], "env": {}, "cwd": None,
            "tools": None, "actions": [], "timeout_s": 8.0, "start_timeout_s": 30.0, "max_result_chars": 6000})

    def test_a_url_entry_is_accepted(self):
        conns, _ = self.servers(remote={"url": "http://127.0.0.1:8765/mcp"})
        self.assertEqual(conns["remote"]["url"], "http://127.0.0.1:8765/mcp")
        self.assertIsNone(conns["remote"]["command"])

    def test_disabled_entries_are_skipped_quietly(self):
        conns, problems = self.servers(a={"command": "x", "enabled": False}, b={"command": "x", "disabled": True},
                                       c={"command": "x"})
        self.assertEqual(list(conns), ["c"])
        self.assertEqual(problems, [])

    def test_bad_names_are_skipped_with_a_reason(self):
        conns, problems = self.servers(**{"Kpow": {"command": "x"}, "my_site": {"command": "x"},
                                          "way-too-long-a-name": {"command": "x"}, "ok-1": {"command": "x"}})
        self.assertEqual(list(conns), ["ok-1"])
        self.assertEqual(len(problems), 3)
        self.assertTrue(all("lowercase letters, digits and dashes" in p for p in problems))

    def test_one_broken_entry_does_not_take_the_others_down(self):
        conns, problems = self.servers(
            both={"command": "x", "url": "http://a"}, neither={}, args={"command": "x", "args": "--flag"},
            env={"command": "x", "env": {"A": 1}}, tools={"command": "x", "tools": "all"},
            url={"url": "ftp://a"}, timeout={"command": "x", "timeout_s": "soon"}, fine={"command": "x"})
        self.assertEqual(list(conns), ["fine"])
        self.assertEqual(len(problems), 7)
        self.assertIn('connector both: needs a "command"', problems[0])

    def test_numbers_are_clamped_to_sane_limits(self):
        conns, _ = self.servers(fast={"command": "x", "timeout_s": 0.1, "max_result_chars": 10 ** 9},
                                slow={"command": "x", "timeout_s": 600, "start_timeout_s": 1})
        self.assertEqual((conns["fast"]["timeout_s"], conns["fast"]["max_result_chars"]), (1.0, 20000))
        self.assertEqual((conns["slow"]["timeout_s"], conns["slow"]["start_timeout_s"]), (20.0, 5.0))

    def test_settings_save_leaves_connectors_alone(self):
        self.servers(kpow={"command": "kpow-mcp"})
        with mock.patch.object(config, "SETTINGS_DIR", Path(self.tmp.name)), \
                mock.patch.object(config, "SETTINGS_PATH", Path(self.tmp.name) / "settings.json"):
            config.save({**config.DEFAULTS, "connectors": {"x": 1}, "user_name": "Dana"})
        self.assertEqual(list(config.connectors()[0]), ["kpow"])
        saved = json.loads((Path(self.tmp.name) / "settings.json").read_text())
        self.assertEqual(saved, {"user_name": "Dana"})


class Env(unittest.TestCase):
    def conn(self, **env):
        return {"name": "kpow", "env": env}

    def test_path_is_widened_for_launchd(self):
        env, problems = config.connector_env(self.conn())
        dirs = env["PATH"].split(":")
        self.assertIn(str(Path.home() / ".local" / "bin"), dirs)
        self.assertIn("/opt/homebrew/bin", dirs)
        self.assertEqual(dirs[-4:], ["/usr/bin", "/bin", "/usr/sbin", "/sbin"])
        self.assertEqual(len(dirs), len(set(dirs)))
        self.assertEqual(problems, [])

    def test_a_connector_can_set_its_own_path_and_values(self):
        env, _ = config.connector_env(self.conn(PATH="/only/here", SITE="https://kpow.xyz"))
        self.assertEqual(env, {"PATH": "/only/here", "SITE": "https://kpow.xyz"})

    def test_keychain_values_are_read_from_the_connector_account(self):
        calls = []

        def run(cmd, **kw):
            calls.append(cmd)
            return SimpleNamespace(returncode=0, stdout="s3cret\n")

        with mock.patch("subprocess.run", run):
            env, problems = config.connector_env(self.conn(TOKEN="keychain:kpow-token"))
        self.assertEqual(env["TOKEN"], "s3cret")
        self.assertEqual(problems, [])
        self.assertEqual(calls[0][:6], ["security", "find-generic-password", "-s", "vizbrain", "-a", "connector/kpow-token"])

    def test_a_missing_keychain_item_is_a_problem_and_the_value_is_left_out(self):
        with mock.patch("subprocess.run", lambda cmd, **kw: SimpleNamespace(returncode=44, stdout="")):
            env, problems = config.connector_env(self.conn(TOKEN="keychain:nope"))
        self.assertNotIn("TOKEN", env)
        self.assertIn("no Keychain item for TOKEN", problems[0])

    def test_api_key_still_comes_from_the_anthropic_account(self):
        seen = []
        with mock.patch.dict("os.environ", {}, clear=True), \
                mock.patch("subprocess.run", lambda cmd, **kw: seen.append(cmd) or SimpleNamespace(returncode=0, stdout="sk-ant-x\n")):
            self.assertEqual(config.api_key(), "sk-ant-x")
        self.assertIn("anthropic", seen[0])


if __name__ == "__main__":
    unittest.main()
