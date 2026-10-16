"""The voice picker: the catalog, blend specs, British pronunciation, and the
/v1/voices routes the bot's page uses. Run: python -m unittest discover -s tests"""

import http.client
import io
import json
import threading
import unittest
from http.server import ThreadingHTTPServer
from types import SimpleNamespace
from unittest import mock

import numpy as np

from vizbrain import config, server, speech, voices

KOKORO_ENGLISH = 28


class Catalog(unittest.TestCase):
    def test_ids_are_unique_and_every_spec_parses(self):
        ids = [v["id"] for v in voices.CATALOG]
        self.assertEqual(len(ids), len(set(ids)))
        for v in voices.CATALOG:
            self.assertTrue(voices.valid(v["spec"]), v)
            self.assertIn(v["group"], (voices.AMERICAN, voices.BRITISH, voices.MIXES), v)

    def test_every_english_kokoro_voice_is_offered_and_the_default_is_first(self):
        singles = {v["spec"] for v in voices.CATALOG if v["group"] != voices.MIXES and "@" not in v["spec"]}
        self.assertEqual(len(singles), KOKORO_ENGLISH)
        self.assertEqual(voices.CATALOG[0]["spec"], speech.KOKORO_VOICE)

    def test_custom_voices_are_added_and_bad_ones_skipped(self):
        settings = {"custom_voices": [
            {"id": "mine", "label": "My mix", "spec": "kokoro:af_heart*0.7+bf_emma*0.3"},
            {"id": "puck", "label": "Clash with a built-in", "spec": "kokoro:af_sky"},
            {"id": "broken", "spec": "kokoro:not a voice"},
            "not even a dict"]}
        added = [v for v in voices.catalog(settings) if v["group"] == "Custom"]
        self.assertEqual(added, [{"id": "mine", "label": "My mix", "group": "Custom",
                                  "spec": "kokoro:af_heart*0.7+bf_emma*0.3"}])

    def test_current_matches_the_spec_back_to_an_entry(self):
        self.assertEqual(voices.current({"tts_voice": "kokoro:bm_george"})["id"], "bm_george")
        self.assertEqual(voices.current({"tts_voice": "kokoro:bm_george*0.6+bm_lewis*0.4@0.9"})["label"],
                         "Old British gentleman")
        odd = voices.current({"tts_voice": "kokoro:af_sky@1.3"})
        self.assertEqual((odd["id"], odd["label"]), ("custom", "Custom (settings.json)"))

    def test_listing_shows_choices_without_specs(self):
        out = voices.listing({"tts_voice": "kokoro:am_onyx"})
        self.assertEqual((out["current"], out["current_label"]), ("am_onyx", "Onyx (deep)"))
        self.assertEqual(set(out["voices"][0]), {"id", "label", "group"})


class Specs(unittest.TestCase):
    def test_a_single_voice_and_speed(self):
        self.assertEqual(speech.parse_kokoro("kokoro:am_puck@1.12"), ([("am_puck", 1.0)], 1.12))
        self.assertEqual(speech.parse_kokoro("kokoro:bm_fable"), ([("bm_fable", 1.0)], 1.0))

    def test_a_blend_is_normalised(self):
        parts, speed = speech.parse_kokoro("kokoro:bm_george*3+bm_lewis*1@0.9")
        self.assertEqual(parts, [("bm_george", 0.75), ("bm_lewis", 0.25)])
        self.assertEqual(speed, 0.9)

    def test_bad_specs_are_refused(self):
        for spec in ("kokoro:", "kokoro:George", "kokoro:bm_george*0", "kokoro:bm_george@9", "kokoro:bm_george*x"):
            with self.assertRaises(ValueError, msg=spec):
                speech.parse_kokoro(spec)
        self.assertTrue(voices.valid("Samantha"))       # a macOS voice is fine too
        self.assertFalse(voices.valid("kokoro:nope"))

    def test_mostly_british_voices_speak_british(self):
        self.assertEqual(speech.kokoro_lang([("bm_george", 1.0)]), "en-gb")
        self.assertEqual(speech.kokoro_lang([("bm_george", 0.6), ("am_adam", 0.4)]), "en-gb")
        self.assertEqual(speech.kokoro_lang([("bm_george", 0.5), ("am_adam", 0.5)]), "en-us")
        self.assertEqual(speech.kokoro_lang([("af_heart", 1.0)]), "en-us")


class Blending(unittest.TestCase):
    def test_a_blend_is_a_weighted_sum_of_styles_and_is_built_once(self):
        styles = {"bm_george": np.full((2, 1, 4), 1.0, np.float32), "bm_lewis": np.full((2, 1, 4), 3.0, np.float32)}
        calls = []
        fake = SimpleNamespace(get_voice_style=lambda n: calls.append(n) or styles[n],
                               create=lambda text, voice, speed, lang: (calls.append((type(voice).__name__, lang, speed)) or
                                                                       (np.zeros(240, np.float32), 24000)))
        k = speech._Kokoro()
        k._k = fake
        parts = [("bm_george", 0.75), ("bm_lewis", 0.25)]
        k.speak("hi", parts, 0.9)
        k.speak("again", parts, 0.9)
        self.assertEqual(calls.count("bm_george"), 1)
        self.assertEqual(calls[-1], ("ndarray", "en-gb", 0.9))
        np.testing.assert_allclose(k._styles["bm_george*0.750+bm_lewis*0.250"], np.full((2, 1, 4), 1.5))

    def test_a_single_voice_goes_by_name(self):
        seen = []
        k = speech._Kokoro()
        k._k = SimpleNamespace(create=lambda text, voice, speed, lang: seen.append((voice, lang)) or (np.zeros(10, np.float32), 24000))
        k.speak("hi", "af_heart", 1.0)
        self.assertEqual(seen, [("af_heart", "en-us")])


class Routes(unittest.TestCase):
    """GET/POST /v1/voices with a stand-in App."""

    def setUp(self):
        self.spoken, self.saved = [], []
        self.hushed = []
        app = SimpleNamespace(settings={"tts_voice": "kokoro:am_puck@1.12", "speaker": "bot"}, bot_token=None,
                              _say_text=lambda text, expr, source, sound, heard="", voice=None:
                                  self.spoken.append((text, voice)),
                              mac_problem=lambda: "",
                              mac_speaker=SimpleNamespace(hush=lambda: self.hushed.append(True)))
        self.app = app
        patch = mock.patch.object(config, "save", lambda s: self.saved.append(dict(s)))
        patch.start()
        self.addCleanup(patch.stop)
        self.httpd = ThreadingHTTPServer(("127.0.0.1", 0), server.make_handler(app))
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
        self.addCleanup(self.httpd.server_close)
        self.addCleanup(self.httpd.shutdown)

    def call(self, method, path, body=None, headers=None):
        c = http.client.HTTPConnection("127.0.0.1", self.httpd.server_address[1], timeout=5)
        c.request(method, path, body=json.dumps(body) if body is not None else None,
                  headers={"Content-Type": "application/json", **(headers or {})})
        r = c.getresponse()
        data = json.loads(r.read() or b"{}")
        c.close()
        return r.status, data

    def test_get_lists_the_choices_and_the_current_one(self):
        status, data = self.call("GET", "/v1/voices")
        self.assertEqual(status, 200)
        self.assertEqual(data["current"], "puck")
        self.assertEqual(len(data["voices"]), len(voices.CATALOG))

    def test_post_switches_saves_and_says_a_sample(self):
        with mock.patch("sys.stdout", io.StringIO()):
            status, data = self.call("POST", "/v1/voices", {"id": "gentleman"})
        self.assertEqual((status, data["current"], data["current_label"]), (200, "gentleman", "Old British gentleman"))
        spec = "kokoro:bm_george*0.6+bm_lewis*0.4@0.9"
        self.assertEqual(self.app.settings["tts_voice"], spec)
        self.assertEqual(self.saved[-1]["tts_voice"], spec)
        for _ in range(50):
            if self.spoken:
                break
            threading.Event().wait(0.02)
        self.assertEqual(self.spoken, [(voices.SAMPLE, spec)])

    def test_an_unknown_voice_is_refused(self):
        status, data = self.call("POST", "/v1/voices", {"id": "nope"})
        self.assertEqual((status, data["error"]), (400, "unknown voice"))
        self.assertEqual(self.app.settings["tts_voice"], "kokoro:am_puck@1.12")

    def test_once_paired_the_token_is_needed(self):
        self.app.bot_token = "ab12cd34.secret"
        self.assertEqual(self.call("GET", "/v1/voices")[0], 403)
        self.assertEqual(self.call("POST", "/v1/voices", {"id": "puck"})[0], 403)
        self.assertEqual(self.call("GET", "/v1/voices", headers={"X-Viz-Token": "ab12cd34.secret"})[0], 200)

    def wait_spoken(self):
        for _ in range(50):
            if self.spoken:
                return
            threading.Event().wait(0.02)

    def test_get_says_where_he_speaks(self):
        _, data = self.call("GET", "/v1/voices")
        self.assertEqual((data["speaker"], data["mac_problem"]), ("bot", ""))

    def test_post_speaker_switches_saves_and_says_a_line(self):
        with mock.patch("sys.stdout", io.StringIO()):
            status, data = self.call("POST", "/v1/voices", {"speaker": "mac"})
        self.assertEqual((status, data["speaker"]), (200, "mac"))
        self.assertEqual(self.saved[-1]["speaker"], "mac")
        self.assertEqual(self.app.settings["tts_voice"], "kokoro:am_puck@1.12")   # the voice is untouched
        self.wait_spoken()
        self.assertEqual(len(self.spoken), 1)

    def test_post_speaker_reports_why_the_mac_cant_yet(self):
        self.app.mac_problem = lambda: "the bot needs firmware 3.7.37 or later"
        with mock.patch("sys.stdout", io.StringIO()):
            _, data = self.call("POST", "/v1/voices", {"speaker": "mac"})
        self.assertEqual(data["mac_problem"], "the bot needs firmware 3.7.37 or later")

    def test_an_unknown_speaker_is_refused(self):
        status, _ = self.call("POST", "/v1/voices", {"speaker": "kitchen"})
        self.assertEqual(status, 400)
        self.assertEqual(self.app.settings["speaker"], "bot")

    def test_hush_stops_the_mac(self):
        self.assertEqual(self.call("POST", "/v1/hush", {})[0], 200)
        self.assertEqual(self.hushed, [True])

    def test_a_browser_page_cannot_switch_it(self):
        status, _ = self.call("POST", "/v1/voices", {"id": "puck"}, headers={"Origin": "http://evil.example"})
        self.assertEqual(status, 403)


class MacVoice(unittest.TestCase):
    """Which clips the Mac plays: chosen in settings, and only when it can."""

    def app(self, speaker="mac", firmware="3.7.37", audio=True):
        app = object.__new__(server.App)
        app.settings = {"speaker": speaker}
        app.bot_info = {"firmware": firmware}
        app.mac_speaker = SimpleNamespace(available=audio, error="OSError: no PortAudio")
        return app

    def test_mac_when_chosen_and_possible(self):
        self.assertTrue(self.app().mac_voice())

    def test_the_bot_when_chosen(self):
        self.assertFalse(self.app(speaker="bot").mac_voice())

    def test_older_firmware_keeps_his_own_speaker(self):
        # It can't mute itself, so he would talk twice.
        app = self.app(firmware="3.7.36")
        self.assertFalse(app.mac_voice())
        self.assertIn("3.7.37", app.mac_problem())

    def test_a_mac_without_audio_keeps_his_own_speaker(self):
        app = self.app(audio=False)
        self.assertFalse(app.mac_voice())
        self.assertIn("PortAudio", app.mac_problem())


class MacSpeakerPlayback(unittest.TestCase):
    """MacSpeaker plays a clip as it grows, a hush cuts it short, and each clip
    gets a fresh device list (a reconnected Bluetooth speaker has a new ID)."""

    def setUp(self):
        self.written, self.aborted, self.refreshed = [], [], []
        test = self

        class Out:
            def __init__(self, **kw):
                test.opened = kw
            def __enter__(self):
                return self
            def __exit__(self, *a):
                pass
            def write(self, data):
                test.written.append(len(data))
                threading.Event().wait(0.01)
            def abort(self):
                test.aborted.append(True)
        sd = SimpleNamespace(RawOutputStream=Out, _terminate=lambda: self.refreshed.append("stop"),
                             _initialize=lambda: self.refreshed.append("start"))
        patch = mock.patch.dict("sys.modules", {"sounddevice": sd})
        patch.start()
        self.addCleanup(patch.stop)

    def wait(self, cond):
        for _ in range(200):
            if cond():
                return True
            threading.Event().wait(0.01)
        return False

    def test_plays_a_growing_clip_to_the_end(self):
        clip = speech.LiveClip()
        clip.append(b"\0" * 9600)
        speech.MacSpeaker().play(clip)
        clip.append(b"\0" * 4800)
        clip.finish()
        self.assertTrue(self.wait(lambda: sum(self.written) == 14400))
        self.assertEqual(self.opened, {"samplerate": speech.PLAY_RATE, "channels": 1, "dtype": "int16"})
        self.assertFalse(self.aborted)

    def test_hush_stops_and_drops_the_rest(self):
        clip = speech.LiveClip(b"\0" * speech.MacSpeaker.PIECE * 50)   # 5 s
        mac = speech.MacSpeaker()
        mac.play(clip)
        self.assertTrue(self.wait(lambda: self.written))
        mac.hush()
        self.assertTrue(self.wait(lambda: self.aborted))
        self.assertLess(sum(self.written), speech.MacSpeaker.PIECE * 50)

    def test_rereads_devices_before_each_clip(self):
        mac = speech.MacSpeaker()
        for _ in range(2):
            mac.play(speech.LiveClip(b"\0" * 4800))
            self.assertTrue(self.wait(lambda: mac._streams == 0 and self.written))
        self.assertEqual(self.refreshed, ["stop", "start"] * 2)

    def test_leaves_portaudio_alone_while_a_stream_is_open(self):
        mac = speech.MacSpeaker()
        mac._streams = 1                    # another clip still playing
        mac.play(speech.LiveClip(b"\0" * 4800))
        self.assertTrue(self.wait(lambda: self.written))
        self.assertEqual(self.refreshed, [])


if __name__ == "__main__":
    unittest.main()
