"""Local speech: whisper (mlx) for listening; Kokoro (or macOS `say`) for speaking.

Everything runs on the Mac; no audio leaves the LAN (plan decision D3).
Kokoro's model lives in ~/Library/Application Support/vizbrain/kokoro/; without
it, or with settings "tts_engine": "say", the macOS voices are used.
"""

from __future__ import annotations

import io
import os
import queue
import re
import subprocess
import tempfile
import threading
import time
import uuid
import wave
from pathlib import Path

import numpy as np

PLAY_RATE = 24000  # Hz, s16le mono — what the bot's speaker plays

# One voice everywhere (Kevin: the voice doesn't change with the mood).
# "kokoro:<voice>[@speed]" uses Kokoro (local neural TTS); anything else is a macOS `say` voice.
KOKORO_VOICE = "kokoro:am_puck@1.12"
SAY_VOICE = "Junior"   # macOS fallback when Kokoro isn't installed
KOKORO_DIR = Path.home() / "Library" / "Application Support" / "vizbrain" / "kokoro"


def voice_for(settings: dict) -> str:
    """The bot's voice: settings["tts_voice"] if set, else Kokoro's am_puck (or `say` without Kokoro)."""
    if settings.get("tts_voice"):
        return settings["tts_voice"]
    if settings.get("tts_engine", "kokoro") == "kokoro" and (KOKORO_DIR / "kokoro-v1.0.onnx").exists():
        return KOKORO_VOICE
    return SAY_VOICE


class _Kokoro:
    """Kokoro-82M via onnxruntime on the CPU (~3.8x real time on an M5; the
    Neural Engine and the int8 model were both slower). One shared session."""

    def __init__(self):
        self._k = None
        self._lock = threading.Lock()

    def _load(self):
        if self._k is None:
            from kokoro_onnx import Kokoro
            self._k = Kokoro(str(KOKORO_DIR / "kokoro-v1.0.onnx"), str(KOKORO_DIR / "voices-v1.0.bin"))
        return self._k

    def warm(self) -> None:
        try:
            self.speak("Ready.", "am_puck", 1.12)
            print("[tts] kokoro ready")
        except Exception as e:  # noqa: BLE001
            print(f"[tts] kokoro unavailable: {e}")

    def speak(self, text: str, voice: str, speed: float) -> bytes:
        with self._lock:
            samples, rate = self._load().create(text, voice=voice, speed=speed, lang="en-us")
        if rate != PLAY_RATE:
            n = int(len(samples) * PLAY_RATE / rate)
            samples = np.interp(np.linspace(0, len(samples), n, endpoint=False), np.arange(len(samples)), samples)
        return (np.clip(samples, -1.0, 1.0) * 32767).astype("<i2").tobytes()


kokoro = _Kokoro()


class Listener:
    def __init__(self, model: str):
        self.model = model
        self._lock = threading.Lock()
        self._ready = False

    def warm(self) -> None:
        """Load the model once (first call downloads it from Hugging Face)."""
        try:
            self.transcribe(np.zeros(8000, dtype=np.float32))
            self._ready = True
            print(f"[stt] {self.model} ready")
        except Exception as e:  # noqa: BLE001 - report and keep serving
            print(f"[stt] warm-up failed: {e}")

    def transcribe(self, samples: np.ndarray) -> str:
        import mlx_whisper  # heavy import, deferred

        with self._lock:
            out = mlx_whisper.transcribe(
                samples, path_or_hf_repo=self.model, language="en",
                condition_on_previous_text=False, fp16=True,
                initial_prompt="vizBot, Kevin, WLED, vizMac, Stackchan.",
            )
        return (out.get("text") or "").strip()


def wav_to_float(data: bytes) -> tuple[np.ndarray, int]:
    """Decode a 16-bit PCM WAV to float32 mono samples."""
    with wave.open(io.BytesIO(data)) as w:
        rate, ch, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
        frames = w.readframes(w.getnframes())
    if width != 2:
        raise ValueError(f"expected 16-bit WAV, got {width * 8}-bit")
    pcm = np.frombuffer(frames, dtype="<i2").astype(np.float32) / 32768.0
    if ch > 1:
        pcm = pcm.reshape(-1, ch).mean(axis=1)
    if rate != 16000:
        n = int(len(pcm) * 16000 / rate)
        pcm = np.interp(np.linspace(0, len(pcm), n, endpoint=False), np.arange(len(pcm)), pcm)
        pcm = pcm.astype(np.float32)
    return pcm, 16000


def normalize(samples: np.ndarray, target_peak: float = 0.7) -> np.ndarray:
    """Boost quiet mic recordings so whisper hears them clearly."""
    peak = float(np.max(np.abs(samples))) if samples.size else 0.0
    if peak < 1e-4:
        return samples
    return np.clip(samples * (target_peak / peak), -1.0, 1.0).astype(np.float32)


def synthesize(text: str, voice: str, rate: int) -> bytes:
    """Speak text; returns raw s16le mono PCM at PLAY_RATE. Kokoro voices look
    like "kokoro:am_puck@1.12"; anything else goes to macOS `say` (also the
    fallback if Kokoro fails)."""
    if voice.startswith("kokoro:"):
        name, _, speed = voice[7:].partition("@")
        try:
            return kokoro.speak(text, name, float(speed or 1.0))
        except Exception as e:  # noqa: BLE001 - fall back to say rather than go silent
            print(f"[tts] kokoro failed ({e}); using say")
            voice = ""
    fd, path = tempfile.mkstemp(suffix=".wav")
    os.close(fd)
    try:
        cmd = ["say", "-r", str(rate), "-o", path, "--file-format=WAVE",
               f"--data-format=LEI16@{PLAY_RATE}"]
        if voice:
            cmd += ["-v", voice]
        r = subprocess.run(cmd + ["--", text], capture_output=True, timeout=30)
        if r.returncode != 0 and voice:
            # Voice not installed — fall back to the system default.
            subprocess.run(cmd[:-2] + ["--", text], check=True, capture_output=True, timeout=30)
        with wave.open(path) as w:
            return w.readframes(w.getnframes())
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


class LiveClip:
    """PCM that grows while it's being read: sentences are synthesized and
    appended as Claude writes them, and GET /v1/audio streams it as it grows."""

    def __init__(self, pcm: bytes | None = None):
        self.buf = bytearray(pcm or b"")
        self.done = pcm is not None
        self.created = time.time()
        self._cv = threading.Condition()

    def append(self, pcm: bytes) -> None:
        with self._cv:
            self.buf += pcm
            self._cv.notify_all()

    def finish(self) -> None:
        with self._cv:
            self.done = True
            self._cv.notify_all()

    def read_from(self, pos: int, timeout: float = 30.0) -> tuple[bytes, bool]:
        """Bytes after pos (waits for some, up to timeout) and whether the clip is complete."""
        with self._cv:
            if len(self.buf) <= pos and not self.done:
                self._cv.wait(timeout)
            return bytes(self.buf[pos:]), self.done   # done: nothing will follow this data


class ClipStore:
    """Reply clips the bot fetches with GET /v1/audio/<id>. Kept 60 s after creation."""

    TTL_S = 60

    def __init__(self):
        self._clips: dict[str, LiveClip] = {}
        self._lock = threading.Lock()

    def add(self, clip: LiveClip) -> str:
        cid = uuid.uuid4().hex[:12]
        now = time.time()
        with self._lock:
            self._clips = {k: v for k, v in self._clips.items() if now - v.created < self.TTL_S}
            self._clips[cid] = clip
        return cid

    def put(self, pcm: bytes) -> str:
        return self.add(LiveClip(pcm))

    def get(self, cid: str) -> LiveClip | None:
        with self._lock:
            return self._clips.get(cid)


class Mouth:
    """Sentence-by-sentence speech into a LiveClip, on its own thread, in order."""

    def __init__(self, clip: LiveClip, voice: str, rate: int):
        self.clip, self.voice, self.rate = clip, voice, rate
        self._q: "queue.Queue[str | None]" = queue.Queue()
        self.spoken: list[str] = []
        threading.Thread(target=self._run, daemon=True).start()

    def say(self, text: str) -> None:
        if text.strip():
            self.spoken.append(text.strip())
            self._q.put(text.strip())

    def close(self) -> None:
        self._q.put(None)

    def _run(self) -> None:
        while (text := self._q.get()) is not None:
            try:
                self.clip.append(synthesize(text, self.voice, self.rate))
            except Exception as e:  # noqa: BLE001 - skip a bad sentence, keep talking
                print(f"[tts] failed on {text!r}: {e}")
        self.clip.finish()


class SentenceSplitter:
    """Turns streamed text deltas into whole sentences, pulling out [face:x]
    [gesture:y] tags (never spoken) as soon as they're complete."""

    _TAG = re.compile(r"\[(face|gesture)\s*:\s*([a-z_]+)\]", re.I)
    _END = re.compile(r"(.+?[.!?…])(?=\s)", re.S)

    def __init__(self, on_sentence, on_tag):
        self.on_sentence, self.on_tag = on_sentence, on_tag
        self.buf = ""

    def feed(self, delta: str) -> None:
        self.buf += delta
        for kind, value in self._TAG.findall(self.buf):
            self.on_tag(kind.lower(), value.lower())
        self.buf = self._TAG.sub("", self.buf)
        self.buf = re.sub(r"\[actions:[^\]]*\]", "", self.buf)   # never speak action notes
        # Hold back anything after an unfinished "[" (a tag still arriving).
        cut = self.buf.rfind("[")
        ready, pending = (self.buf, "") if cut < 0 or "]" in self.buf[cut:] else (self.buf[:cut], self.buf[cut:])
        while (m := self._END.match(ready)):
            self.on_sentence(_clean(m.group(1)))
            ready = ready[m.end():]
        self.buf = ready + pending

    def flush(self) -> None:
        rest = _clean(self._TAG.sub("", self.buf))
        self.buf = ""
        if rest:
            self.on_sentence(rest)


def _clean(text: str) -> str:
    text = re.sub(r"[*_`#>\[\]]+", "", text)
    return re.sub(r"\s+", " ", text).strip()


def pcm_ms(pcm: bytes) -> int:
    return int(len(pcm) / 2 / PLAY_RATE * 1000)
