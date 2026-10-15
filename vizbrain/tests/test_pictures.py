"""Album covers on the bot's screen: which links are fetched, how they're
shrunk for the bot, how they're sent, and when a turn shows one.
Run: python -m unittest discover -s tests"""

import io
import struct
import threading
import unittest
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from types import SimpleNamespace
from unittest import mock

from vizbrain import pictures
from vizbrain.bot import ART_FIRMWARE, Bot, firmware_ok

from fakes import BrainTest, FakeClaude, make_brain, reply, text, tool_use
from test_personal import fake_vizmac, fake_wled
from vizbrain import tools


def png(w=300, h=300) -> bytes:
    rows = b"".join(b"\x00" + bytes(v for x in range(w) for v in ((x * 7) % 256, (y * 5) % 256, 128))
                    for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def jpeg_size(data: bytes) -> tuple[int, int, int]:
    """(marker, width, height) of the frame header: 0xC0 is baseline, 0xC2 progressive."""
    i = 2
    while i < len(data):
        marker, length = data[i + 1], struct.unpack(">H", data[i + 2:i + 4])[0]
        if marker in (0xC0, 0xC1, 0xC2):
            h, w = struct.unpack(">HH", data[i + 5:i + 9])
            return marker, w, h
        i += 2 + length
    raise ValueError("no frame header")


class CoverLinks(unittest.TestCase):
    def test_a_spotify_cover_is_found(self):
        out = "Playing on Mac: Israelites by Desmond Dekker (0:20 of 2:35). Cover: https://i.scdn.co/image/ab67 URI: spotify:track:x"
        self.assertEqual(pictures.cover_url(out), "https://i.scdn.co/image/ab67")
        self.assertEqual(pictures.cover_url("Playing X. Cover: https://mosaic.scdn.co/640/abc."), "https://mosaic.scdn.co/640/abc")

    def test_other_hosts_plain_http_and_no_cover_are_ignored(self):
        for out in ("Cover: https://evil.example/x.jpg", "Cover: http://i.scdn.co/image/ab", "Playing X.", "",
                    "Cover: https://i.scdn.co.evil.example/x"):
            self.assertIsNone(pictures.cover_url(out), out)


class ForBot(unittest.TestCase):
    def test_any_picture_becomes_a_small_240_baseline_jpeg(self):
        jpeg = pictures.for_bot(png(300, 300))
        self.assertEqual(jpeg[:2], b"\xff\xd8")
        self.assertEqual(jpeg_size(jpeg), (0xC0, 240, 240))   # the bot's decoder can't do progressive
        self.assertLessEqual(len(jpeg), pictures.MAX_FOR_BOT)


class Upload(unittest.TestCase):
    def test_art_posts_one_jpeg_file_with_the_time(self):
        got = {}

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_POST(self):
                got["path"] = self.path
                got["type"] = self.headers["Content-Type"]
                got["body"] = self.rfile.read(int(self.headers["Content-Length"]))
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b'{"ok":true}')

        httpd = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        threading.Thread(target=httpd.serve_forever, daemon=True).start()
        self.addCleanup(httpd.server_close)
        self.addCleanup(httpd.shutdown)
        Bot(f"127.0.0.1:{httpd.server_address[1]}").art(b"\xff\xd8JPEGDATA\xff\xd9", ms=8000)
        self.assertEqual(got["path"], "/bot/art?ms=8000")
        self.assertTrue(got["type"].startswith("multipart/form-data; boundary="))
        self.assertIn(b'filename="art.jpg"\r\nContent-Type: image/jpeg\r\n\r\n\xff\xd8JPEGDATA\xff\xd9\r\n--', got["body"])

    def test_the_bot_firmware_gate(self):
        self.assertTrue(firmware_ok("3.7.34", ART_FIRMWARE))
        self.assertFalse(firmware_ok("3.7.33", ART_FIRMWARE))
        self.assertFalse(firmware_ok(None, ART_FIRMWARE))


class Shower(unittest.TestCase):
    def setUp(self):
        self.sent = []
        self.bot = SimpleNamespace(art=lambda jpeg, ms: self.sent.append((jpeg, ms)))
        self.firmware = "3.7.34"
        self.shower = pictures.Shower(lambda: self.bot, lambda: self.firmware)
        patches = (mock.patch.object(pictures, "fetch", lambda url: b"raw:" + url.encode()),
                   mock.patch.object(pictures, "for_bot", lambda raw: b"jpeg:" + raw),
                   mock.patch("sys.stdout", io.StringIO()))
        for p in patches:
            p.start()
            self.addCleanup(p.stop)

    def test_shows_once_then_skips_a_repeat(self):
        self.shower.show("https://i.scdn.co/a")
        self.shower.show("https://i.scdn.co/a")
        self.shower.show("https://i.scdn.co/b")
        self.assertEqual(self.sent, [(b"jpeg:raw:https://i.scdn.co/a", 8000), (b"jpeg:raw:https://i.scdn.co/b", 8000)])

    def test_again_shows_a_repeat_for_a_new_song_on_the_same_album(self):
        self.shower.show("https://i.scdn.co/a")
        self.shower.show("https://i.scdn.co/a", again=True)
        self.assertEqual(len(self.sent), 2)

    def test_only_spotify_https_covers_are_fetched(self):
        self.shower.show("https://evil.example/a.jpg", again=True)
        self.shower.show("http://i.scdn.co/a", again=True)
        self.assertEqual(self.sent, [])

    def test_old_firmware_or_no_bot_shows_nothing(self):
        self.firmware = "3.7.33"
        self.shower.show("https://i.scdn.co/a")
        self.firmware, self.bot = "3.7.34", None
        self.shower.show("https://i.scdn.co/a")
        self.assertEqual(self.sent, [])

    def test_a_failure_never_raises(self):
        self.bot = SimpleNamespace(art=mock.Mock(side_effect=OSError("bot gone")))
        self.shower.show("https://i.scdn.co/a")   # no exception


class Hub:
    def __init__(self, answer):
        self.answer = answer

    def api_list(self):
        return [{"name": "spotify__now_playing", "description": "Now playing.", "input_schema": {"type": "object"}}]

    def owns(self, name):
        return name == "spotify__now_playing"

    def reads(self, name):
        return True if self.owns(name) else None

    def replaced(self):
        return set()

    def run(self, name, args, timeout=None):
        return self.answer


class SongChanges(unittest.TestCase):
    def app(self):
        from vizbrain import server
        app = object.__new__(server.App)
        app.busy = threading.Lock()
        app.shown = []
        app.pictures = SimpleNamespace(show=lambda url, again=False: app.shown.append((url, again)))
        return app

    def test_a_new_song_flashes_its_cover_even_if_repeated(self):
        app = self.app()
        with mock.patch("sys.stdout", io.StringIO()):
            app.on_track({"title": "Ceremony", "artist": "New Order", "cover": "https://i.scdn.co/c", "connector": "spotify"})
        for _ in range(50):
            if app.shown:
                break
            threading.Event().wait(0.01)
        self.assertEqual(app.shown, [("https://i.scdn.co/c", True)])

    def test_not_while_he_is_talking_with_someone(self):
        app = self.app()
        app.busy.acquire()
        app.on_track({"title": "x", "artist": "y", "cover": "https://i.scdn.co/c", "connector": "spotify"})
        threading.Event().wait(0.05)
        self.assertEqual(app.shown, [])


class FromATurn(BrainTest):
    def turn(self, answer, *replies):
        shown = []
        box = tools.Toolbox(lambda: None, fake_wled(), fake_vizmac(),
                            memory=SimpleNamespace(prompt_text=lambda: ""),
                            routines=SimpleNamespace(prompt_text=lambda: ""), connectors=Hub(answer))
        b = make_brain(FakeClaude(*replies), box)
        b.on_picture = shown.append
        b.turn("what's this song?")
        return shown

    def test_a_connector_answer_with_a_cover_shows_it(self):
        shown = self.turn(("[data from the spotify connector] Playing: Israelites. Cover: https://i.scdn.co/image/ab URI: x", False),
                          reply(text("Let me check."), tool_use("spotify__now_playing")), reply(text("Israelites.")))
        self.assertEqual(shown, ["https://i.scdn.co/image/ab"])

    def test_an_error_or_a_cover_less_answer_shows_nothing(self):
        for answer in (("error: Cover: https://i.scdn.co/image/ab", True), ("Nothing is playing.", False)):
            shown = self.turn(answer, reply(text("Let me check."), tool_use("spotify__now_playing")), reply(text("Hmm.")))
            self.assertEqual(shown, [], answer)


if __name__ == "__main__":
    unittest.main()
