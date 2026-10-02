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
import datetime as dt
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

from . import __version__, config, speech, usage
from .bot import EXPRESSIONS, GESTURES, Bot, BotError, discover_stackchan
from .brain import Brain, bubble_text
from .lab import VizMac, Wled
from .memory import Memory, Routines
from .tools import Toolbox

# Sense events the bot reports (POST /v1/event): what to tell Claude, and how
# long to wait before reacting to the same kind again.
EVENTS = {
    "arrival": ("[Event from your camera: movement at the desk after {quiet_min} quiet minutes. The photo is what "
                "your camera sees right now. If a person is in it, greet them in one short sentence. If nobody "
                "is there (an empty room, a screen or light changing), reply with only the word silent.]",
                600, "chime"),
    "lean_in": ("[Event from your proximity sensor: someone just leaned in close to your face. "
                "React in a few words, maybe ask what's up.]", 180, "curious"),
}


class App:
    def __init__(self):
        self.settings = config.load()
        self.clips = speech.ClipStore()
        self.listener = speech.Listener(self.settings["stt_model"])
        self.wled = Wled(self.settings["wled_allow"])
        self.vizmac = VizMac(self.settings["vizmac_url"])
        self._bot: Bot | None = None
        self._bot_lock = threading.Lock()
        self.toolbox = Toolbox(self.get_bot, self.wled, self.vizmac, Memory(), Routines())
        self._event_last: dict[str, float] = {}
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

    def start_turn(self, heard: str, source: str, on_done=None) -> dict:
        """Start a turn and return as soon as the first sentence is ready.

        Claude's reply streams in; each finished sentence is synthesized and
        appended to one live clip that the bot is already playing, so he starts
        talking after the first sentence instead of the whole answer. Later
        sentences and any tool calls keep flowing into the same clip. on_done
        runs when the whole turn (including speech synthesis) is finished.
        """
        t0 = time.time()
        personality = self.personality()
        voice = speech.PERSONALITY_VOICES.get(personality.lower(), self.settings["tts_voice"])
        clip = speech.LiveClip()
        cid = self.clips.add(clip)
        mouth = speech.Mouth(clip, voice, self.settings["tts_rate"])
        st = {"first": None, "t_first": None, "expression": -1, "gesture": None}
        ready = threading.Event()

        def on_tag(kind: str, value: str) -> None:
            if kind == "face" and st["expression"] < 0 and value in EXPRESSIONS:
                st["expression"] = EXPRESSIONS.index(value)
            elif kind == "gesture" and st["gesture"] is None and value in GESTURES:
                st["gesture"] = value
                self.toolbox.run("head_gesture", {"name": value})   # async on the bot

        def on_sentence(text: str) -> None:
            mouth.say(text)
            if st["first"] is None:
                st["first"], st["t_first"] = text, time.time()
                ready.set()

        splitter = speech.SentenceSplitter(on_sentence, on_tag)

        def work() -> None:
            result = {"tools": []}
            try:
                result = self.brain.turn(heard, personality, on_text=splitter.feed)
                splitter.flush()
                if not mouth.spoken:  # offline stand-in or error text: nothing was streamed
                    if st["expression"] < 0:
                        st["expression"] = result.get("expression", -1)
                    on_sentence(result.get("text") or "Hmm.")
            except Exception as e:  # noqa: BLE001
                traceback.print_exc()
                if not mouth.spoken:
                    on_sentence("Something went wrong in my brain.")
                result["tools"] = result.get("tools", []) + [f"error: {e}"]
            finally:
                mouth.close()
                ready.set()
                said = " ".join(mouth.spoken)
                timing = {"first_sentence": int(((st["t_first"] or time.time()) - t0) * 1000),
                          "turn": int((time.time() - t0) * 1000)}
                self.log.appendleft({"at": time.strftime("%H:%M:%S"), "source": source,
                                     "personality": personality, "tools": result.get("tools", []),
                                     "online": self.brain.online, "heard": heard, "text": said,
                                     "timing_ms": timing})
                print(f"[turn] {source}: {heard!r} -> {said!r} tools={result.get('tools')} {timing}")
                if on_done:
                    on_done()

        threading.Thread(target=work, daemon=True).start()
        ready.wait(60)
        first = st["first"] or ""
        return {
            "heard": heard, "text": first, "bubble": bubble_text(first),
            "expression": st["expression"], "audio_id": cid,
            # Bubble-time hint for the bot (the clip's length isn't known yet).
            "audio_ms": max(2500, len(first.split()) * 380),
            "timing_ms": {"first_sentence": int(((st["t_first"] or time.time()) - t0) * 1000)},
        }

    # ---- unprompted speech: events and routines ----------------------------

    def speak_prompt(self, prompt: str, source: str, sound: str | None = None) -> dict | None:
        """Run a turn nobody typed or said (an event or a routine) and have the
        bot speak it. Returns None if a turn is already in progress."""
        if not self.busy.acquire(blocking=False):
            return None
        try:
            bot = self.get_bot()
            if bot and sound:
                threading.Thread(target=lambda: _quiet(bot.sound, sound), daemon=True).start()
            reply = self.start_turn(prompt, source, on_done=self.busy.release)
        except Exception:
            self.busy.release()
            raise
        if bot:
            try:
                bot.play(reply["audio_id"], reply["bubble"], reply["audio_ms"], reply["expression"])
            except BotError as e:
                print(f"[speak] {source}: bot didn't take the clip: {e}")
        return reply

    def handle_event(self, evt: dict) -> str:
        kind = str(evt.get("type", ""))
        if kind not in EVENTS:
            return "ignored (unknown type)"
        template, cooldown, sound = EVENTS[kind]
        now = time.time()
        if now - self._event_last.get(kind, 0) < cooldown:
            return "ignored (cooldown)"
        self._event_last[kind] = now
        prompt = template.format(quiet_min=evt.get("quiet_min", "several"))
        if kind == "arrival":
            # Look before greeting: lights and screens also move.
            threading.Thread(target=self._arrival, args=(prompt, sound), daemon=True).start()
        else:
            threading.Thread(target=self._quick_event, args=(kind, prompt, sound), daemon=True).start()
        return "reacting"

    def _quick_event(self, kind: str, prompt: str, sound: str) -> None:
        """A simple reaction (lean-in): one call to the event model, no tools."""
        if not self.busy.acquire(blocking=False):
            return
        try:
            result = self.brain.quick(prompt, self.personality())
        finally:
            self.busy.release()
        if result.get("text"):
            self._say_text(result["text"], result.get("expression", -1), f"event:{kind}", sound,
                           heard=prompt)

    def _arrival(self, prompt: str, sound: str, tries: int = 3, gap_s: float = 3.0) -> None:
        """Motion fires as someone walks into view, often before they're in
        frame, so an empty photo is retried a couple of times before giving up."""
        bot = self.get_bot()
        for attempt in range(1, tries + 1):
            if attempt > 1:
                time.sleep(gap_s)
            try:
                photo = bot.photo(shutter=False) if bot else None
            except BotError as e:
                print(f"[event] arrival: no photo ({e})")
                photo = None
            if not photo or not self.busy.acquire(blocking=False):
                self._event_last["arrival"] = 0
                return
            try:
                result = self.brain.quick(prompt, self.personality(), images=[photo])
            finally:
                self.busy.release()
            text = (result.get("text") or "").strip()
            if text.lower().strip(" .!") not in ("silent", ""):
                self._say_text(text, result.get("expression", -1), "event:arrival", sound)
                return
            print(f"[event] arrival: nobody in photo {attempt}/{tries}")
        self._event_last["arrival"] = 0     # nobody there: don't use up the cooldown
        self.log.appendleft({"at": time.strftime("%H:%M:%S"), "source": "event:arrival",
                             "heard": f"(photo check x{tries})", "text": "(nobody there, stayed quiet)",
                             "tools": []})

    def _say_text(self, text: str, expression: int, source: str, sound: str | None,
                  heard: str = "(photo check)") -> None:
        """Speak an already-written reply (no model call)."""
        personality = self.personality()
        voice = speech.PERSONALITY_VOICES.get(personality.lower(), self.settings["tts_voice"])
        pcm = speech.synthesize(text, voice, self.settings["tts_rate"])
        cid = self.clips.put(pcm)
        bot = self.get_bot()
        if bot:
            if sound:
                threading.Thread(target=lambda: _quiet(bot.sound, sound), daemon=True).start()
            try:
                bot.play(cid, bubble_text(text), speech.pcm_ms(pcm), expression)
            except BotError as e:
                print(f"[speak] {source}: bot didn't take the clip: {e}")
        self.log.appendleft({"at": time.strftime("%H:%M:%S"), "source": source, "heard": heard,
                             "text": text, "tools": [f"model={self.settings['event_model']}"]})
        print(f"[turn] {source}: -> {text!r}")

    def run_scheduler(self) -> None:
        """Check routines every 20 s. A routine that finds the bot busy is
        retried on the next check (it stays due for 10 minutes)."""
        while True:
            try:
                now = dt.datetime.now()
                for r in self.toolbox.routines.due(now):
                    if self.speak_prompt(f"[Routine {r['id']}, scheduled {r['days']} at {r['time']}: {r['what']}]",
                                         f"routine:{r['id']}", "power_up"):
                        self.toolbox.routines.mark_run(r["id"], now.date().isoformat())
            except Exception:  # noqa: BLE001 - keep the scheduler alive
                traceback.print_exc()
            time.sleep(20)

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
        server_version = f"vizbrain/{__version__}"

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
                    "bot": bot.host if bot else None, "spend": usage.summary(),
                })
            if path == "/v1/log":
                return self._json(200, list(app.log))
            if path.startswith("/v1/audio/"):
                clip = app.clips.get(path.rsplit("/", 1)[-1])
                if clip is None:
                    return self._json(404, {"error": "no such clip"})
                return self._stream_clip(clip)
            if path in ("/", "/index.html"):
                page = resources.files("vizbrain").joinpath("static/index.html").read_bytes()
                return self._send(200, page, "text/html; charset=utf-8")
            self._json(404, {"error": "not found"})

        def _stream_clip(self, clip):
            """Send a clip, chunked, as it grows (a finished clip goes out in one piece)."""
            ctype = f"audio/L16; rate={speech.PLAY_RATE}; channels=1"
            data, done = clip.read_from(0, timeout=0)
            if done:
                return self._send(200, data, ctype)
            self.protocol_version = "HTTP/1.1"   # chunked transfer needs 1.1
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Transfer-Encoding", "chunked")
            self.send_header("Connection", "close")
            self.end_headers()
            pos = 0
            while True:
                if data:
                    self.wfile.write(f"{len(data):X}\r\n".encode() + data + b"\r\n")
                    self.wfile.flush()
                    pos += len(data)
                if done:
                    break
                data, done = clip.read_from(pos, timeout=45)
                if not data and not done:
                    break   # synthesis stalled; end the response rather than hang the bot
            self.wfile.write(b"0\r\n\r\n")
            self.close_connection = True

        def do_POST(self):
            path = self.path.split("?")[0]
            try:
                if path == "/v1/voice":
                    return self._voice()
                if path == "/v1/text":
                    return self._text()
                if path == "/v1/event":
                    evt = json.loads(self._body() or b"{}")
                    outcome = app.handle_event(evt)
                    app.log.appendleft({"at": time.strftime("%H:%M:%S"), "source": "event",
                                        "heard": json.dumps(evt), "text": outcome, "tools": []})
                    print(f"[event] {evt} -> {outcome}")
                    return self._json(202, {"ok": True, "outcome": outcome})
                self._json(404, {"error": "not found"})
            except Exception as e:  # noqa: BLE001
                traceback.print_exc()
                self._json(500, {"error": str(e)})

        def _voice(self):
            app.adopt_bot(self.client_address[0])
            wav = self._body()
            if not app.busy.acquire(blocking=False):
                return self._json(503, {"error": "busy"})
            started = False
            try:
                t0 = time.time()
                samples, _ = speech.wav_to_float(wav)
                samples = speech.normalize(samples)
                heard = app.listener.transcribe(samples) if samples.size > 1600 else ""
                stt_ms = int((time.time() - t0) * 1000)
                reply = app.start_turn(heard, "voice", on_done=app.busy.release)
                started = True
                reply["timing_ms"]["stt"] = stt_ms
                reply["timing_ms"]["audio_in_ms"] = int(samples.size / 16)
                self._json(200, reply)
            finally:
                if not started:
                    app.busy.release()

        def _text(self):
            data = json.loads(self._body() or b"{}")
            heard = str(data.get("text", "")).strip()
            if not heard:
                return self._json(400, {"error": "text required"})
            if not app.busy.acquire(blocking=False):
                return self._json(503, {"error": "busy"})
            try:
                reply = app.start_turn(heard, "text", on_done=app.busy.release)
            except Exception:
                app.busy.release()
                raise
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
    app.toolbox.cached_weather()  # warm the prompt's weather so the first question is one call
    threading.Thread(target=app.run_scheduler, daemon=True).start()
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


def _quiet(fn, *args) -> None:
    try:
        fn(*args)
    except Exception as e:  # noqa: BLE001 - a missed chime is fine
        print(f"[speak] {getattr(fn, '__name__', fn)} failed: {e}")
