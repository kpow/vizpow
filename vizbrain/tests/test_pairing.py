"""Pairing: the token file, handing the token to the bot (App.announce), and
which callers the brain will follow. Run: python -m unittest discover -s tests"""

import stat
import tempfile
import threading
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from vizbrain import config, server
from vizbrain.bot import BotError

TOKEN = "ab12cd34.0123456789abcdef0123456789abcdef"


class FakeBot:
    """A bot's /brain/status and /brain/config, as firmware 3.7.31 answers them."""

    def __init__(self, host="127.0.0.1", pairs=True, **status):
        self.host = host
        self.status = {"brainHost": "", "wake": {"enabled": True}, **status}
        if pairs:
            self.status.setdefault("pairId", "")
        self.configs = []
        self.down = False

    def _get(self, path, params=None, timeout=3.0):
        if self.down:
            raise BotError(f"{path}: timed out")
        if path == "/brain/config":
            self.configs.append(params)
            if "host" in params:
                self.status["brainHost"] = params["host"]
            if "token" in params and "pairId" in self.status:
                self.status["pairId"] = params["token"].split(".")[0]
            if "wake" in params:
                self.status["wake"] = {"enabled": bool(params["wake"])}
        return self.status

    def state(self):
        if self.down:
            raise BotError("/state: timed out")
        return {"boardType": "stackchan", "firmwareVersion": "3.7.31"}


def make_app(bot, paired=False, wake_model=False, bot_host=""):
    app = SimpleNamespace(
        _bot=bot, _bot_lock=threading.Lock(), get_bot=lambda: app._bot,
        pairing={"token": TOKEN, "paired": paired},
        settings={"bot_host": bot_host, "wake_enabled": True},
        wake=SimpleNamespace(available=wake_model),
        _announce_fails=0, _announce_err="",
        bot_info={"firmware": None, "seen": 0.0, "error": "", "brain_addr": ""})
    return app


class Announce(unittest.TestCase):
    def setUp(self):
        p = mock.patch.object(config, "save_pairing")
        self.saved = p.start()
        self.addCleanup(p.stop)

    def test_new_bot_gets_address_and_token_and_is_paired(self):
        bot = FakeBot()
        app = make_app(bot)
        server.App.announce(app, 4050)
        self.assertEqual(bot.configs, [{"host": "127.0.0.1:4050", "token": TOKEN, "wake": 0}])
        self.assertTrue(app.pairing["paired"])
        self.saved.assert_called_once()

    def test_announce_records_what_health_reports(self):
        bot = FakeBot()
        app = make_app(bot)
        server.App.announce(app, 4050)
        self.assertEqual(app.bot_info["firmware"], "3.7.31")
        self.assertEqual(app.bot_info["brain_addr"], "127.0.0.1:4050")
        self.assertGreater(app.bot_info["seen"], 0)
        bot.down = True
        server.App.announce(app, 4050)
        self.assertIn("timed out", app.bot_info["error"])

    def test_settled_bot_is_left_alone(self):
        bot = FakeBot(brainHost="127.0.0.1:4050", pairId="ab12cd34", wake={"enabled": False})
        app = make_app(bot, paired=True)
        server.App.announce(app, 4050)
        self.assertEqual(bot.configs, [])
        self.saved.assert_not_called()

    def test_moved_brain_address_is_resent(self):
        bot = FakeBot(brainHost="192.168.1.23:4050", pairId="ab12cd34", wake={"enabled": False})
        server.App.announce(make_app(bot, paired=True), 4050)
        self.assertEqual(bot.configs, [{"host": "127.0.0.1:4050"}])

    def test_bot_holding_another_brains_token_is_repaired(self):
        bot = FakeBot(brainHost="127.0.0.1:4050", pairId="deadbeef", wake={"enabled": False})
        server.App.announce(make_app(bot, paired=True), 4050)
        self.assertEqual(bot.configs, [{"token": TOKEN}])

    def test_old_firmware_gets_no_token_and_stays_unpaired(self):
        bot = FakeBot(pairs=False)
        app = make_app(bot)
        server.App.announce(app, 4050)
        self.assertEqual(bot.configs, [{"host": "127.0.0.1:4050", "wake": 0}])
        self.assertFalse(app.pairing["paired"])

    def test_wake_follows_the_model(self):
        bot = FakeBot(brainHost="127.0.0.1:4050", pairId="ab12cd34", wake={"enabled": False})
        server.App.announce(make_app(bot, paired=True, wake_model=True), 4050)
        self.assertEqual(bot.configs, [{"wake": 1}])

    def test_unreachable_bot_is_looked_for_again(self):
        bot = FakeBot()
        bot.down = True
        app = make_app(bot)
        for _ in range(2):
            server.App.announce(app, 4050)
        self.assertIs(app._bot, bot)
        server.App.announce(app, 4050)
        self.assertIsNone(app._bot)

    def test_unreachable_pinned_bot_is_kept(self):
        bot = FakeBot()
        bot.down = True
        app = make_app(bot, bot_host="192.168.1.80")
        for _ in range(4):
            server.App.announce(app, 4050)
        self.assertIs(app._bot, bot)


class Adopt(unittest.TestCase):
    def adopt(self, current, ip, trusted, bot_host=""):
        app = make_app(current, bot_host=bot_host)
        with mock.patch.object(server, "Bot", lambda host: SimpleNamespace(host=host)):
            server.App.adopt_bot(app, ip, trusted=trusted)
        return app._bot.host if app._bot else None

    def test_first_caller_is_taken_when_no_bot_is_known(self):
        self.assertEqual(self.adopt(None, "192.168.1.80", trusted=False), "192.168.1.80")

    def test_stranger_cannot_replace_the_bot(self):
        self.assertEqual(self.adopt(FakeBot("192.168.1.80"), "192.168.1.66", trusted=False), "192.168.1.80")

    def test_bot_with_the_token_is_followed_to_a_new_address(self):
        self.assertEqual(self.adopt(FakeBot("192.168.1.80"), "192.168.1.91", trusted=True), "192.168.1.91")

    def test_pinned_bot_never_moves(self):
        self.assertEqual(self.adopt(FakeBot("192.168.1.80"), "192.168.1.91", trusted=True,
                                    bot_host="192.168.1.80"), "192.168.1.80")


class TokenFile(unittest.TestCase):
    def test_created_once_private_and_stable(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "pairing.json"
            with mock.patch.object(config, "SETTINGS_DIR", Path(d)), \
                    mock.patch.object(config, "PAIRING_PATH", path):
                first = config.pairing()
                self.assertRegex(first["token"], r"^[0-9a-f]{8}\.[0-9a-f]{32}$")
                self.assertFalse(first["paired"])
                self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
                config.save_pairing({**first, "paired": True})
                self.assertEqual(config.pairing(), {**first, "paired": True})
                path.write_text("not json")
                self.assertNotEqual(config.pairing()["token"], first["token"])


if __name__ == "__main__":
    unittest.main()
