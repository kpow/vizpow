"""Follow-up listening: when his reply asks a question, the bot listens for the
answer without a tap, at most MAX_FOLLOW_UPS times in a row.
Run: python -m unittest discover -s tests"""

import collections
import http.client
import io
import json
import struct
import threading
import unittest
from http.server import ThreadingHTTPServer
from types import SimpleNamespace
from unittest import mock

from vizbrain import server, speech


class AsksBack(unittest.TestCase):
    def test_a_question_at_the_end_asks_back(self):
        self.assertTrue(server.asks_back("I found two. Want me to read them?"))
        self.assertTrue(server.asks_back('Should I call it "done?" '))

    def test_a_question_just_before_the_last_sentence_counts(self):
        # From the live test: the suggestion was a question, then one more sentence.
        self.assertTrue(server.asks_back("Let me check. It's hot out, so how about some old rocksteady? "
                                         "Or a replay of the Phish shows could hit the spot."))

    def test_a_question_earlier_on_does_not(self):
        self.assertFalse(server.asks_back("Want the weather? It's sunny. High of 70. Low of 55."))

    def test_anything_else_does_not(self):
        for said in ("Done.", "", "Going red!", "It's sunny and 70."):
            self.assertFalse(server.asks_back(said), said)


class WhisperPrompt(unittest.TestCase):
    def test_hints_only(self):
        self.assertEqual(speech.whisper_prompt(["Kevin", "Maeby"]), "vizBot, Kevin, Maeby, Stackchan.")

    def test_the_question_being_answered_goes_last(self):
        self.assertEqual(speech.whisper_prompt(["Kevin"], "What are you in the mood for:\n reggae, Phish, or something else?"),
                         "vizBot, Kevin, Stackchan. What are you in the mood for: reggae, Phish, or something else?")

    def test_a_long_reply_keeps_its_end(self):
        reply = "Here is a long answer about many things. " * 10 + "Want me to put on some reggae?"
        prompt = speech.whisper_prompt([], reply)
        self.assertTrue(prompt.endswith("Want me to put on some reggae?"))
        self.assertLessEqual(len(prompt) - len("vizBot, Stackchan. "), speech.QUESTION_CONTEXT_CHARS)


class FakeMouth:
    def __init__(self, clip, voice, rate):
        self.clip, self.spoken = clip, []

    def say(self, text):
        self.spoken.append(text)

    def close(self):
        self.clip.finish()


class StartTurn(unittest.TestCase):
    """App.start_turn with a scripted brain and no speech synthesis."""

    def app(self, reply_text):
        app = object.__new__(server.App)
        app.settings = {"tts_rate": 190, "debug_log": False, "speaker": "bot"}
        app.clips = speech.ClipStore()
        app.log = collections.deque(maxlen=5)
        app.personality = lambda: "Chill"
        app.toolbox = SimpleNamespace(run=lambda *a: None)

        def turn(heard, personality, on_text=None):
            on_text(reply_text + " ")
            return {"text": reply_text, "tools": []}
        app.brain = SimpleNamespace(turn=turn, online=True)
        return app

    def run_turn(self, reply_text, may_listen):
        app = self.app(reply_text)
        done = threading.Event()
        with mock.patch.object(server.speech, "Mouth", FakeMouth), \
                mock.patch.object(server.speech, "voice_for", return_value="v"), \
                mock.patch("sys.stdout", io.StringIO()):
            reply = app.start_turn("hi", "voice", on_done=done.set, may_listen=may_listen)
            self.assertTrue(done.wait(5))
        return app, app.clips.get(reply["audio_id"])

    def test_a_question_marks_the_clip(self):
        app, clip = self.run_turn("I can look that up. Want me to?", may_listen=True)
        self.assertTrue(clip.done and clip.listen)
        self.assertTrue(app.log[0]["listen"])
        self.assertEqual(app.last_said, "I can look that up. Want me to?")

    def test_a_statement_does_not(self):
        _, clip = self.run_turn("It's sunny.", may_listen=True)
        self.assertFalse(clip.listen)

    def test_not_when_the_follow_ups_are_used_up(self):
        _, clip = self.run_turn("Anything else?", may_listen=False)
        self.assertFalse(clip.listen)


def wav(seconds=1.0) -> bytes:
    n = int(16000 * seconds)
    data = b"\x00\x00" * n
    header = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack(
        "<IHHIIHH", 16, 1, 1, 16000, 32000, 2, 16) + b"data" + struct.pack("<I", len(data))
    return header + data


class OverHttp(unittest.TestCase):
    """The /v1/voice and /v1/listen routes with a stand-in App."""

    def setUp(self):
        self.turns, self.heard, self.contexts = [], ["what's up"], []

        def transcribe(samples, hints, context=""):
            self.contexts.append(context)
            return self.heard[0]
        app = SimpleNamespace(
            listener=SimpleNamespace(state="ready", error="", transcribe=transcribe), last_said="Reggae, Phish, or something else?",
            adopt_bot=lambda ip, trusted=False: None, bot_token=None, busy=threading.Lock(),
            stt_hints=lambda: [], clips=speech.ClipStore(), follow_ups=0)

        def start_turn(heard, source, on_done=None, may_listen=False):
            self.turns.append((heard, may_listen))
            on_done()
            return {"heard": heard, "text": "ok", "bubble": "ok", "expression": -1,
                    "audio_id": "x", "audio_ms": 1, "timing_ms": {}}
        app.start_turn = start_turn
        self.app = app
        self.httpd = ThreadingHTTPServer(("127.0.0.1", 0), server.make_handler(app))
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
        self.addCleanup(self.httpd.server_close)
        self.addCleanup(self.httpd.shutdown)

    def call(self, method, path, headers=None, body=None):
        c = http.client.HTTPConnection("127.0.0.1", self.httpd.server_address[1], timeout=5)
        c.request(method, path, body=body, headers=headers or {})
        r = c.getresponse()
        data = json.loads(r.read() or b"{}")
        c.close()
        return r.status, data

    def voice(self, follow_up=False):
        headers = {"Content-Type": "audio/wav", **({"X-Viz-Follow-Up": "1"} if follow_up else {})}
        with mock.patch("sys.stdout", io.StringIO()):
            return self.call("POST", "/v1/voice", headers, wav())

    def test_follow_ups_are_allowed_up_to_the_limit_then_reset_by_a_tap(self):
        self.voice()
        for _ in range(server.MAX_FOLLOW_UPS):
            self.voice(follow_up=True)
        self.voice()
        self.assertEqual([m for _, m in self.turns], [True, True, True, False, True])

    def test_only_a_follow_up_is_heard_with_the_question_as_context(self):
        self.voice()
        self.voice(follow_up=True)
        self.assertEqual(self.contexts, ["", "Reggae, Phish, or something else?"])

    def test_silence_where_an_answer_was_expected_ends_quietly(self):
        self.heard[0] = ""
        status, reply = self.voice(follow_up=True)
        self.assertEqual(status, 200)
        self.assertEqual((reply["heard"], reply["audio_id"]), ("", ""))
        self.assertEqual(self.turns, [])
        self.assertTrue(self.app.busy.acquire(blocking=False), "busy lock was not released")

    def test_silence_after_a_tap_still_gets_a_turn(self):
        self.heard[0] = ""
        self.voice()
        self.assertEqual(self.turns, [("", True)])

    def test_listen_reports_the_clip_flag_once_it_is_finished(self):
        clip = speech.LiveClip()
        cid = self.app.clips.add(clip)
        clip.listen = True
        self.assertEqual(self.call("GET", f"/v1/listen/{cid}"), (200, {"listen": False}))   # still being written
        clip.finish()
        self.assertEqual(self.call("GET", f"/v1/listen/{cid}"), (200, {"listen": True}))
        self.assertEqual(self.call("GET", "/v1/listen/nope"), (200, {"listen": False}))

    def test_listen_needs_the_pairing_token_once_paired(self):
        self.app.bot_token = "ab12cd34.secret"
        status, _ = self.call("GET", "/v1/listen/x")
        self.assertEqual(status, 403)
        status, _ = self.call("GET", "/v1/listen/x", {"X-Viz-Token": "ab12cd34.secret"})
        self.assertEqual(status, 200)


if __name__ == "__main__":
    unittest.main()
