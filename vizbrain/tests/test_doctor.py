"""`vizbrain doctor`: each check, with the Mac, launchd, Claude and the bot
stood in. Run: python -m unittest discover -s tests"""

import io
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import anthropic

from vizbrain import __main__ as cli
from vizbrain import config, doctor, install

KEY = "sk-ant-api03-" + "x" * 95
HEALTHY = {"ok": True, "version": doctor.__version__, "uptime_s": 7300, "key": "valid", "ears": "ready",
           "bot": "192.168.1.80", "bot_seen_s_ago": 20, "bot_error": "", "capped": None, "last_error": None}
STATE = {"boardType": "stackchan", "firmwareVersion": "3.7.31", "deviceName": "vizBot-dana"}
STATUS = {"enabled": True, "brainHost": "192.168.1.57:4050", "pairId": "ab12cd34", "turns": 41,
          "failures": 0, "lastError": ""}


def fake_bot(state=STATE, status=STATUS, down=False):
    def get(path, *a, **k):
        if down:
            raise doctor.BotError(f"{path}: timed out")
        return status
    def st():
        if down:
            raise doctor.BotError("/state: timed out")
        return state
    return mock.Mock(_get=get, state=st)


class Checks(unittest.TestCase):
    def report(self, fn, *args, **patches):
        r = doctor.Report()
        with mock.patch.multiple(doctor, **patches) if patches else mock.MagicMock():
            out = fn(r, *args)
        return r, "\n".join(r.lines), out

    # ---- key ----
    def key(self, retrieve=None, key=KEY):
        client = SimpleNamespace(models=SimpleNamespace(retrieve=retrieve or (lambda m: None)))
        with mock.patch.object(config, "api_key", return_value=key), \
                mock.patch("vizbrain.brain.make_client", return_value=client), \
                mock.patch.dict(doctor.os.environ, {}, clear=False):
            doctor.os.environ.pop("ANTHROPIC_API_KEY", None)
            r = doctor.Report()
            got = doctor.check_key(r, dict(config.DEFAULTS), True)
        return r, "\n".join(r.lines), got

    def test_good_key_is_reported_without_showing_it(self):
        r, text, got = self.key()
        self.assertEqual((r.problems, got), (0, KEY))
        self.assertIn("Claude accepted the key sk-ant-… (108 characters, from the Keychain)", text)
        self.assertNotIn(KEY[8:], text)

    def test_no_key(self):
        r, text, got = self.key(key=None)
        self.assertEqual(r.problems, 1)
        self.assertIn("Run: vizbrain set-key", text)

    def test_rejected_key_says_who_to_ask(self):
        def nope(model):
            resp = SimpleNamespace(request=None, status_code=401, headers={})
            raise anthropic.AuthenticationError("invalid x-api-key", response=resp, body=None)
        r, text, _ = self.key(nope)
        self.assertEqual(r.problems, 1)
        self.assertIn("Claude rejected the key", text)
        self.assertIn("Email Kevin for a working key", text)

    # ---- the running copy ----
    def running(self, health, key=KEY, loaded=True, holder=None):
        with mock.patch.object(install, "health", return_value=health), \
                mock.patch.object(install, "service_loaded", return_value=loaded), \
                mock.patch("vizbrain.server.port_holder", return_value=holder):
            r = doctor.Report()
            doctor.check_running(r, dict(config.DEFAULTS), key)
        return r, "\n".join(r.lines)

    def test_healthy(self):
        r, text = self.running(HEALTHY)
        self.assertEqual((r.problems, r.warnings), (0, 0))
        self.assertIn(f"vizbrain {doctor.__version__} is running (up 2 h)", text)

    def test_not_running(self):
        r, text = self.running(None, loaded=False)
        self.assertIn("vizbrain isn't running", text)
        self.assertIn("Run: vizbrain install-service", text)
        self.assertIn("Run: vizbrain restart", self.running(None, loaded=True)[1])

    def test_port_taken_by_something_else(self):
        r, text = self.running(None, holder="Port 4050 is in use by another app (pid 9: node).")
        self.assertIn("in use by another app", text)

    def test_started_before_the_key_was_saved(self):
        r, text = self.running({**HEALTHY, "key": "none"})
        self.assertEqual(r.problems, 1)
        self.assertIn("started before the key was saved", text)

    def test_states_a_friend_can_hit(self):
        self.assertIn("out of credit", self.running({**HEALTHY, "key": "no_credit"})[1])
        self.assertIn("still loading his ears", self.running({**HEALTHY, "ears": "loading"})[1])
        self.assertIn("spending limit for the day", self.running({**HEALTHY, "capped": "day"})[1])
        old = {k: v for k, v in HEALTHY.items() if k != "version"}
        self.assertIn("An older vizbrain is running", self.running(old)[1])

    # ---- the bot ----
    def bot(self, health=HEALTHY, my_ip="192.168.1.57", bots=(), pair_id="ab12cd34", **bot_kw):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "pairing.json"
            path.write_text(json.dumps({"token": f"{pair_id}.secret", "paired": True}))
            with mock.patch.object(doctor, "Bot", return_value=fake_bot(**bot_kw)), \
                    mock.patch.object(doctor, "discover_stackchans", return_value=list(bots)), \
                    mock.patch.object(config, "PAIRING_PATH", path):
                r = doctor.Report()
                doctor.check_bot(r, dict(config.DEFAULTS), health, my_ip)
        return r, "\n".join(r.lines)

    def test_everything_right(self):
        r, text = self.bot()
        self.assertEqual((r.problems, r.warnings), (0, 0), text)
        self.assertIn("Found vizBot-dana at 192.168.1.80, firmware 3.7.31", text)
        self.assertIn("pointed at this Mac (192.168.1.57:4050): 41 turns, 0 failed", text)
        self.assertIn("paired with this brain", text)
        self.assertNotIn("secret", text)

    def test_no_bot_anywhere(self):
        r, text = self.bot(health=None)
        self.assertEqual(r.problems, 1)
        self.assertIn("No vizBot found on this network", text)

    def test_old_firmware(self):
        r, text = self.bot(state={**STATE, "firmwareVersion": "3.7.13"})
        self.assertIn("his firmware is 3.7.13 and this brain needs 3.7.31 or newer", text)
        self.assertIn(install.FIRMWARE_PAGE, text)

    def test_mac_changed_address(self):
        r, text = self.bot(my_ip="192.168.1.99")
        self.assertEqual(r.problems, 1)
        self.assertIn("looking for his brain at 192.168.1.57:4050, but this Mac is 192.168.1.99:4050", text)

    def test_background_copy_blocked_from_the_network(self):
        r, text = self.bot(health={**HEALTHY, "bot_seen_s_ago": None, "bot_error": "[Errno 65] No route to host"})
        self.assertEqual(r.problems, 1)
        self.assertIn("Terminal can reach vizBot, but the running vizbrain can't ([Errno 65] No route to host)", text)
        self.assertIn("Local Network", text)

    def test_just_started_is_not_called_blocked(self):
        r, text = self.bot(health={**HEALTHY, "uptime_s": 30, "bot_seen_s_ago": None})
        self.assertEqual(r.problems, 0, text)

    def test_missed_tap_is_not_an_error(self):
        r, text = self.bot(status={**STATUS, "lastError": "no speech detected"})
        self.assertEqual(r.warnings, 0, text)
        self.assertIn("brain POST failed", self.bot(status={**STATUS, "lastError": "brain POST failed (status -1)"})[1])

    def test_bot_switched_off(self):
        r, text = self.bot(down=True)
        self.assertEqual(r.problems, 1)
        self.assertIn("isn't answering", text)

    # ---- the Mac ----
    def mac(self, pmset):
        with mock.patch.object(doctor.subprocess, "run", return_value=SimpleNamespace(stdout=pmset)):
            r = doctor.Report()
            doctor.check_mac(r)
        return r, "\n".join(r.lines)

    def test_sleep_settings(self):
        self.assertEqual(self.mac(" sleep                0\n")[0].warnings, 0)
        self.assertEqual(self.mac(" sleep                1 (sleep prevented by coreaudiod, powerd)\n")[0].warnings, 0)
        r, text = self.mac(" disksleep            10\n sleep                10\n")
        self.assertEqual(r.warnings, 1)
        self.assertIn("set to sleep after 10 min idle", text)

    # ---- settings ----
    def test_broken_settings_file(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "settings.json"
            path.write_text('{"user_name": "Dana",}')
            with mock.patch.object(config, "SETTINGS_PATH", path), mock.patch("sys.stdout", io.StringIO()):
                r = doctor.Report()
                doctor.check_settings(r)
                self.assertEqual(r.problems, 1)
                path.write_text('{"user_name": "Dana", "favourite_colour": "teal"}')
                r = doctor.Report()
                doctor.check_settings(r)
        text = "\n".join(r.lines)
        self.assertIn("unknown names are ignored: favourite_colour", text)
        self.assertNotIn("Dana", text)            # names of settings, never their values


class Whole(unittest.TestCase):
    def test_report_reads_well_and_hides_the_home_folder(self):
        r = doctor.Report()
        r.add(doctor.OK, f"Log: no errors recently ({Path.home()}/Library/Logs/vizbrain/vizbrain.log)")
        r.add(doctor.FAIL, "No Claude API key saved", "Run: vizbrain set-key")
        text = r.text()
        self.assertIn("[ OK ] Log: no errors recently (~/Library/Logs/vizbrain/vizbrain.log)", text)
        self.assertIn("[FAIL] No Claude API key saved\n       Fix: Run: vizbrain set-key", text)
        self.assertIn("1 problem found.", text)
        self.assertIn("email it to Kevin", text)
        self.assertNotIn(str(Path.home()), text)

    def test_warnings_alone_are_not_called_perfect(self):
        r = doctor.Report()
        r.add(doctor.WARN, "Not set to start at login")
        self.assertIn("No problems found. 1 thing worth a look", r.text())
        self.assertIn("Everything checks out.", doctor.Report().text())

    def test_one_broken_check_does_not_hide_the_rest(self):
        names = ["check_system", "check_packages", "check_settings", "check_internet", "check_key",
                 "check_models", "check_running", "check_agent", "check_mac", "check_bot", "check_log"]
        ran = []
        def make(name):
            def check(r, *a):
                ran.append(name)
                if name == "check_internet":
                    raise RuntimeError("boom")
                r.add(doctor.OK, name)
            check.__name__ = name
            return check
        with mock.patch.multiple(doctor, **{n: make(n) for n in names}), \
                mock.patch.object(config, "load", return_value=dict(config.DEFAULTS)), \
                mock.patch("sys.stdout", io.StringIO()) as out:
            code = doctor.run()
        self.assertEqual(ran, names)
        self.assertEqual(code, 0)
        self.assertIn("A check couldn't run (check_internet: RuntimeError: boom)", out.getvalue())

    def test_command(self):
        with mock.patch.object(doctor, "run", return_value=0) as run:
            self.assertEqual(cli.main(["doctor", "--copy"]), 0)
        run.assert_called_once_with(True)


if __name__ == "__main__":
    unittest.main()


class Connectors(unittest.TestCase):
    def check(self, conns, problems, health):
        r = doctor.Report()
        with mock.patch.object(config, "connectors", return_value=(conns, problems)):
            doctor.check_connectors(r, health)
        return r, "\n".join(r.lines)

    def test_none_listed_says_nothing(self):
        r, text = self.check({}, [], HEALTHY)
        self.assertEqual(text, "")

    def test_ready_failed_and_missing(self):
        health = dict(HEALTHY, connectors=[
            {"name": "kpow", "state": "ready", "tools": 11, "calls": 3, "errors": 0, "last_error": ""},
            {"name": "spotify", "state": "failed", "tools": 0, "calls": 0, "errors": 0,
             "last_error": "command not found: spotify-mcp"}])
        r, text = self.check({"kpow": {}, "spotify": {}, "new": {}}, [], health)
        self.assertIn("[ OK ] Connector kpow: ready, 11 tools, 3 calls, 0 errors", text)
        self.assertIn("[FAIL] Connector spotify is failed: command not found: spotify-mcp", text)
        self.assertIn("Fix: Run: vizbrain connectors test spotify", text)
        self.assertIn("[WARN] Connector new isn't running", text)
        self.assertEqual((r.problems, r.warnings), (1, 1))

    def test_a_broken_file_is_a_problem(self):
        r, text = self.check({}, ["connectors.json is unreadable: bad"], None)
        self.assertIn("[FAIL] connectors.json: connectors.json is unreadable: bad", text)
