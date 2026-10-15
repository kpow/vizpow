"""vizbrain HTTP API (plan: docs/PLAN-vizbot-brain.md, "Interface contracts").

  POST /v1/voice       16 kHz mono s16 WAV from the bot -> reply JSON
  GET  /v1/audio/<id>  reply clip, raw s16le mono PCM @ 24 kHz
  GET  /v1/listen/<id> after playing a clip: {"listen": true} when the reply asked a question
  GET  /v1/voices      the voice picker's choices and the current one (the bot's page, via /brain/voices)
  POST /v1/voices      {"id": ...} switch voice; he says a sample line in it
  POST /v1/text        {"text": ...} typed turn; the bot speaks the reply
  POST /v1/event       presence events (Stage 3; logged for now)
  POST /v1/say         {"text": ..., "voice"?: ...} speak exact text, no model call
  GET  /v1/health      status
  GET  /v1/log         recent turns
  GET  /               typing page
  UDP  :4051           the bot's idle mic stream; "Hey vizBot" starts a turn (wake.py)
"""

from __future__ import annotations

import collections
import datetime as dt
import hmac
import ipaddress
import json
import os
import re
import signal
import socket
import stat
import sys
import subprocess
import threading
import time
import traceback
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from importlib import resources

from . import __version__, config, speech, usage
from .bot import EXPRESSIONS, GESTURES, Bot, BotError, discover_stackchan, firmware_ok
from .brain import Brain, bubble_text
from .lab import VizMac, Wled
from .memory import Memory, Routines
from .tools import Toolbox
from .wake import WakeDetector

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

# Routes for the person at this Mac: the typing page, the log, status and say.
# They spend the API key and show what was said, so they are served to
# loopback only. The bot's routes (/v1/voice, /v1/event, /v1/audio/<id>) are
# reached over the LAN, and once a bot is paired they need its token.
LOCAL_ROUTES = ("/", "/index.html", "/v1/text", "/v1/say", "/v1/log", "/v1/health")
MAX_JSON_BODY = 64 * 1024
# Answers in a row the bot listens for without a tap. A follow-up recording
# says so with the X-Viz-Follow-Up header.
MAX_FOLLOW_UPS = 3
MAX_TEXT_CHARS = 2000


NOT_PAIRED = "not paired with this brain"


def refusal(method: str, path: str, client_ip: str, headers, port: int,
            token: str | None = None) -> str | None:
    """Why this request is refused, or None to serve it.

    token: the pairing token the bot's routes require (None until a bot is
    paired, which leaves them open to the LAN as they were before pairing).

    Beyond loopback-only, the local routes check Host (a rebound DNS name
    arrives with the attacker's hostname), Origin (another site's page), and
    require a JSON Content-Type on POST: that makes a cross-site fetch need a
    CORS preflight, which this server never answers.
    """
    origin = headers.get("Origin")
    if path not in LOCAL_ROUTES:
        # The bot isn't a browser: anything carrying Origin here is a web page.
        if origin:
            return "browser requests are not allowed on this route"
        if token and not hmac.compare_digest(headers.get("X-Viz-Token") or "", token):
            return NOT_PAIRED
        return None
    try:
        local = ipaddress.ip_address(client_ip).is_loopback
    except ValueError:
        local = False
    if not local:
        return "only available on the Mac that runs vizbrain"
    names = {f"localhost:{port}", f"127.0.0.1:{port}", f"[::1]:{port}"}
    if (headers.get("Host") or "").lower() not in names:
        return "unexpected Host header"
    if origin and origin.lower() not in {f"http://{n}" for n in names}:
        return "cross-site request"
    if method == "POST":
        ctype = (headers.get("Content-Type") or "").split(";")[0].strip().lower()
        if ctype != "application/json":
            return "Content-Type must be application/json"
    return None


class App:
    def __init__(self):
        self.settings = config.load()
        self.clips = speech.ClipStore()
        self.listener = speech.Listener(self.settings["stt_model"])
        self.wled = Wled(self.settings["wled_allow"])
        self.vizmac = VizMac(self.settings["vizmac_url"])
        self._bot: Bot | None = None
        self._bot_lock = threading.Lock()
        self._bot_miss = 0.0            # when discovery last found nothing
        self.pairing = config.pairing()
        self._announce_fails = 0
        self._announce_err = ""
        self.started = time.time()
        # What the announce loop last learned about the bot, for /v1/health.
        self.bot_info: dict = {"firmware": None, "seen": 0.0, "error": "", "brain_addr": ""}
        # Connectors: MCP servers from connectors.json. With none listed there is
        # no hub at all, and the MCP library is never loaded.
        conns, self.connector_problems = config.connectors()
        for problem in self.connector_problems:
            print(f"[connectors] {problem}")
        self.connectors = None
        if conns:
            from .connectors import Hub
            from .install import LOG_DIR
            self.connectors = Hub(conns, log_dir=LOG_DIR)
        self.toolbox = Toolbox(self.get_bot, self.wled, self.vizmac, Memory(), Routines(),
                               config.location(self.settings), connectors=self.connectors)
        self._event_last: dict[str, float] = {}
        self.brain = Brain(self.settings, self.toolbox, config.api_key())
        # Album covers from music connectors go on his screen (fw 3.7.34+).
        from .pictures import Shower
        self.pictures = Shower(self.get_bot, lambda: self.bot_info.get("firmware"))
        self.brain.on_picture = lambda url: threading.Thread(target=self.pictures.show, args=(url,),
                                                              daemon=True).start()
        if self.connectors:
            self.connectors.on_track = self.on_track
        self.log: collections.deque = collections.deque(maxlen=50)
        self.busy = threading.Lock()
        self.follow_ups = 0             # follow-up answers in a row (see MAX_FOLLOW_UPS)
        self.last_said = ""             # his last spoken reply: context for hearing the answer to it
        self._bonjour: subprocess.Popen | None = None
        self.wake = WakeDetector(self.on_wake, threshold=self.settings["wake_threshold"])
        self.wake.verbose = bool(self.settings["debug_log"])

    def on_track(self, song: dict) -> None:
        """A new song started (a music connector's now-playing): flash its cover,
        like vizSpot's Now Playing. Not while he's talking with someone."""
        if self.busy.locked():
            return
        print(f"[art] new song from {song['connector']}")
        threading.Thread(target=self.pictures.show, args=(song["cover"],), kwargs={"again": True},
                         daemon=True).start()

    # ---- bot address ------------------------------------------------------

    @property
    def bot_token(self) -> str | None:
        """The token the bot's routes require, once a bot holds it."""
        return self.pairing["token"] if self.pairing["paired"] else None

    def get_bot(self) -> Bot | None:
        with self._bot_lock:
            # A browse takes 2.5 s under this lock, so a miss is kept for 30 s.
            if self._bot is None and time.time() - self._bot_miss > 30:
                host = self.settings["bot_host"] or discover_stackchan()
                if host and host.endswith(".local"):
                    try:  # urllib's own .local lookup can stall ~5 s
                        host = socket.getaddrinfo(host, 80, socket.AF_INET)[0][4][0]
                    except OSError:
                        pass
                if host:
                    self._bot = Bot(host)
                    print(f"[bot] using {host}")
                else:
                    self._bot_miss = time.time()
            return self._bot

    def adopt_bot(self, ip: str, trusted: bool = False) -> None:
        """Follow the bot to the address it calls from (unless pinned in
        settings). trusted: the call carried the pairing token. Without it a
        caller is only taken when no bot is known, never in place of one."""
        if self.settings["bot_host"] or ip.startswith("127.") or ip == "::1":
            return
        with self._bot_lock:
            if self._bot is None or (trusted and self._bot.host != ip):
                self._bot = Bot(ip)
                print(f"[bot] adopted {ip}")

    def on_wake(self, host: str, score: float) -> None:
        """The bot's mic stream said "Hey vizBot": have that bot start a turn."""
        bot = self._bot
        if bot is None or bot.host != host:
            return   # UDP carries no token: only the bot we already drive counts
        print(f"[wake] Hey vizBot from {host} (score {score:.2f})")

        def go() -> None:
            try:
                Bot(host)._get("/brain/wake")
            except BotError as e:   # busy (409) or offline: nothing to do
                print(f"[wake] {e}")
        threading.Thread(target=go, daemon=True).start()

    def stt_hints(self) -> list[str]:
        """Words for the transcriber to expect: the user's name, the gear that's
        here, then the names each connector says to expect (refreshed every minute)."""
        gone = self.toolbox.unavailable()
        base = [self.brain.user, *(["WLED"] if "wled_set" not in gone else []),
                *(["vizMac"] if "keyboard_effect" not in gone else [])]
        if not self.connectors:
            return base
        from .connectors import fit_hints
        return fit_hints(base, self.connectors.vocabulary())

    def personality(self) -> str:
        bot = self.get_bot()
        try:
            return bot.personality() if bot else "Chill"
        except BotError:
            return "Chill"

    # ---- turns -----------------------------------------------------------

    def start_turn(self, heard: str, source: str, on_done=None, may_listen: bool = False) -> dict:
        """Start a turn and return as soon as the first sentence is ready.

        Claude's reply streams in; each finished sentence is synthesized and
        appended to one live clip that the bot is already playing, so he starts
        talking after the first sentence instead of the whole answer. Later
        sentences and any tool calls keep flowing into the same clip. on_done
        runs when the whole turn (including speech synthesis) is finished.
        may_listen: if the reply ends in a question, mark the clip so the bot
        listens for the answer without a tap.
        """
        t0 = time.time()
        personality = self.personality()
        voice = speech.voice_for(self.settings)
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
                said = " ".join(mouth.spoken)
                clip.listen = may_listen and asks_back(said)   # before close: the bot asks once the clip ends
                self.last_said = said
                mouth.close()
                ready.set()
                timing = {"first_sentence": int(((st["t_first"] or time.time()) - t0) * 1000),
                          "turn": int((time.time() - t0) * 1000)}
                self.log.appendleft({"at": time.strftime("%H:%M:%S"), "source": source,
                                     "personality": personality, "tools": result.get("tools", []),
                                     "online": self.brain.online, "heard": heard, "text": said,
                                     "timing_ms": timing, "listen": clip.listen})
                if self.settings["debug_log"]:
                    print(f"[turn] {source}: {heard!r} -> {said!r} tools={result.get('tools')} {timing}")
                else:   # the log file keeps no words, only how the turn went
                    names = [t.split("(")[0].split(" ->")[0][:40] for t in result.get("tools", [])]
                    print(f"[turn] {source}: heard {len(heard.split())} words -> said {len(said.split())} words "
                          f"tools={names} {timing}")
                if on_done:
                    on_done()

        threading.Thread(target=work, daemon=True).start()
        ready.wait(40)   # answer before the bot's own 45 s limit; the clip keeps filling
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
        if self.brain.capped():
            return "ignored (spend cap)"
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
                  heard: str = "(photo check)", voice: str | None = None) -> None:
        """Speak an already-written reply (no model call)."""
        voice = voice or speech.voice_for(self.settings)
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
        print(f"[turn] {source}: -> {text!r}" if self.settings["debug_log"] else
              f"[turn] {source}: said {len(text.split())} words")

    def run_scheduler(self) -> None:
        """Check routines every 20 s. A routine that finds the bot busy is
        retried on the next check (it stays due for 10 minutes)."""
        while True:
            try:
                now = dt.datetime.now()
                for r in self.toolbox.routines.due(now):
                    if self.brain.capped():
                        break   # over the spend cap: routines wait rather than announce it
                    if self.speak_prompt(f"[Routine {r['id']}, scheduled {r['days']} at {r['time']}: {r['what']}]",
                                         f"routine:{r['id']}", "power_up"):
                        self.toolbox.routines.mark_run(r["id"], now.date().isoformat())
            except Exception:  # noqa: BLE001 - keep the scheduler alive
                traceback.print_exc()
            time.sleep(20)

    # ---- bonjour ---------------------------------------------------------

    def announce(self, port: int) -> None:
        """Keep the bot pointed at us and paired (it also looks for
        _vizbrain._tcp, but the ESP32's mDNS query has proven unreliable).
        Sent again every minute because either address can change; the bot
        persists what it is told, so this only writes when something differs."""
        bot = self.get_bot()
        if bot is None:
            return
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                s.connect((bot.host, 80))
                me = f"{s.getsockname()[0]}:{port}"
            status = bot._get("/brain/status")
            if not isinstance(status, dict):
                raise BotError("/brain/status: not a voice build")
            self.bot_info.update(firmware=bot.state().get("firmwareVersion"), seen=time.time(),
                                 error="", brain_addr=me)
            want = {}
            if status.get("brainHost") != me:
                want["host"] = me
            token = self.pairing["token"]
            pair_id = token.split(".")[0]
            can_pair = "pairId" in status          # firmware 3.7.31 and later
            if can_pair and status["pairId"] != pair_id:
                want["token"] = token
            # Head-tap bots shouldn't stream their mic to a brain that isn't listening.
            wake_on = bool(self.settings["wake_enabled"] and self.wake.available)
            if isinstance(status.get("wake"), dict) and status["wake"].get("enabled") != wake_on:
                want["wake"] = int(wake_on)
            if want:
                status = bot._get("/brain/config", want)
                print(f"[bot] told {bot.host}: " + ", ".join(
                    f"{k}={'(pairing token)' if k == 'token' else v}" for k, v in want.items()))
            if (can_pair and not self.pairing["paired"] and isinstance(status, dict)
                    and status.get("pairId") == pair_id):
                self.pairing["paired"] = True
                config.save_pairing(self.pairing)
                print(f"[bot] paired with {bot.host}; the bot routes now need its token")
            self._announce_fails, self._announce_err = 0, ""
        except (OSError, BotError) as e:
            self._announce_fails += 1
            self.bot_info["error"] = str(e)[:160]
            if str(e) != self._announce_err:     # once per kind of failure, not once a minute
                self._announce_err = str(e)
                print(f"[bot] announce failed: {e}")
            if self._announce_fails >= 3 and not self.settings["bot_host"]:
                with self._bot_lock:             # gone or moved: look for it again
                    if self._bot is bot:
                        self._bot = None

    def run_announce(self, port: int) -> None:
        while True:
            try:
                self.announce(port)
            except Exception:  # noqa: BLE001 - keep announcing
                traceback.print_exc()
            time.sleep(60)

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
        if self.connectors:
            self.connectors.stop()


def _voice_label(settings: dict) -> str:
    from . import voices
    return voices.current(settings)["label"]


def asks_back(said: str) -> bool:
    """Whether a reply asks something near its end, so an answer is expected.
    The last two sentences count: "How about some rocksteady? Or the Phish
    shows could hit the spot." still wants an answer. A wrong guess only
    costs a few seconds of listening that end quietly."""
    sentences = [s for s in re.split(r"(?<=[.!?])\s+", said.strip()) if s.strip()]
    return any(s.rstrip(" \"'”’)").endswith("?") for s in sentences[-2:])


def make_handler(app: App):
    class Handler(BaseHTTPRequestHandler):
        server_version = f"vizbrain/{__version__}"

        def log_message(self, fmt, *args):  # quieter default logging
            # Clip fetches, and the typing page polling every 4 s, would bury everything else.
            if not self.path.startswith(("/v1/audio/", "/v1/listen/", "/v1/health", "/v1/log")):
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

        def _body(self, limit: int = 4 * 1024 * 1024) -> bytes:
            n = int(self.headers.get("Content-Length") or 0)
            if n > limit:
                raise ValueError("body too large")
            return self.rfile.read(n) if n else b""

        def _json_body(self) -> dict:
            data = json.loads(self._body(MAX_JSON_BODY) or b"{}")
            if not isinstance(data, dict):
                raise ValueError("expected a JSON object")
            return data

        def _refused(self, method: str, path: str) -> bool:
            why = refusal(method, path, self.client_address[0], self.headers,
                          self.server.server_address[1], app.bot_token)
            if why == NOT_PAIRED:   # "say" is shown by the bot as his bubble
                self._json(403, {"error": why, "say": "I'm not paired with this Mac yet. Try me again in a minute."})
            elif why:
                self._json(403, {"error": why})
            return bool(why)

        # -- routes --
        def do_GET(self):
            path = self.path.split("?")[0]
            if self._refused("GET", path):
                return
            if path == "/v1/health":
                bot = app._bot
                info = app.bot_info
                return self._json(200, {
                    "ok": True, "version": __version__, "uptime_s": int(time.time() - app.started),
                    "claude": app.brain.online, "key": app.brain.key_state, "model": app.settings["model"],
                    "stt": app.settings["stt_model"], "ears": app.listener.state,
                    "tts": app.settings.get("tts_engine", "kokoro"),
                    "voice": speech.voice_for(app.settings),
                    "voice_label": _voice_label(app.settings),
                    "bot": bot.host if bot else None,
                    "bot_firmware": info["firmware"], "bot_firmware_ok": firmware_ok(info["firmware"]),
                    "bot_seen_s_ago": int(time.time() - info["seen"]) if info["seen"] else None,
                    "bot_error": info["error"], "brain_addr": info["brain_addr"],
                    "last_error": app.brain.last_error,
                    "spend": usage.summary(),
                    "capped": app.brain.capped(), "paired": app.pairing["paired"],
                    "wake": {"model": app.wake.available, "threshold": app.wake.threshold,
                             "wakes": app.wake.wakes, "packets": app.wake.receiver.packets},
                    "connectors": app.connectors.status() if app.connectors else [],
                    "connector_problems": app.connector_problems,
                })
            if path == "/v1/log":
                return self._json(200, list(app.log))
            if path == "/v1/voices":
                from . import voices
                return self._json(200, voices.listing(app.settings))
            if path.startswith("/v1/listen/"):
                clip = app.clips.get(path.rsplit("/", 1)[-1])
                return self._json(200, {"listen": bool(clip and clip.done and clip.listen)})
            if path.startswith("/v1/audio/"):
                clip = app.clips.get(path.rsplit("/", 1)[-1])
                if clip is None:
                    return self._json(404, {"error": "no such clip"})
                try:
                    return self._stream_clip(clip)
                except (BrokenPipeError, ConnectionResetError):
                    return print("[http] the bot stopped listening to a clip (cancelled)")
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
            if self._refused("POST", path):
                return
            try:
                if path == "/v1/voice":
                    return self._voice()
                if path == "/v1/text":
                    return self._text()
                if path == "/v1/event":
                    evt = self._json_body()
                    outcome = app.handle_event(evt)
                    app.log.appendleft({"at": time.strftime("%H:%M:%S"), "source": "event",
                                        "heard": json.dumps(evt), "text": outcome, "tools": []})
                    print(f"[event] {evt} -> {outcome}")
                    return self._json(202, {"ok": True, "outcome": outcome})
                if path == "/v1/say":
                    req = self._json_body()
                    text = str(req.get("text") or "").strip()
                    if not text:
                        return self._json(400, {"error": "text required"})
                    if len(text) > MAX_TEXT_CHARS:
                        return self._json(413, {"error": f"text is over {MAX_TEXT_CHARS} characters"})
                    app._say_text(text, -1, "say", None, heard="(say)", voice=req.get("voice"))
                    return self._json(200, {"ok": True})
                if path == "/v1/voices":
                    return self._choose_voice()
                self._json(404, {"error": "not found"})
            except Exception as e:  # noqa: BLE001
                traceback.print_exc()
                self._json(500, {"error": str(e),
                                 "say": "Something went wrong in my brain. Check the vizbrain page on the Mac."})

        def _choose_voice(self):
            """The picker on the bot's page: switch voice and say a sample line in it.
            voice_for() reads the settings on every reply, so no restart."""
            from . import voices
            choice = voices.find(app.settings, str(self._json_body().get("id") or ""))
            if choice is None:
                return self._json(400, {"error": "unknown voice"})
            app.settings["tts_voice"] = choice["spec"]
            config.save(app.settings)
            print(f"[voice] now {choice['label']} ({choice['spec']})")
            threading.Thread(target=app._say_text, args=(voices.SAMPLE, -1, "voice", None),
                             kwargs={"heard": "(voice picked)", "voice": choice["spec"]}, daemon=True).start()
            return self._json(200, {"ok": True, "current": choice["id"], "current_label": choice["label"]})

        def _voice(self):
            app.adopt_bot(self.client_address[0], trusted=app.bot_token is not None)
            wav = self._body()
            ears = app.listener.state     # "say" is shown by the bot as his bubble
            if ears == "loading":
                return self._json(503, {"error": "speech model still loading",
                                        "say": "I'm still getting my ears ready. Try me again in a minute."})
            if ears == "failed":
                return self._json(503, {"error": f"speech model failed: {app.listener.error}",
                                        "say": "My hearing isn't working. On the Mac, run vizbrain doctor."})
            if not app.busy.acquire(blocking=False):
                return self._json(503, {"error": "busy"})
            follow_up = self.headers.get("X-Viz-Follow-Up") == "1"
            app.follow_ups = app.follow_ups + 1 if follow_up else 0
            started = False
            try:
                t0 = time.time()
                samples, _ = speech.wav_to_float(wav)
                samples = speech.normalize(samples)
                context = app.last_said if follow_up else ""   # the question this answers
                heard = app.listener.transcribe(samples, app.stt_hints(), context) if samples.size > 1600 else ""
                stt_ms = int((time.time() - t0) * 1000)
                if follow_up and len(heard.split()) == 0:
                    # Noise where an answer was expected: end quietly rather
                    # than have him ask "say that again?" into an empty room.
                    return self._json(200, {"heard": "", "text": "", "bubble": "", "expression": -1,
                                            "audio_id": "", "audio_ms": 0, "timing_ms": {"stt": stt_ms}})
                reply = app.start_turn(heard, "voice", on_done=app.busy.release,
                                       may_listen=app.follow_ups < MAX_FOLLOW_UPS)
                started = True
                reply["timing_ms"]["stt"] = stt_ms
                reply["timing_ms"]["audio_in_ms"] = int(samples.size / 16)
                self._json(200, reply)
            finally:
                if not started:
                    app.busy.release()

        def _text(self):
            data = self._json_body()
            heard = str(data.get("text", "")).strip()
            if not heard:
                return self._json(400, {"error": "text required"})
            if len(heard) > MAX_TEXT_CHARS:
                return self._json(413, {"error": f"text is over {MAX_TEXT_CHARS} characters"})
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


def port_holder(port: int) -> str | None:
    """Why this port can't be served (someone else has it), or None when it's free."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        # Same option the HTTP server binds with. Without it, connections the
        # previous copy closed a moment ago (TIME_WAIT, about 30 s) look like a
        # running copy, and a restart quietly exits instead of starting.
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind(("0.0.0.0", port))
            return None
        except OSError:
            pass
    out = subprocess.run(["lsof", "-nP", f"-tiTCP:{port}", "-sTCP:LISTEN"],
                         capture_output=True, text=True).stdout.split()
    pid = out[0] if out else "?"
    cmd = subprocess.run(["ps", "-o", "command=", "-p", pid], capture_output=True, text=True).stdout
    if "vizbrain" in cmd:
        return (f"vizbrain is already running (pid {pid}). To restart it: vizbrain restart. "
                f"To stop it: vizbrain stop.")
    return (f"Port {port} is in use by another app (pid {pid}: {cmd.strip()[:60] or 'unknown'}). "
            f"Quit it, or set \"port\" in settings.json.")


class Stamped:
    """Stdout with the time in front of every line, so the log can be read later."""

    def __init__(self, out):
        self.out, self.fresh = out, True

    def write(self, text: str) -> int:
        for part in text.splitlines(keepends=True):
            if self.fresh:
                self.out.write(time.strftime("%m-%d %H:%M:%S "))
            self.out.write(part)
            self.fresh = part.endswith("\n")
        return len(text)

    def flush(self) -> None:
        self.out.flush()

    def __getattr__(self, name):
        return getattr(self.out, name)


def trim_log(limit: int = 5 * 1024 * 1024) -> None:
    """When stdout is a log file that has grown past the limit (the login
    agent appends to one forever), keep the old one as .1 and start over."""
    try:
        st = os.fstat(1)
        if not stat.S_ISREG(st.st_mode) or st.st_size < limit:
            return
        from .install import LOG_DIR
        log = LOG_DIR / "vizbrain.log"
        if log.stat().st_ino == st.st_ino:
            log.with_suffix(".log.1").write_bytes(log.read_bytes())
            os.ftruncate(1, 0)
    except OSError:
        pass


def serve(port: int | None = None) -> None:
    trim_log()
    sys.stdout = sys.stderr = Stamped(sys.stdout)
    port = port or config.load()["port"]
    taken = port_holder(port)
    if taken:
        # Exit cleanly: the login agent only restarts vizbrain after a crash.
        print(f"[vizbrain] {taken}")
        return
    app = App()
    print(f"[vizbrain] {__version__} starting. Claude "
          f"{'ONLINE (' + app.settings['model'] + ')' if app.brain.online else 'OFFLINE (no API key yet: run vizbrain set-key)'}")
    threading.Thread(target=app.listener.warm, daemon=True).start()
    threading.Thread(target=speech.kokoro.warm, args=(speech.voice_for(app.settings),), daemon=True).start()
    threading.Thread(target=app.run_announce, args=(port,), daemon=True).start()
    threading.Thread(target=app.wled.refresh, daemon=True).start()
    app.toolbox.cached_weather()  # warm the prompt's weather so the first question is one call
    if app.connectors:
        app.connectors.start()    # in the background; their tools appear as each one is ready
        print(f"[connectors] starting {', '.join(app.connectors.live)}")
    threading.Thread(target=app.run_scheduler, daemon=True).start()
    if app.settings["wake_enabled"] and app.wake.available:
        app.wake.start()
        print(f"[wake] listening for 'Hey vizBot' on UDP :{app.wake.receiver.port} "
              f"(threshold {app.wake.threshold})")
    else:
        print(f"[wake] off (enabled={app.settings['wake_enabled']}, model={app.wake.model_path})")
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
