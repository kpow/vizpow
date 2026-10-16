"""Who may call which route (server.refusal), checked both as a function and
over a real socket. Run: python -m unittest discover -s tests"""

import collections
import http.client
import json
import threading
import unittest
from http.server import ThreadingHTTPServer
from types import SimpleNamespace

from vizbrain import server

PORT = 4050
OK_HOST = {"Host": f"localhost:{PORT}"}
JSON = {"Content-Type": "application/json"}


class Refusal(unittest.TestCase):
    def why(self, method, path, ip, headers):
        return server.refusal(method, path, ip, headers, PORT)

    def test_local_routes_serve_loopback(self):
        for path in ("/", "/v1/health", "/v1/log"):
            self.assertIsNone(self.why("GET", path, "127.0.0.1", OK_HOST))
        self.assertIsNone(self.why("GET", "/v1/log", "::1", {"Host": f"[::1]:{PORT}"}))
        self.assertIsNone(self.why("POST", "/v1/text", "127.0.0.1", {**OK_HOST, **JSON}))

    def test_local_routes_refuse_the_lan(self):
        for method, path in (("GET", "/"), ("GET", "/v1/log"), ("GET", "/v1/health"),
                             ("POST", "/v1/text"), ("POST", "/v1/say")):
            self.assertIn("only available", self.why(method, path, "192.168.1.50", {**OK_HOST, **JSON}))

    def test_rebound_dns_name_is_refused(self):
        self.assertIn("Host", self.why("GET", "/v1/log", "127.0.0.1", {"Host": f"evil.example:{PORT}"}))
        self.assertIn("Host", self.why("GET", "/v1/log", "127.0.0.1", {}))

    def test_other_sites_pages_are_refused(self):
        h = {**OK_HOST, **JSON, "Origin": "https://evil.example"}
        self.assertEqual(self.why("POST", "/v1/text", "127.0.0.1", h), "cross-site request")
        h["Origin"] = "null"
        self.assertEqual(self.why("POST", "/v1/text", "127.0.0.1", h), "cross-site request")
        h["Origin"] = f"http://localhost:{PORT}"
        self.assertIsNone(self.why("POST", "/v1/text", "127.0.0.1", h))

    def test_simple_cross_site_post_is_refused(self):
        # What fetch(..., {mode: "no-cors"}) can send without a preflight.
        for ctype in ("text/plain", "application/x-www-form-urlencoded", None):
            h = dict(OK_HOST, **({"Content-Type": ctype} if ctype else {}))
            self.assertIn("Content-Type", self.why("POST", "/v1/text", "127.0.0.1", h))
        h = {**OK_HOST, "Content-Type": "application/json; charset=utf-8"}
        self.assertIsNone(self.why("POST", "/v1/say", "127.0.0.1", h))

    def test_bot_routes_stay_open_to_the_lan(self):
        bot = {"Host": "192.168.1.57:4050", "X-Bot-Id": "vizbot-a3f2"}
        self.assertIsNone(self.why("POST", "/v1/voice", "192.168.1.80", bot))
        self.assertIsNone(self.why("POST", "/v1/event", "192.168.1.80", bot))
        self.assertIsNone(self.why("GET", "/v1/audio/0123456789ab", "192.168.1.80", bot))

    def test_bot_routes_refuse_browsers(self):
        h = {"Host": "192.168.1.57:4050", "Origin": "https://evil.example"}
        self.assertIn("browser", self.why("POST", "/v1/event", "192.168.1.80", h))
        self.assertIn("browser", self.why("POST", "/v1/voice", "127.0.0.1", h))

    def test_paired_bot_routes_need_the_token(self):
        token = "ab12cd34.0123456789abcdef0123456789abcdef"
        for method, path in (("POST", "/v1/voice"), ("POST", "/v1/event"), ("GET", "/v1/audio/0123456789ab")):
            def why(headers):
                return server.refusal(method, path, "192.168.1.80", headers, PORT, token)
            self.assertEqual(why({"X-Bot-Id": "vizbot"}), server.NOT_PAIRED)
            self.assertEqual(why({"X-Viz-Token": "ab12cd34.wrong"}), server.NOT_PAIRED)
            self.assertEqual(why({"X-Viz-Token": "ab12cd34"}), server.NOT_PAIRED)   # the public half
            self.assertIsNone(why({"X-Viz-Token": token}))

    def test_token_does_not_open_the_local_routes(self):
        token = "ab12cd34.0123456789abcdef0123456789abcdef"
        h = {**OK_HOST, **JSON, "X-Viz-Token": token}
        self.assertIn("only available", server.refusal("POST", "/v1/text", "192.168.1.80", h, PORT, token))


class OverHttp(unittest.TestCase):
    """The handler with a stand-in App: no model, no bot, no Bonjour."""

    @classmethod
    def setUpClass(cls):
        cls.turns = []
        app = SimpleNamespace(
            _bot=None, log=collections.deque([{"heard": "secret", "text": "secret"}]),
            brain=SimpleNamespace(online=True, capped=lambda: None, key_state="valid", last_error=None),
            settings={"model": "m", "stt_model": "s", "tts_voice": "kokoro:am_puck",
                      "speaker": "bot"},
            mac_problem=lambda: "", mac_speaker=SimpleNamespace(failing=""),
            started=0.0, listener=SimpleNamespace(state="ready", error=""),
            bot_info={"firmware": "3.7.31", "seen": 0.0, "error": "", "brain_addr": ""},
            adopt_bot=lambda ip, trusted=False: None,
            wake=SimpleNamespace(available=False, threshold=0.5, wakes=0,
                                 receiver=SimpleNamespace(packets=0)),
            busy=threading.Lock(), get_bot=lambda: None, bot_token=None,
            pairing={"token": "ab12cd34.secret", "paired": False},
            connectors=None, connector_problems=[],
            handle_event=lambda evt: "ignored (test)")

        def start_turn(heard, source, on_done=None):
            cls.turns.append(heard)
            on_done()
            return {"heard": heard, "text": "ok", "bubble": "ok", "expression": -1,
                    "audio_id": "x", "audio_ms": 1, "timing_ms": {}}
        app.start_turn = start_turn
        cls.app = app
        cls.httpd = ThreadingHTTPServer(("127.0.0.1", 0), server.make_handler(app))
        cls.port = cls.httpd.server_address[1]
        threading.Thread(target=cls.httpd.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()

    def call(self, method, path, headers=None, body=None):
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        c.putrequest(method, path, skip_host=True)
        for k, v in (headers or {}).items():
            c.putheader(k, v)
        if body is not None:
            c.putheader("Content-Length", str(len(body)))
        c.endheaders(body)
        r = c.getresponse()
        data = r.read()
        c.close()
        return r.status, data

    def host(self, **extra):
        return {"Host": f"localhost:{self.port}", **extra}

    def test_log_and_page_from_this_mac(self):
        status, data = self.call("GET", "/v1/log", self.host())
        self.assertEqual(status, 200)
        self.assertIn(b"secret", data)
        self.assertEqual(self.call("GET", "/", self.host())[0], 200)

    def test_rebinding_cannot_read_the_log(self):
        status, data = self.call("GET", "/v1/log", {"Host": f"evil.example:{self.port}"})
        self.assertEqual(status, 403)
        self.assertNotIn(b"secret", data)

    def test_typing_page_post_runs_a_turn(self):
        body = json.dumps({"text": "hello"}).encode()
        h = self.host(**{"Content-Type": "application/json", "Origin": f"http://localhost:{self.port}"})
        self.assertEqual(self.call("POST", "/v1/text", h, body)[0], 200)
        self.assertIn("hello", self.turns)

    def test_cross_site_post_runs_nothing(self):
        before = len(self.turns)
        body = json.dumps({"text": "spend money"}).encode()
        for h in (self.host(**{"Content-Type": "text/plain"}),
                  self.host(**{"Content-Type": "application/json", "Origin": "https://evil.example"})):
            self.assertEqual(self.call("POST", "/v1/text", h, body)[0], 403)
        self.assertEqual(len(self.turns), before)

    def test_preflight_is_never_answered(self):
        status, _ = self.call("OPTIONS", "/v1/text", self.host(Origin="https://evil.example"))
        self.assertEqual(status, 501)

    def test_long_text_is_refused(self):
        before = len(self.turns)
        body = json.dumps({"text": "x" * (server.MAX_TEXT_CHARS + 1)}).encode()
        h = self.host(**{"Content-Type": "application/json"})
        self.assertEqual(self.call("POST", "/v1/text", h, body)[0], 413)
        self.assertEqual(len(self.turns), before)

    def test_bot_event_still_accepted(self):
        status, data = self.call("POST", "/v1/event", {"Host": "192.168.1.57:4050", "X-Bot-Id": "vizbot"},
                                 b'{"type":"arrival"}')
        self.assertEqual(status, 202)
        self.assertIn(b"ignored (test)", data)

    def test_paired_brain_refuses_a_caller_without_the_token(self):
        self.app.bot_token = "ab12cd34.secret"
        self.addCleanup(setattr, self.app, "bot_token", None)
        h = {"Host": "192.168.1.57:4050", "X-Bot-Id": "vizbot"}
        status, data = self.call("POST", "/v1/event", h, b'{"type":"arrival"}')
        self.assertEqual(status, 403)
        self.assertIn("not paired", json.loads(data)["say"])     # the bot shows this as his bubble
        h["X-Viz-Token"] = "ab12cd34.secret"
        self.assertEqual(self.call("POST", "/v1/event", h, b'{"type":"arrival"}')[0], 202)

    def test_health_stays_reachable_when_paired(self):
        self.app.bot_token = "ab12cd34.secret"
        self.addCleanup(setattr, self.app, "bot_token", None)
        self.assertEqual(self.call("GET", "/v1/health", self.host())[0], 200)

    def test_health_says_what_doctor_needs(self):
        h = json.loads(self.call("GET", "/v1/health", self.host())[1])
        self.assertEqual((h["version"], h["key"], h["ears"], h["bot_firmware_ok"]),
                         (server.__version__, "valid", "ready", True))
        self.assertEqual(h["voice"], "kokoro:am_puck")
        self.assertIsNone(h["bot_seen_s_ago"])

    def test_voice_before_the_ears_are_ready_gets_a_bubble_not_a_hang(self):
        for state, words in (("loading", "still getting my ears ready"), ("failed", "vizbrain doctor")):
            self.app.listener.state = state
            self.addCleanup(setattr, self.app.listener, "state", "ready")
            before = len(self.turns)
            status, data = self.call("POST", "/v1/voice", {"Host": "192.168.1.57:4050"}, b"RIFF")
            self.assertEqual(status, 503)
            self.assertIn(words, json.loads(data)["say"])
            self.assertEqual(len(self.turns), before)
            self.assertFalse(self.app.busy.locked())


if __name__ == "__main__":
    unittest.main()
