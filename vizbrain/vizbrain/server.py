"""vizbrain HTTP API (plan: docs/PLAN-vizbot-brain.md, "Interface contracts").

  POST /v1/voice       16 kHz mono s16 WAV from the bot -> reply JSON
  GET  /v1/audio/<id>  reply clip, raw s16le mono PCM @ 24 kHz
  POST /v1/text        {"text": ...} typed turn; the bot speaks the reply
  POST /v1/event       presence events (Stage 3; logged for now)
  GET  /v1/health      status
  GET  /v1/log         recent turns
  GET  /               typing page
"""

from __future__ import annotations

import collections
import json
import os
import signal
import socket
import subprocess
import threading
import time
import traceback
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from importlib import resources

from . import config, speech
from .bot import Bot, BotError, discover_stackchan
from .brain import Brain, bubble_text
from .lab import VizMac, Wled
from .tools import Toolbox


class App:
    def __init__(self):
        self.settings = config.load()
        self.clips = speech.ClipStore()
        self.listener = speech.Listener(self.settings["stt_model"])
        self.wled = Wled(self.settings["wled_allow"])
        self.vizmac = VizMac(self.settings["vizmac_url"])
        self._bot: Bot | None = None
        self._bot_lock = threading.Lock()
        self.toolbox = Toolbox(self.get_bot, self.wled, self.vizmac)
        self.brain = Brain(self.settings, self.toolbox, config.api_key())
        self.log: collections.deque = collections.deque(maxlen=50)
        self.busy = threading.Lock()
        self._bonjour: subprocess.Popen | None = None

    # ---- bot address ------------------------------------------------------

    def get_bot(self) -> Bot | None:
        with self._bot_lock:
            if self._bot is None:
                host = self.settings["bot_host"] or discover_stackchan()
                if host and host.endswith(".local"):
                    try:  # urllib's own .local lookup can stall ~5 s
                        host = socket.getaddrinfo(host, 80, socket.AF_INET)[0][4][0]
                    except OSError:
                        pass
                if host:
                    self._bot = Bot(host)
                    print(f"[bot] using {host}")
            return self._bot

    def adopt_bot(self, ip: str) -> None:
        """The bot that calls us is the bot we drive (unless pinned in settings)."""
        if self.settings["bot_host"] or ip.startswith("127.") or ip == "::1":
            return
        with self._bot_lock:
            if self._bot is None or self._bot.host != ip:
                self._bot = Bot(ip)
                print(f"[bot] adopted {ip}")

    def personality(self) -> str:
        bot = self.get_bot()
        try:
            return bot.personality() if bot else "Chill"
        except BotError:
            return "Chill"

    # ---- turns -----------------------------------------------------------

    def run_turn(self, heard: str, source: str) -> dict:
        t0 = time.time()
        personality = self.personality()
        result = self.brain.turn(heard, personality)
        t_llm = time.time()
        text = result["text"] or "Hmm."
        voice = speech.PERSONALITY_VOICES.get(personality.lower(), self.settings["tts_voice"])
        pcm = speech.synthesize(text, voice, self.settings["tts_rate"])
        clip = self.clips.put(pcm)
        t_tts = time.time()
        if result.get("gesture"):
            self.toolbox.run("head_gesture", {"name": result["gesture"]})  # async on the bot
        reply = {
            "heard": heard, "text": text, "bubble": bubble_text(text),
            "expression": result.get("expression", -1), "audio_id": clip, "audio_ms": speech.pcm_ms(pcm),
            "timing_ms": {"brain": int((t_llm - t0) * 1000), "tts": int((t_tts - t_llm) * 1000)},
        }
        self.log.appendleft({"at": time.strftime("%H:%M:%S"), "source": source,
                             "personality": personality, "tools": result["tools"],
                             "online": self.brain.online, **reply})
        print(f"[turn] {source}: {heard!r} -> {text!r} tools={result['tools']} {reply['timing_ms']}")
        return reply

    # ---- bonjour ---------------------------------------------------------

    def announce(self, port: int) -> None:
        """Tell the bot where we are (it also looks for _vizbrain._tcp, but the
        ESP32's mDNS query has proven unreliable). Persisted on the bot, so it
        only writes when the address changed."""
        bot = self.get_bot()
        if bot is None:
            return
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                s.connect((bot.host, 80))
                me = f"{s.getsockname()[0]}:{port}"
            status = bot._get("/brain/status")
            if isinstance(status, dict) and status.get("brainHost") != me:
                bot._get("/brain/config", {"host": me})
                print(f"[bot] told {bot.host} the brain is at {me}")
        except (OSError, BotError) as e:
            print(f"[bot] announce failed: {e}")

    def advertise(self, port: int) -> None:
        # Clear registrations orphaned by earlier runs that were killed.
        subprocess.run(["pkill", "-f", "dns-sd -R vizbrain"], capture_output=True)
        try:
            self._bonjour = subprocess.Popen(
                ["dns-sd", "-R", "vizbrain", "_vizbrain._tcp", "local", str(port), "api=/v1"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError as e:
            print(f"[bonjour] not advertised: {e}")

    def stop(self) -> None:
        if self._bonjour:
            self._bonjour.terminate()


def make_handler(app: App):
    class Handler(BaseHTTPRequestHandler):
        server_version = "vizbrain/0.1"

        def log_message(self, fmt, *args):  # quieter default logging
            if "/v1/audio/" not in self.path:
                print(f"[http] {self.client_address[0]} {fmt % args}")

        # -- helpers --
        def _send(self, code: int, body: bytes, ctype: str):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)

        def _json(self, code: int, obj):
            self._send(code, json.dumps(obj).encode(), "application/json")

        def _body(self) -> bytes:
            n = int(self.headers.get("Content-Length") or 0)
            if n > 4 * 1024 * 1024:
                raise ValueError("body too large")
            return self.rfile.read(n) if n else b""

        # -- routes --
        def do_GET(self):
            path = self.path.split("?")[0]
            if path == "/v1/health":
                bot = app._bot
                return self._json(200, {
                    "ok": True, "claude": app.brain.online, "model": app.settings["model"],
                    "stt": app.settings["stt_model"], "tts": "macos-say",
                    "bot": bot.host if bot else None,
                })
            if path == "/v1/log":
                return self._json(200, list(app.log))
            if path.startswith("/v1/audio/"):
                pcm = app.clips.get(path.rsplit("/", 1)[-1])
                if pcm is None:
                    return self._json(404, {"error": "no such clip"})
                return self._send(200, pcm, f"audio/L16; rate={speech.PLAY_RATE}; channels=1")
            if path in ("/", "/index.html"):
                page = resources.files("vizbrain").joinpath("static/index.html").read_bytes()
                return self._send(200, page, "text/html; charset=utf-8")
            self._json(404, {"error": "not found"})

        def do_POST(self):
            path = self.path.split("?")[0]
            try:
                if path == "/v1/voice":
                    return self._voice()
                if path == "/v1/text":
                    return self._text()
                if path == "/v1/event":
                    evt = json.loads(self._body() or b"{}")
                    app.log.appendleft({"at": time.strftime("%H:%M:%S"), "source": "event", "event": evt})
                    return self._json(202, {"ok": True})
                self._json(404, {"error": "not found"})
            except Exception as e:  # noqa: BLE001
                traceback.print_exc()
                self._json(500, {"error": str(e)})

        def _voice(self):
            app.adopt_bot(self.client_address[0])
            wav = self._body()
            if not app.busy.acquire(blocking=False):
                return self._json(503, {"error": "busy"})
            try:
                t0 = time.time()
                samples, _ = speech.wav_to_float(wav)
                samples = speech.normalize(samples)
                heard = app.listener.transcribe(samples) if samples.size > 1600 else ""
                stt_ms = int((time.time() - t0) * 1000)
                reply = app.run_turn(heard, "voice")
                reply["timing_ms"]["stt"] = stt_ms
                reply["timing_ms"]["audio_in_ms"] = int(samples.size / 16)
                self._json(200, reply)
            finally:
                app.busy.release()

        def _text(self):
            data = json.loads(self._body() or b"{}")
            heard = str(data.get("text", "")).strip()
            if not heard:
                return self._json(400, {"error": "text required"})
            if not app.busy.acquire(blocking=False):
                return self._json(503, {"error": "busy"})
            try:
                reply = app.run_turn(heard, "text")
            finally:
                app.busy.release()
            bot = app.get_bot()
            spoke = False
            if bot and data.get("speak", True):
                try:
                    bot.play(reply["audio_id"], reply["bubble"], reply["audio_ms"], reply["expression"])
                    spoke = True
                except BotError:
                    try:  # firmware without voice: show the reply as a bubble
                        bot.say(reply["bubble"], min(10000, reply["audio_ms"] + 1500))
                    except BotError as e:
                        reply["bot_error"] = str(e)
            reply["spoke"] = spoke
            self._json(200, reply)

    return Handler


def serve(port: int | None = None) -> None:
    app = App()
    port = port or app.settings["port"]
    print(f"[vizbrain] Claude {'ONLINE (' + app.settings['model'] + ')' if app.brain.online else 'OFFLINE (no API key; rule-based stand-in)'}")
    threading.Thread(target=app.listener.warm, daemon=True).start()
    threading.Thread(target=app.announce, args=(port,), daemon=True).start()
    threading.Thread(target=app.wled.refresh, daemon=True).start()
    httpd = ThreadingHTTPServer(("0.0.0.0", port), make_handler(app))
    httpd.daemon_threads = True
    app.advertise(port)
    # launchd stops us with SIGTERM: take the Bonjour child down with us.
    signal.signal(signal.SIGTERM, lambda *_: (app.stop(), os._exit(0)))
    print(f"[vizbrain] listening on :{port}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        app.stop()
