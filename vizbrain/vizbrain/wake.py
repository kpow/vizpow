"""Wake word: the bot's idle mic stream ("Hey vizBot").

While idle, the bot sends 32 ms chunks of 16 kHz mono s16 audio over UDP
(port 4051) whenever the room is louder than its noise floor, with ~1 s of
pre-roll. Packets (see vizbot/voice_client.h, "Wake-word mic stream"):

    "VZW1" | seq u32 | flags u8 | pad[3] | payload

flags: START = first chunk of a burst, END = burst over (no samples),
BEAT = heartbeat every 5 s while quiet (payload: noise RMS u16).

    python -m vizbrain.wake record DIR    save every burst as a WAV in DIR
"""

from __future__ import annotations

import argparse
import socket
import struct
import threading
import time
import wave
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

import numpy as np

PORT = 4051
RATE = 16000
CHUNK = 512
F_START, F_END, F_BEAT = 0x01, 0x02, 0x04
BURST_TIMEOUT_S = 0.6       # no packet for this long ends a burst (a lost END)


@dataclass
class Burst:
    host: str
    started: float
    chunks: list[np.ndarray] = field(default_factory=list)
    last_seq: int = -1
    gaps: int = 0           # chunks lost in transit

    def audio(self) -> np.ndarray:
        return np.concatenate(self.chunks) if self.chunks else np.zeros(0, np.int16)


class WakeReceiver:
    """Reassembles bursts from the bot's stream.

    on_chunk(host, samples) sees every chunk as it arrives (for the detector);
    on_burst(burst) gets each finished burst.
    """

    def __init__(self, on_chunk: Callable[[str, np.ndarray], None] | None = None,
                 on_burst: Callable[[Burst], None] | None = None, port: int = PORT):
        self.on_chunk = on_chunk
        self.on_burst = on_burst
        self.port = port
        self.bursts: dict[str, Burst] = {}
        self.noise: dict[str, int] = {}
        self.last_seen: dict[str, float] = {}
        self.packets = 0
        self._stop = threading.Event()
        self._sock: socket.socket | None = None

    def start(self) -> None:
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind(("0.0.0.0", self.port))
        self._sock.settimeout(0.2)
        threading.Thread(target=self._run, name="wake-rx", daemon=True).start()

    def stop(self) -> None:
        self._stop.set()

    def _finish(self, host: str) -> None:
        b = self.bursts.pop(host, None)
        if b and b.chunks and self.on_burst:
            self.on_burst(b)

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                data, (host, _) = self._sock.recvfrom(2048)
            except socket.timeout:
                data = None
            now = time.monotonic()
            for h, b in list(self.bursts.items()):
                if now - self.last_seen.get(h, now) > BURST_TIMEOUT_S:
                    self._finish(h)
            if not data or len(data) < 12 or data[:4] != b"VZW1":
                continue
            self.packets += 1
            self.last_seen[host] = now
            seq, flags = struct.unpack_from("<IB", data, 4)
            payload = data[12:]
            if flags & F_BEAT:
                if len(payload) >= 2:
                    self.noise[host] = struct.unpack_from("<H", payload)[0]
                continue
            if flags & F_END:
                self._finish(host)
                continue
            samples = np.frombuffer(payload, dtype="<i2")
            b = self.bursts.get(host)
            if b is None or flags & F_START or seq <= b.last_seq:
                if b is not None:
                    self._finish(host)
                b = self.bursts[host] = Burst(host=host, started=time.time())
            elif b.last_seq >= 0 and seq != b.last_seq + 1:
                b.gaps += seq - b.last_seq - 1
            b.last_seq = seq
            b.chunks.append(samples)
            if self.on_chunk:
                self.on_chunk(host, samples)


MODEL_DIR = Path.home() / "Library" / "Application Support" / "vizbrain" / "wakeword" / "models"


class WakeModel:
    """Streaming "Hey vizBot" scorer, one per bot stream.

    Same feature pipeline as openWakeWord's AudioFeatures (melspectrogram ->
    Google speech embedding every 80 ms), then the trained classifier over the
    last 16 embeddings. Only needs onnxruntime.
    """

    STEP = 1280                      # 80 ms

    def __init__(self, model_path: Path):
        import onnxruntime as ort
        opts = ort.SessionOptions()
        opts.inter_op_num_threads = opts.intra_op_num_threads = 1
        mk = lambda p: ort.InferenceSession(str(p), sess_options=opts, providers=["CPUExecutionProvider"])
        self.mel = mk(MODEL_DIR / "melspectrogram.onnx")
        self.emb = mk(MODEL_DIR / "embedding_model.onnx")
        self.clf = mk(model_path)
        self.clf_in = self.clf.get_inputs()[0].name
        self.reset()

    def reset(self) -> None:
        self.raw = np.zeros(0, np.int16)
        self.pending = np.zeros(0, np.int16)
        self.mels = np.ones((76, 32), np.float32)   # openWakeWord starts from ones too
        self.feats = np.zeros((0, 96), np.float32)
        # Warm up on a second of quiet room hiss, so a burst's first words are
        # scored against a full window like the training clips were.
        self.feed(np.random.default_rng().normal(0, 100, 16000).astype(np.int16))

    def feed(self, samples: np.ndarray) -> list[float]:
        """Add audio; returns a score for every 80 ms step completed."""
        self.pending = np.concatenate([self.pending, samples])
        scores = []
        while len(self.pending) >= self.STEP:
            step, self.pending = self.pending[: self.STEP], self.pending[self.STEP:]
            self.raw = np.concatenate([self.raw, step])[-(self.STEP + 480):]
            if len(self.raw) < 400:
                continue
            spec = self.mel.run(None, {"input": self.raw[None, :].astype(np.float32)})[0].squeeze() / 10 + 2
            self.mels = np.vstack([self.mels, spec])[-97:]
            e = self.emb.run(None, {"input_1": self.mels[-76:][None, :, :, None]})[0].reshape(1, 96)
            self.feats = np.vstack([self.feats, e])[-16:]
            if len(self.feats) == 16:
                scores.append(float(self.clf.run(None, {self.clf_in: self.feats[None]})[0].reshape(-1)[0]))
            else:
                scores.append(0.0)
        return scores


class WakeDetector:
    """Feeds each bot's stream through a WakeModel and calls on_wake(host, score)."""

    def __init__(self, on_wake: Callable[[str, float], None], threshold: float = 0.5,
                 model_path: Path = MODEL_DIR / "hey_vizbot.onnx", cooldown_s: float = 3.0):
        self.on_wake = on_wake
        self.threshold = threshold
        self.model_path = model_path
        self.cooldown_s = cooldown_s
        self.models: dict[str, WakeModel] = {}
        self.last_wake: dict[str, float] = {}
        self.peak = 0.0                 # highest score in the current burst (diagnostics)
        self.wakes = 0
        self.receiver = WakeReceiver(on_chunk=self._chunk, on_burst=self._burst_end)

    @property
    def available(self) -> bool:
        return self.model_path.exists()

    def start(self) -> None:
        self.receiver.start()

    def _chunk(self, host: str, samples: np.ndarray) -> None:
        m = self.models.get(host)
        if m is None:
            m = self.models[host] = WakeModel(self.model_path)
        for s in m.feed(samples):
            self.peak = max(self.peak, s)
            now = time.monotonic()
            if s >= self.threshold and now - self.last_wake.get(host, 0) > self.cooldown_s:
                self.last_wake[host] = now
                self.wakes += 1
                self.on_wake(host, s)

    def _burst_end(self, b: Burst) -> None:
        # Each burst starts after a quiet gap: begin from a clean slate.
        print(f"[wake] burst {len(b.audio()) / RATE:.1f} s from {b.host}, peak score {self.peak:.2f}", flush=True)
        self.peak = 0.0
        m = self.models.get(b.host)
        if m:
            m.reset()


def write_wav(path: Path, samples: np.ndarray) -> None:
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(samples.astype("<i2").tobytes())


def _record(out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)

    def saved(b: Burst) -> None:
        a = b.audio()
        name = time.strftime("%H%M%S", time.localtime(b.started)) + f"_{int(b.started * 1000) % 1000:03d}.wav"
        write_wav(out / name, a)
        rms = int(np.sqrt(np.mean(a.astype(np.float64) ** 2))) if len(a) else 0
        print(f"{name}  {len(a) / RATE:5.2f} s  peak {int(np.abs(a).max()):5d}  rms {rms:5d}  "
              f"lost {b.gaps} chunks  (room noise {rx.noise.get(b.host, '?')})", flush=True)

    rx = WakeReceiver(on_burst=saved)
    rx.start()
    print(f"Listening on UDP :{PORT}, saving bursts to {out}. Ctrl-C to stop.", flush=True)
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass


def trim_utterance(a: np.ndarray, max_s: float = 2.2) -> np.ndarray | None:
    """Cut a burst down to the one thing said in it (None if it's not one short utterance)."""
    n = len(a) // CHUNK
    if n < 4:
        return None
    rms = np.sqrt(np.mean(a[: n * CHUNK].astype(np.float64).reshape(n, CHUNK) ** 2, axis=1))
    loud = np.flatnonzero(rms > max(np.percentile(rms, 20) * 3, 150))
    if len(loud) == 0:
        return None
    first, last = loud[0], loud[-1]
    if (last - first + 1) * CHUNK / RATE > max_s:    # more than one "Hey vizBot"
        return None
    s = max(0, (first - 6) * CHUNK)                   # ~0.2 s before
    e = min(len(a), (last + 10) * CHUNK)              # ~0.3 s after
    return a[s:e]


def _guided(out: Path, bot: str, count: int) -> None:
    """Prompt on the bot's screen, keep exactly one clip per prompt."""
    import urllib.parse
    import urllib.request

    def bubble(text: str, ms: int = 4000) -> None:
        q = urllib.parse.urlencode({"text": text, "dur": ms})
        try:
            urllib.request.urlopen(f"http://{bot}/bot/say?{q}", timeout=2).read()
        except OSError as e:
            print(f"  (bubble failed: {e})")
        print(text, flush=True)

    done = threading.Event()
    state: dict = {"burst": None, "since": 0.0}

    def on_burst(b: Burst) -> None:
        if b.started >= state["since"] and state["burst"] is None:
            state["burst"] = b
            done.set()

    out.mkdir(parents=True, exist_ok=True)
    rx = WakeReceiver(on_burst=on_burst)
    rx.start()
    blocks = ["at the desk, normal voice", "from across the room", "quiet / mumbled", "loud"]
    per = max(1, count // len(blocks))
    kept = len(list(out.glob("*.wav")))
    for bi, how in enumerate(blocks):
        for t in range(10, 0, -2):
            bubble(f"Next: {how}. Starting in {t}", 2500)
            time.sleep(2)
        got = 0
        while got < per:
            # Settle: wait for the room to go quiet so the gate is closed.
            t0 = time.monotonic()
            while rx.bursts and time.monotonic() - t0 < 5:
                time.sleep(0.1)
            state["burst"], state["since"] = None, time.time()
            done.clear()
            bubble(f"Say 'Hey vizBot'  ({got + 1}/{per}, {how})", 5000)
            if not done.wait(8):
                bubble("Didn't hear it. Again!", 1500)
                time.sleep(1.5)
                continue
            a = trim_utterance(state["burst"].audio())
            if a is None:
                bubble("Just once, please. Again!", 1500)
                time.sleep(1.5)
                continue
            write_wav(out / f"{bi}_{kept:03d}.wav", a)
            kept += 1
            got += 1
            print(f"  kept {len(a) / RATE:.2f} s, peak {int(np.abs(a).max())}", flush=True)
            time.sleep(0.6)
    bubble(f"All done! {kept} clips. Thank you!", 6000)
    rx.stop()


# Read top to bottom. (True = wake word, False = a sound-alike that must not wake him.)
STUDIO_SCRIPT: list[tuple[bool, str, str]] = [
    (True, "Hey vizBot", "normal voice"),
    (True, "Hey vizBot", "normal voice"),
    (False, "Hey robot", "normal voice"),
    (True, "Hey vizBot", "quiet"),
    (True, "Hey vizBot", "quiet"),
    (False, "Hey, this bot", ""),
    (True, "Hey vizBot", "loud, like he's across the room"),
    (True, "Hey vizBot", "loud, like he's across the room"),
    (False, "Hey Siri", ""),
    (True, "Hey vizBot", "fast"),
    (True, "Hey vizBot", "slow and stretched out"),
    (False, "Hey there", ""),
    (True, "Hey vizBot", "like a question"),
    (True, "Hey vizBot", "annoyed"),
    (False, "Visit the website", ""),
    (True, "Hey vizBot", "cheerful"),
    (True, "Hey vizBot", "mumbled"),
    (False, "Hey buddy", ""),
    (True, "Hey vizBot", "turn your head away"),
    (True, "Hey vizBot", "turn your head away"),
    (False, "His bot is over there", ""),
    (True, "Hey vizBot", "lean back in your chair"),
    (True, "Hey vizBot", "lean back in your chair"),
    (False, "Hey Vince", ""),
    (True, "Hey vizBot", "whisper-ish"),
    (True, "Hey vizBot", "normal voice"),
    (False, "Business", ""),
    (True, "Hey vizBot", "tired"),
    (True, "Hey vizBot", "excited"),
    (False, "Hey, what's up?", ""),
    (True, "Hey vizBot", "normal voice"),
    (True, "Hey vizBot", "quiet"),
    (False, "OK bot", ""),
    (True, "Hey vizBot", "loud"),
    (True, "Hey vizBot", "fast"),
    (False, "Hey, is it hot?", ""),
    (True, "Hey vizBot", "with a hand over your mouth"),
    (True, "Hey vizBot", "normal voice"),
    (False, "Hey everybody", ""),
    (True, "Hey vizBot", "normal voice"),
]

STUDIO_HTML = """<!doctype html><html><head><meta charset="utf-8"><title>vizBot wake studio</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:-apple-system,system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:24px;text-align:center}
#n{color:#888;font-size:18px} #line{font-size:64px;font-weight:700;margin:28px 0 6px}
#how{font-size:24px;color:#9cf;min-height:30px} #tag{font-size:16px;margin-top:8px}
.wake{color:#6d6} .not{color:#e96}
button{font-size:30px;padding:22px 60px;border-radius:16px;border:0;margin:28px 8px 8px;cursor:pointer}
#rec{background:#d33;color:#fff} #rec.armed{background:#555}
#skip,#back{font-size:16px;padding:10px 20px;background:#333;color:#ccc}
#msg{font-size:22px;min-height:32px;margin-top:16px} #next{color:#666;margin-top:26px;font-size:18px}
</style></head><body>
<div id="n"></div><div id="line"></div><div id="how"></div><div id="tag"></div>
<button id="rec" onclick="post('record')">● Record</button><br>
<button id="back" onclick="post('back')">← back</button><button id="skip" onclick="post('skip')">skip →</button>
<div id="msg"></div><div id="next"></div>
<p style="color:#666;font-size:14px;margin-top:40px">Press Record (or the space bar), then read the big line once. It moves on by itself.</p>
<script>
async function post(a){await fetch('/api/'+a,{method:'POST'});tick()}
document.addEventListener('keydown',e=>{if(e.code==='Space'){e.preventDefault();post('record')}});
async function tick(){
 const s=await (await fetch('/api/state')).json();
 document.getElementById('n').textContent=s.done?'':`${s.i+1} of ${s.total}`;
 document.getElementById('line').textContent=s.done?'All done — thank you!':s.text;
 document.getElementById('how').textContent=s.done?`${s.saved} clips saved`:(s.how?'('+s.how+')':'');
 const t=document.getElementById('tag');
 t.textContent=s.done?'':(s.wake?'wake word':'NOT the wake word — just read it');t.className=s.wake?'wake':'not';
 const r=document.getElementById('rec');r.className=s.phase==='armed'?'armed':'';
 r.textContent=s.phase==='armed'?'Listening… say it now':'● Record';
 document.getElementById('msg').textContent=s.msg||'';
 document.getElementById('next').textContent=s.next?'next: '+s.next:'';
}
setInterval(tick,300);tick();
</script></body></html>"""


def _studio(out: Path, port: int) -> None:
    """A local page with a Record button and a script to read; one labelled clip per press."""
    import json as _json
    from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

    out.mkdir(parents=True, exist_ok=True)
    labels = out / "labels.jsonl"
    st = {"i": 0, "phase": "idle", "since": 0.0, "msg": "", "saved": 0}
    lock = threading.Lock()

    def on_burst(b: Burst) -> None:
        with lock:
            if st["phase"] != "armed" or b.started < st["since"] - 0.2 or st["i"] >= len(STUDIO_SCRIPT):
                return
            wake, text, how = STUDIO_SCRIPT[st["i"]]
            a = trim_utterance(b.audio(), max_s=2.2 if wake else 3.0)
            if a is None:
                st["phase"], st["msg"] = "idle", "Heard more than one thing. Press Record and try again."
                return
            name = f"{'pos' if wake else 'neg'}_{st['i']:02d}_{int(time.time())}.wav"
            write_wav(out / name, a)
            with labels.open("a") as f:
                f.write(_json.dumps({"file": name, "wake": wake, "text": text, "how": how}) + "\n")
            st["saved"] += 1
            st["i"] += 1
            st["phase"], st["msg"] = "idle", f"✓ got it ({len(a) / RATE:.1f} s)"
            print(f"saved {name}  {text!r} ({how})", flush=True)

    def watchdog() -> None:
        while True:
            time.sleep(0.2)
            with lock:
                if st["phase"] == "armed" and time.time() - st["since"] > 9 and not rx.bursts:
                    st["phase"], st["msg"] = "idle", "Didn't hear anything. Press Record and try again."

    rx = WakeReceiver(on_burst=on_burst)
    rx.start()
    threading.Thread(target=watchdog, daemon=True).start()

    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def _send(self, body: bytes, ctype: str) -> None:
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path.startswith("/api/state"):
                with lock:
                    i, n = st["i"], len(STUDIO_SCRIPT)
                    cur = STUDIO_SCRIPT[i] if i < n else (False, "", "")
                    nxt = STUDIO_SCRIPT[i + 1] if i + 1 < n else None
                    body = {"i": i, "total": n, "done": i >= n, "wake": cur[0], "text": cur[1], "how": cur[2],
                            "phase": st["phase"], "msg": st["msg"], "saved": st["saved"],
                            "next": f"{nxt[1]}" + (f" ({nxt[2]})" if nxt and nxt[2] else "") if nxt else ""}
                self._send(_json.dumps(body).encode(), "application/json")
            else:
                self._send(STUDIO_HTML.encode(), "text/html; charset=utf-8")

        def do_POST(self):
            with lock:
                if self.path == "/api/record" and st["i"] < len(STUDIO_SCRIPT):
                    st["phase"], st["since"], st["msg"] = "armed", time.time(), ""
                elif self.path == "/api/skip":
                    st["i"] = min(st["i"] + 1, len(STUDIO_SCRIPT))
                    st["phase"], st["msg"] = "idle", ""
                elif self.path == "/api/back":
                    st["i"] = max(st["i"] - 1, 0)
                    st["phase"], st["msg"] = "idle", ""
            self._send(b"{}", "application/json")

    print(f"Studio at http://127.0.0.1:{port}  (clips -> {out})", flush=True)
    ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="vizbrain.wake")
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("record", help="save every burst from the bot as a WAV")
    r.add_argument("out", type=Path)
    g = sub.add_parser("guided", help="prompt on the bot's screen; one 'Hey vizBot' clip per prompt")
    g.add_argument("out", type=Path)
    g.add_argument("--bot", default="10.0.0.93")
    g.add_argument("--count", type=int, default=40)
    s = sub.add_parser("studio", help="a local page with a Record button and a script to read")
    s.add_argument("out", type=Path)
    s.add_argument("--port", type=int, default=4052)
    args = p.parse_args(argv)
    if args.cmd == "studio":
        _studio(args.out, args.port)
    elif args.cmd == "record":
        _record(args.out)
    elif args.cmd == "guided":
        _guided(args.out, args.bot, args.count)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
