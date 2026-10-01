"""Local speech: whisper (mlx) for listening, macOS `say` for speaking.

Both run on the Mac; no audio leaves the LAN (plan decision D3). `say` is the
v1 voice because it needs no model download; Kokoro can replace it later
behind the same synthesize() call.
"""

from __future__ import annotations

import io
import os
import subprocess
import tempfile
import threading
import time
import uuid
import wave

import numpy as np

PLAY_RATE = 24000  # Hz, s16le mono — what the bot's speaker plays

# A distinct voice per personality (vizbot/bot_mode.h built-ins).
PERSONALITY_VOICES = {
    "chill": "Eddy (English (US))",
    "hyper": "Junior",
    "grumpy": "Grandpa (English (US))",
}


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
    """Speak text with macOS `say`; returns raw s16le mono PCM at PLAY_RATE."""
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


class ClipStore:
    """Reply clips the bot fetches with GET /v1/audio/<id>. Kept 60 s."""

    TTL_S = 60

    def __init__(self):
        self._clips: dict[str, tuple[float, bytes]] = {}
        self._lock = threading.Lock()

    def put(self, pcm: bytes) -> str:
        cid = uuid.uuid4().hex[:12]
        now = time.time()
        with self._lock:
            self._clips = {k: v for k, v in self._clips.items() if now - v[0] < self.TTL_S}
            self._clips[cid] = (now, pcm)
        return cid

    def get(self, cid: str) -> bytes | None:
        with self._lock:
            hit = self._clips.get(cid)
        return hit[1] if hit else None


def pcm_ms(pcm: bytes) -> int:
    return int(len(pcm) / 2 / PLAY_RATE * 1000)
