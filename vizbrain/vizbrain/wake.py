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


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="vizbrain.wake")
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("record", help="save every burst from the bot as a WAV")
    r.add_argument("out", type=Path)
    args = p.parse_args(argv)
    if args.cmd == "record":
        _record(args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
