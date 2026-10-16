"""The voice picker: the catalog, blend specs, British pronunciation, and the
/v1/voices routes the bot's page uses. Run: python -m unittest discover -s tests"""

import http.client
import io
import json
import subprocess
import threading
import time
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

    def app(self, speaker="mac", firmware="3.7.37", audio=True, failing=""):
        app = object.__new__(server.App)
        app.settings = {"speaker": speaker}
        app.bot_info = {"firmware": firmware}
        app.mac_speaker = SimpleNamespace(available=audio, error="OSError: no PortAudio",
                                          failing=failing)
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


class MacDroppedAClip(unittest.TestCase):
    """A clip the Mac dropped is ours to handle, not news for the page."""

    def app(self, failing=""):
        app = object.__new__(server.App)
        app.settings = {"speaker": "mac"}
        app.bot_info = {"firmware": "3.7.37"}
        app.mac_speaker = SimpleNamespace(available=True, error="", failing=failing)
        return app

    def test_the_next_clip_goes_to_his_own_speaker(self):
        # He mutes himself for a Mac clip, so a failing Mac would leave silence.
        self.assertFalse(self.app(failing="PaErrorCode -9986").mac_voice())

    def test_it_is_not_put_in_front_of_anyone(self):
        # mac_problem() reaches the bot's page; a dropped clip is a diagnostic.
        app = self.app(failing="PaErrorCode -9986")
        self.assertEqual(app.mac_problem(), "")

    def test_setup_problems_are_still_worth_saying(self):
        app = self.app(failing="PaErrorCode -9986")
        app.mac_speaker.available = False
        app.mac_speaker.error = "OSError: no PortAudio"
        self.assertIn("PortAudio", app.mac_problem())   # this one the page should say

    def test_once_it_lapses_he_goes_back_to_the_mac(self):
        self.assertTrue(self.app(failing="").mac_voice())


class MacSpeakerRemembersAFailure(unittest.TestCase):
    """MacSpeaker.failing: set when a clip is dropped, cleared when one plays."""

    def stub(self, err):
        test = self

        class Out:
            def __init__(self, **kw):
                if err:
                    raise err
            def __enter__(self):
                return self
            def __exit__(self, *a):
                pass
            def write(self, data):
                test.written.append(len(data))
            def abort(self):
                pass
        self.written = []
        return SimpleNamespace(RawOutputStream=Out, _terminate=lambda: None,
                               _initialize=lambda: None,
                               default=SimpleNamespace(device=(0, 1)),
                               query_devices=lambda i: {"name": "MIDDLETON"})

    def play(self, mac, err=None):
        clip = speech.LiveClip(b"\0" * 4800)
        clip.finish()
        with mock.patch.dict("sys.modules", {"sounddevice": self.stub(err)}):
            mac._run(clip, threading.Event())

    def test_an_open_that_fails_is_remembered_not_raised(self):
        mac = speech.MacSpeaker()
        self.assertEqual(mac.failing, "")
        self.play(mac, RuntimeError("Internal PortAudio error [PaErrorCode -9986]"))
        self.assertIn("-9986", mac.failing)
        self.assertEqual(self.written, [])

    def test_a_clip_that_plays_clears_it(self):
        mac = speech.MacSpeaker()
        self.play(mac, RuntimeError("no device"))
        self.assertIn("no device", mac.failing)
        self.play(mac)
        self.assertEqual(mac.failing, "")
        self.assertEqual(sum(self.written), 4800)

    def test_it_lapses_so_he_tries_the_mac_again(self):
        mac = speech.MacSpeaker()
        mac._failure = ("no device", time.time())
        self.assertEqual(mac.failing, "no device")
        mac._failure = ("no device", time.time() - speech.MacSpeaker.FAILED_S - 1)
        self.assertEqual(mac.failing, "")

    def test_a_failed_open_still_lets_go_of_the_stream_count(self):
        mac = speech.MacSpeaker()
        self.play(mac, RuntimeError("no device"))
        self.assertEqual(mac._streams, 0)   # or the next clip would skip the re-read


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
        self.sd = SimpleNamespace(
            RawOutputStream=Out, _terminate=lambda: self.refreshed.append("stop"),
            _initialize=lambda: self.refreshed.append("start"),
            default=SimpleNamespace(device=(0, 1)),
            query_devices=lambda i: {"name": "MIDDLETON"} if i == 1 else {"name": "a microphone"})
        patch = mock.patch.dict("sys.modules", {"sounddevice": self.sd})
        patch.start()
        self.addCleanup(patch.stop)
        self.logged = []
        said = mock.patch("builtins.print", lambda *a, **kw: self.logged.append(" ".join(str(x) for x in a)))
        said.start()
        self.addCleanup(said.stop)

    def line(self):
        """The one [mac-speaker] line this clip logged."""
        lines = [l for l in self.logged if l.startswith("[mac-speaker]")]
        self.assertEqual(len(lines), 1, self.logged)
        return lines[0]

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

    def test_a_clip_that_played_says_where_and_how_long(self):
        """The whole point: nobody turns on debug_log before the morning he
        goes quiet, so a clip that worked has to leave a line of its own."""
        mac = speech.MacSpeaker()
        mac.play(speech.LiveClip(b"\0" * speech.PLAY_RATE * 2))      # 1.0 s of s16 mono
        self.assertTrue(self.wait(lambda: mac._streams == 0 and self.logged))
        line = self.line()
        self.assertIn("played 1.0s", line)
        self.assertIn("on MIDDLETON", line)
        self.assertRegex(line, r"in \d+\.\ds$")

    def test_a_hush_reads_differently_from_a_clip_that_finished(self):
        mac = speech.MacSpeaker()
        mac.play(speech.LiveClip(b"\0" * speech.MacSpeaker.PIECE * 50))   # 5 s
        self.assertTrue(self.wait(lambda: self.written))
        mac.hush()
        self.assertTrue(self.wait(lambda: self.logged))
        line = self.line()
        self.assertTrue(line.startswith("[mac-speaker] hushed "), line)
        self.assertIn("on MIDDLETON", line)
        self.assertLess(float(line.split("hushed ")[1].split("s ")[0]), 5.0)

    def test_a_clip_that_stalls_reads_differently_again(self):
        mac = speech.MacSpeaker()
        mac.STALL_S = 0.0                   # a clip that never finishes and never grows
        mac.play(speech.LiveClip())
        self.assertTrue(self.wait(lambda: self.logged))
        self.assertTrue(self.line().startswith("[mac-speaker] stalled 0.0s on MIDDLETON "), self.line())

    def test_a_failed_open_still_says_so_once(self):
        def boom(**kw):
            raise RuntimeError("'!obj'")
        self.sd.RawOutputStream = boom
        mac = speech.MacSpeaker()
        mac.play(speech.LiveClip(b"\0" * 4800))
        self.assertTrue(self.wait(lambda: self.logged))
        self.assertIn("playback failed", self.line())


class MacSound(unittest.TestCase):
    """What /v1/health reports about the Mac's own sound, which nothing else
    in the stack can see: volume 0 looks exactly like a dropped clip."""

    def test_reads_the_volume_and_the_mute_switch(self):
        out = SimpleNamespace(stdout="output volume:69, input volume:33, alert volume:55, output muted:false")
        with mock.patch("subprocess.run", return_value=out):
            self.assertEqual(speech.system_volume(), (69, False))
        out.stdout = "output volume:0, input volume:33, alert volume:55, output muted:true"
        with mock.patch("subprocess.run", return_value=out):
            self.assertEqual(speech.system_volume(), (0, True))

    def test_survives_osascript_being_unavailable(self):
        for blow_up in (OSError("no osascript"), subprocess.TimeoutExpired("osascript", 1)):
            with mock.patch("subprocess.run", side_effect=blow_up):
                self.assertEqual(speech.system_volume(), (None, None))

    def test_names_the_output_a_clip_would_go_to(self):
        sd = SimpleNamespace(_terminate=lambda: None, _initialize=lambda: None,
                             default=SimpleNamespace(device=(0, 1)),
                             query_devices=lambda i: {"name": "MIDDLETON"})
        with mock.patch.dict("sys.modules", {"sounddevice": sd}):
            self.assertEqual(speech.MacSpeaker().output(), "MIDDLETON")

    def test_an_output_portaudio_cannot_name_is_not_an_error(self):
        def gone(i):
            raise RuntimeError("Error querying device 99")
        sd = SimpleNamespace(_terminate=lambda: None, _initialize=lambda: None,
                             default=SimpleNamespace(device=(0, 99)), query_devices=gone)
        with mock.patch.dict("sys.modules", {"sounddevice": sd}):
            self.assertEqual(speech.MacSpeaker().output(), "")

    def test_health_gets_one_reading_for_a_run_of_polls(self):
        mac = speech.MacSpeaker()
        out = SimpleNamespace(stdout="output volume:44, input volume:33, alert volume:55, output muted:false")
        with mock.patch("subprocess.run", return_value=out) as run, \
             mock.patch.object(speech.MacSpeaker, "output", lambda self: "MIDDLETON"):
            self.assertEqual(mac.sound(), {"volume": 44, "muted": False, "output": "MIDDLETON"})
            for _ in range(5):
                mac.sound()
            self.assertEqual(run.call_count, 1)


if __name__ == "__main__":
    unittest.main()
