"""`vizbrain connectors`: list, add, remove and set-secret, against a temporary
connectors.json. Run: python -m unittest discover -s tests"""

import contextlib
import io
import json
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from vizbrain import __main__ as cli
from vizbrain import config, connectors_cli, install

from test_hub import FAKE


class Cli(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "connectors.json"
        for p in (mock.patch.object(config, "CONNECTORS_PATH", self.path),
                  mock.patch.object(config, "SETTINGS_DIR", Path(self.tmp.name)),
                  mock.patch.object(install, "service_loaded", return_value=False),
                  mock.patch.object(connectors_cli, "_health", return_value=None)):
            p.start()
            self.addCleanup(p.stop)

    def run_cli(self, *argv) -> tuple[int, str]:
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = cli.main(["connectors", *argv])
        return code, out.getvalue()

    def servers(self) -> dict:
        return json.loads(self.path.read_text())["mcpServers"]

    def test_with_no_file_it_says_how_to_add_one(self):
        code, out = self.run_cli()
        self.assertEqual(code, 0)
        self.assertIn("No connectors. Add one with: vizbrain connectors add NAME -- COMMAND", out)

    def test_add_a_command_with_its_options(self):
        code, out = self.run_cli("add", "spotify", "--action", "play", "--action", "like", "--replaces", "music_play",
                                 "--timeout", "6", "--env", "TOKEN=keychain:spot", "--", "spotify-mcp", "--verbose")
        self.assertEqual(code, 0, out)
        self.assertEqual(self.servers()["spotify"], {
            "command": "spotify-mcp", "args": ["--verbose"], "timeout_s": 6.0, "actions": ["play", "like"],
            "replaces": ["music_play"], "env": {"TOKEN": "keychain:spot"}})
        self.assertEqual(stat.S_IMODE(os.stat(self.path).st_mode), 0o600)

    def test_add_keeps_the_others_and_list_shows_them(self):
        self.run_cli("add", "kpow", "--", "kpow-mcp")
        self.run_cli("add", "remote", "--url", "http://127.0.0.1:8765/mcp")
        self.assertEqual(sorted(self.servers()), ["kpow", "remote"])
        code, out = self.run_cli()
        self.assertIn("kpow: kpow-mcp", out)
        self.assertIn("remote: http://127.0.0.1:8765/mcp", out)
        self.assertIn("vizbrain isn't running", out)

    def test_bad_input_is_refused_and_writes_nothing(self):
        for argv in (["add", "Bad_Name", "--", "x"], ["add", "ok"], ["add", "ok", "--url", "http://a", "--", "x"],
                     ["add", "ok", "--url", "ftp://a"], ["add", "ok", "--env", "NOEQUALS", "--", "x"]):
            code, out = self.run_cli(*argv)
            self.assertEqual(code, 1, argv)
        self.assertFalse(self.path.exists())

    def test_remove(self):
        self.run_cli("add", "kpow", "--", "kpow-mcp")
        self.assertEqual(self.run_cli("remove", "kpow")[0], 0)
        self.assertEqual(self.servers(), {})
        self.assertEqual(self.run_cli("remove", "kpow")[0], 1)

    def test_adding_restarts_a_running_service(self):
        with mock.patch.object(install, "service_loaded", return_value=True), \
                mock.patch.object(install, "restart") as restart:
            self.run_cli("add", "kpow", "--", "kpow-mcp")
        restart.assert_called_once()

    def test_set_secret_stores_it_under_the_connector_account(self):
        with mock.patch.object(connectors_cli.getpass, "getpass", return_value="s3cret"), \
                mock.patch.object(connectors_cli.subprocess, "run") as run:
            code, out = self.run_cli("set-secret", "spot")
        self.assertEqual(code, 0)
        self.assertEqual(run.call_args.args[0],
                         ["security", "add-generic-password", "-U", "-s", "vizbrain", "-a", "connector/spot", "-w", "s3cret"])
        self.assertIn('"keychain:spot"', out)

    def test_secret_ids_are_found_for_uninstall(self):
        self.run_cli("add", "a", "--env", "T=keychain:one", "--env", "U=plain", "--", "x")
        self.run_cli("add", "b", "--env", "T=keychain:two", "--", "x")
        self.assertEqual(config.connector_secret_ids(), ["one", "two"])

    def test_test_starts_a_real_server_and_calls_a_tool(self):
        self.run_cli("add", "fake", "--", sys.executable, FAKE)
        code, out = self.run_cli("test", "fake", "--call", "echo", '{"text": "hi there"}')
        self.assertEqual(code, 0, out)
        self.assertIn("fake: ready after", out)
        self.assertIn("fake__echo: Say the text back.", out)
        self.assertIn("[data from the fake connector] echo: hi there", out)

    def test_test_of_an_unknown_name(self):
        code, out = self.run_cli("test", "ghost")
        self.assertEqual(code, 1)
        self.assertIn("No connector called ghost", out)


if __name__ == "__main__":
    unittest.main()
