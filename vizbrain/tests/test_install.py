"""Setup, downloads and the login agent (install.py), without touching the
real launchd, Keychain, settings or network. Run: python -m unittest discover -s tests"""

import hashlib
import http.server
import io
import json
import plistlib
import socket
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

from vizbrain import __main__ as cli
from vizbrain import bot, config, install, server


class Plist(unittest.TestCase):
    def test_agent_is_built_for_this_user_and_install(self):
        d = plistlib.loads(install.plist_bytes("/Users/dana/.local/share/uv/tools/vizbrain/bin/python",
                                               Path("/Users/dana/Library/Logs/vizbrain")))
        self.assertEqual(d["ProgramArguments"],
                         ["/Users/dana/.local/share/uv/tools/vizbrain/bin/python", "-m", "vizbrain", "serve"])
        self.assertEqual(d["StandardOutPath"], "/Users/dana/Library/Logs/vizbrain/vizbrain.log")
        self.assertEqual(d["KeepAlive"], {"SuccessfulExit": False})
        self.assertTrue(d["RunAtLoad"])
        self.assertNotIn("kpow", json.dumps(d).replace(install.LABEL, ""))
        self.assertNotIn("WorkingDirectory", d)

    def test_agent_names_its_app_so_macos_can_grant_it_the_network(self):
        d = plistlib.loads(install.plist_bytes("/x/python", Path("/x/logs")))
        self.assertEqual(d["AssociatedBundleIdentifiers"], ["com.kpow.vizbrain"])

    def test_app_bundle_is_written_signed_and_registered(self):
        with tempfile.TemporaryDirectory() as d, \
                mock.patch.object(install, "APP_PATH", Path(d) / "vizbrain.app"), \
                mock.patch.object(install.subprocess, "run") as run:
            self.assertTrue(install.make_app(4050))
            info = plistlib.loads((Path(d) / "vizbrain.app/Contents/Info.plist").read_bytes())
            launcher = Path(d) / "vizbrain.app/Contents/MacOS/vizbrain"
            self.assertEqual(info["CFBundleIdentifier"], "com.kpow.vizbrain")
            self.assertEqual(info["CFBundleExecutable"], "vizbrain")
            self.assertIn("vizBot", info["NSLocalNetworkUsageDescription"])
            self.assertIn("_http._tcp", info["NSBonjourServices"])
            self.assertTrue(info["LSUIElement"])
            self.assertIn("open http://localhost:4050", launcher.read_text())
            self.assertTrue(launcher.stat().st_mode & 0o111)
        commands = [c.args[0][:2] for c in run.call_args_list]
        self.assertIn(["codesign", "-s"], commands)
        self.assertIn([install.LSREGISTER, "-f"], commands)

    def test_default_agent_runs_the_interpreter_that_is_installed(self):
        import sys
        self.assertEqual(plistlib.loads(install.plist_bytes())["ProgramArguments"][0], sys.executable)


class Download(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        cls.payload = b"voice model bytes " * 5000
        (Path(cls.dir.name) / "model.bin").write_bytes(cls.payload)
        handler = lambda *a, **k: http.server.SimpleHTTPRequestHandler(*a, directory=cls.dir.name, **k)  # noqa: E731
        cls.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        cls.httpd.RequestHandlerClass.log_message = lambda *a: None
        threading.Thread(target=cls.httpd.serve_forever, daemon=True).start()
        cls.url = f"http://127.0.0.1:{cls.httpd.server_address[1]}/model.bin"
        cls.sha = hashlib.sha256(cls.payload).hexdigest()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        cls.dir.cleanup()

    def fetch(self, sha):
        out = Path(tempfile.mkdtemp()) / "kokoro" / "model.bin"
        with mock.patch("sys.stdout", io.StringIO()):
            install.download(self.url, out, sha, len(self.payload))
        return out

    def test_good_download_lands_whole(self):
        out = self.fetch(self.sha)
        self.assertEqual(out.read_bytes(), self.payload)
        self.assertTrue(install.file_ok(out, self.sha, len(self.payload)))
        self.assertEqual([p.name for p in out.parent.iterdir()], ["model.bin"])

    def test_damaged_download_is_rejected_and_leaves_nothing(self):
        with self.assertRaisesRegex(RuntimeError, "damaged"):
            self.fetch("0" * 64)

    def test_truncated_file_is_not_ok(self):
        out = self.fetch(self.sha)
        out.write_bytes(self.payload[:-1])
        self.assertFalse(install.file_ok(out, self.sha, len(self.payload)))
        self.assertFalse(install.file_ok(out.with_name("missing"), self.sha, 1))

    def test_voice_files_already_here_are_not_fetched_again(self):
        with mock.patch.object(install, "file_ok", return_value=True), \
                mock.patch.object(install, "download") as dl, mock.patch("sys.stdout", io.StringIO()):
            install.fetch_voice()
        dl.assert_not_called()


class Questions(unittest.TestCase):
    PLACES = {"results": [
        {"name": "Austin", "admin1": "Texas", "country_code": "US", "timezone": "America/Chicago",
         "latitude": 30.26715, "longitude": -97.74306},
        {"name": "Austin", "admin1": "Minnesota", "country_code": "US", "timezone": "America/Chicago",
         "latitude": 43.66663, "longitude": -92.97464}]}

    def test_geocode_reads_open_meteo(self):
        reply = mock.MagicMock()
        reply.__enter__.return_value = io.BytesIO(json.dumps(self.PLACES).encode())
        with mock.patch.object(install.urllib.request, "urlopen", return_value=reply) as u:
            places = install.geocode("Austin TX")
        self.assertIn("name=Austin%20TX", u.call_args[0][0])
        self.assertEqual(places[0], {"city": "Austin", "region": "Texas", "country": "US",
                                     "timezone": "America/Chicago", "lat": 30.2672, "lon": -97.7431})

    def run_setup(self, answers, settings=None, bots=(), loaded=False):
        """setup() with scripted answers; returns (exit code, saved settings, output)."""
        saved, out = {}, io.StringIO()
        answers = iter(answers)
        places = [dict(city=p["name"], region=p["admin1"], country="US", timezone=p["timezone"],
                       lat=round(p["latitude"], 4), lon=round(p["longitude"], 4))
                  for p in self.PLACES["results"]]
        self.location_sent = []
        fake_bot = mock.Mock()
        fake_bot.set_location.side_effect = lambda lat, lon: self.location_sent.append((lat, lon))
        with mock.patch.object(config, "load", return_value={**config.DEFAULTS, **(settings or {})}), \
                mock.patch.object(config, "save", side_effect=saved.update), \
                mock.patch.object(config, "user_name", side_effect=lambda s: s.get("user_name") or "Kevin"), \
                mock.patch.object(install, "ask", side_effect=lambda q, d="", i=True: next(answers, d) or d), \
                mock.patch.object(install, "geocode", return_value=places), \
                mock.patch.object(install, "discover_stackchans", return_value=list(bots)), \
                mock.patch.object(install, "Bot", return_value=fake_bot), \
                mock.patch.object(install, "fetch_voice"), mock.patch.object(install, "fetch_ears"), \
                mock.patch.object(install, "service_loaded", return_value=loaded), \
                mock.patch.object(install, "restart") as restart, mock.patch("sys.stdout", out):
            code = install.setup()
        self.restarted = restart.called
        return code, saved, out.getvalue()

    def test_name_city_and_bot(self):
        bots = [{"name": "vizBot-dana", "host": "192.168.1.80", "firmware": "3.7.31"}]
        code, saved, out = self.run_setup(["Dana", "Austin", "1"], bots=bots)
        self.assertEqual(code, 0)
        self.assertEqual(saved["user_name"], "Dana")
        self.assertEqual(saved["location"]["region"], "Texas")
        self.assertEqual(self.location_sent, [(30.2672, -97.7431)])
        self.assertIn("Found vizBot-dana at 192.168.1.80 (firmware 3.7.31)", out)
        self.assertFalse(self.restarted)

    def test_enter_through_everything_changes_nothing(self):
        code, saved, out = self.run_setup([])
        self.assertEqual(code, 0)
        self.assertEqual(saved["user_name"], "")
        self.assertEqual(saved["location"], {})
        self.assertIn("No vizBot found", out)

    def test_old_firmware_is_called_out(self):
        bots = [{"name": "vizBot-dana", "host": "192.168.1.80", "firmware": "3.7.13"}]
        _, _, out = self.run_setup([], bots=bots)
        self.assertIn("his firmware is 3.7.13 and this\n  brain needs 3.7.31 or newer", out)
        self.assertIn(install.FIRMWARE_PAGE, out)

    def test_running_copy_is_restarted_to_pick_up_settings(self):
        self.run_setup([], loaded=True)
        self.assertTrue(self.restarted)

    def test_failed_download_says_what_to_do(self):
        with mock.patch.object(install, "fetch_voice", side_effect=OSError("network is down")), \
                mock.patch.object(install, "discover_stackchans", return_value=[]), \
                mock.patch.object(config, "load", return_value=dict(config.DEFAULTS)), \
                mock.patch.object(config, "save"), mock.patch("sys.stdout", io.StringIO()) as out:
            code = install.setup(interactive=False)
        self.assertEqual(code, 1)
        self.assertIn("run: vizbrain setup", out.getvalue())


class Firmware(unittest.TestCase):
    def test_minimum(self):
        for v, ok in (("3.7.31", True), ("3.8.0", True), ("4.0.0", True), ("3.7.30", False),
                      ("3.7.13", False), (None, False), ("garbage", False)):
            self.assertEqual(bot.firmware_ok(v), ok, v)


class Port(unittest.TestCase):
    def test_free_port_is_served(self):
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            port = s.getsockname()[1]
        self.assertIsNone(server.port_holder(port))

    def test_port_a_restarted_copy_just_closed_counts_as_free(self):
        # The old copy answered someone, then was killed: its side of that
        # connection sits in TIME_WAIT with nothing listening. A restart must start.
        srv = socket.socket()
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("0.0.0.0", 0))
        port = srv.getsockname()[1]
        srv.listen()
        c = socket.create_connection(("127.0.0.1", port))
        a, _ = srv.accept()
        a.close(), c.close(), srv.close()
        self.assertIsNone(server.port_holder(port))

    def test_taken_port_gets_an_explanation_not_a_traceback(self):
        with socket.socket() as s:
            s.bind(("0.0.0.0", 0))
            s.listen()
            port = s.getsockname()[1]
            why = server.port_holder(port)
            self.assertRegex(why, r"already running \(pid \d+\)|in use by another app \(pid \d+")
            with mock.patch.object(config, "load", return_value={**config.DEFAULTS, "port": port}), \
                    mock.patch.object(server, "App") as app, mock.patch("sys.stdout", io.StringIO()) as out:
                server.serve()
            app.assert_not_called()          # nothing started: no announce, no Bonjour, no models
            self.assertIn(why, out.getvalue())


class Cli(unittest.TestCase):
    def test_version(self):
        with mock.patch("sys.stdout", io.StringIO()) as out, self.assertRaises(SystemExit):
            cli.main(["--version"])
        self.assertEqual(out.getvalue().strip(), f"vizbrain {cli.__version__}")

    def test_commands_reach_install(self):
        for argv, name, args in ((["setup", "--defaults"], "setup", {"interactive": False}),
                                 (["install-service"], "install_service", {}),
                                 (["restart"], "restart", {}), (["stop"], "stop", {})):
            with mock.patch.object(install, name, return_value=0) as fn:
                self.assertEqual(cli.main(argv), 0)
            fn.assert_called_once_with(**args)

    def update(self, link, reply=None, error=None):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "install-link"
            if link:
                path.write_text(link + "\n")
            page = mock.MagicMock()
            page.__enter__.return_value = io.BytesIO((reply or "").encode())
            with mock.patch.object(install, "LINK_PATH", path), \
                    mock.patch.object(install.urllib.request, "urlopen",
                                      side_effect=error, return_value=page) as fetch, \
                    mock.patch.object(install.os, "execvp") as run, \
                    mock.patch("sys.stdout", io.StringIO()) as out:
                code = install.update()
        return code, fetch, run, out.getvalue()

    def test_update_runs_the_installer_from_the_saved_link(self):
        script = "#!/bin/bash\n# vizbrain installer\n"
        _, fetch, run, _ = self.update("https://kpow.xyz/vb/abc123abc123abc123", script)
        self.assertEqual(fetch.call_args[0][0], "https://kpow.xyz/vb/abc123abc123abc123")
        run.assert_called_once_with("bash", ["bash", "-c", script, "vizbrain-install", "--update"])

    def test_update_without_a_link_says_who_to_ask(self):
        code, fetch, run, out = self.update("")
        self.assertEqual(code, 1)
        fetch.assert_not_called()
        run.assert_not_called()
        self.assertIn("Email Kevin for your install link", out)

    def test_update_with_a_dead_link_runs_nothing(self):
        dead = install.urllib.error.HTTPError("u", 404, "Not Found", {}, io.BytesIO(b""))
        code, _, run, out = self.update("https://kpow.xyz/vb/revoked", error=dead)
        self.assertEqual(code, 1)
        run.assert_not_called()
        self.assertIn("isn't active any more", out)

    def test_update_never_runs_a_web_page_as_a_script(self):
        code, _, run, out = self.update("https://kpow.xyz/vb/x", "<!doctype html><html>kpow</html>")
        self.assertEqual(code, 1)
        run.assert_not_called()

    def test_uninstall_without_purge_keeps_data_and_asks_nothing(self):
        with tempfile.TemporaryDirectory() as d, \
                mock.patch.object(install, "PLIST_PATH", Path(d) / "agent.plist"), \
                mock.patch.object(install, "APP_PATH", Path(d) / "vizbrain.app"), \
                mock.patch.object(install, "service_loaded", return_value=False), \
                mock.patch.object(install.shutil, "rmtree") as rmtree, \
                mock.patch.object(install.subprocess, "run") as run, \
                mock.patch.object(install.sys, "prefix", "/somewhere/venv"), \
                mock.patch("sys.stdout", io.StringIO()) as out:
            (Path(d) / "agent.plist").write_text("x")
            self.assertEqual(install.uninstall(purge=False), 0)
            self.assertFalse((Path(d) / "agent.plist").exists())
        # Only the app goes: settings, memory, logs and models stay, and the key isn't touched.
        rmtree.assert_called_once_with(Path(d) / "vizbrain.app", ignore_errors=True)
        self.assertNotIn("security", [c.args[0][0] for c in run.call_args_list])
        self.assertIn("Kept your settings", out.getvalue())

    def set_key(self, key, typed_workspace, settings=None):
        saved = {}
        with mock.patch.object(cli.getpass, "getpass", return_value=key), \
                mock.patch.object(cli.subprocess, "run") as run, \
                mock.patch("builtins.input", return_value=typed_workspace), \
                mock.patch.object(config, "load", return_value={**config.DEFAULTS, **(settings or {})}), \
                mock.patch.object(config, "save", side_effect=saved.update), \
                mock.patch.object(install, "service_loaded", return_value=False), \
                mock.patch("sys.stdout", io.StringIO()) as out:
            code = cli.set_key(clipboard=False)
        return code, saved, out.getvalue(), run

    def test_personal_key_asks_for_its_workspace(self):
        code, saved, out, run = self.set_key("sk-ant-usr01-" + "y" * 90, "wrkspc_01ABC")
        self.assertEqual(code, 0)
        self.assertEqual(saved["workspace_id"], "wrkspc_01ABC")
        self.assertIn("personal key", out)
        run.assert_called_once()                    # the key went to the Keychain

    def test_personal_key_without_a_workspace_says_what_will_happen(self):
        code, saved, out, _ = self.set_key("sk-ant-usr01-" + "y" * 90, "")
        self.assertEqual(code, 1)
        self.assertEqual(saved, {})
        self.assertIn("Claude will refuse this key", out)

    def test_personal_key_with_a_workspace_already_set_asks_nothing(self):
        code, saved, out, _ = self.set_key("sk-ant-usr01-" + "y" * 90, "unused", {"workspace_id": "wrkspc_1"})
        self.assertEqual((code, saved), (0, {}))
        self.assertNotIn("Workspace ID", out)

    def test_workspace_key_asks_nothing(self):
        code, saved, out, _ = self.set_key("sk-ant-api03-" + "z" * 95, "unused")
        self.assertEqual((code, saved), (0, {}))
        self.assertNotIn("workspace", out)

    def test_purge_needs_a_typed_yes(self):
        with mock.patch.object(install, "ask", return_value="no"), \
                mock.patch.object(install, "service_loaded") as loaded, \
                mock.patch.object(install.shutil, "rmtree") as rmtree, \
                mock.patch("sys.stdout", io.StringIO()):
            self.assertEqual(install.uninstall(purge=True), 1)
        rmtree.assert_not_called()
        loaded.assert_not_called()


if __name__ == "__main__":
    unittest.main()
