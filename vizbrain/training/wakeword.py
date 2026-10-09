"""Train the "Hey vizBot" wake-word model (openWakeWord-style).

Runs in its own venv, not vizbrain's (it needs PyTorch):

    W=~/Library/Application\\ Support/vizbrain/wakeword
    $W/venv/bin/python training/wakeword.py gen        # synthetic clips (Kokoro, Piper, say)
    $W/venv/bin/python training/wakeword.py real DIR   # cut Kevin's clips recorded through the bot
    $W/venv/bin/python training/wakeword.py studio DIR # add labelled clips from `vizbrain.wake studio`
    $W/venv/bin/python training/wakeword.py features   # clips -> openWakeWord embeddings
    $W/venv/bin/python training/wakeword.py train      # train, test, export hey_vizbot.onnx

How it works: openWakeWord turns audio into 96-number "speech embeddings"
every 80 ms with two frozen models. The wake-word model is a small classifier
that looks at the last 16 embeddings (~2 s of audio) and says "was that
'Hey vizBot'?". Training it only needs embeddings, so the huge negative set
(ACAV100M, downloaded pre-embedded) never has to be turned back into audio.

Every synthetic clip is roughened to sound like the bot heard it: a room echo,
real background recorded by the bot's own mic, and a random EQ.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import random
import re
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import butter, fftconvolve, resample_poly, sosfilt

W = Path.home() / "Library" / "Application Support" / "vizbrain" / "wakeword"
CLIPS, FEATS = W / "clips", W / "features"
PSG = W / "psg"
RATE = 16000
WIN = 32000            # 2 s: exactly 16 embeddings
FRAMES = 16

POS_TEXT = ["Hey vizbot!", "Hey, vizbot.", "Hey vizbot", "Hey vizbot?", "Hey, vizbot!"]
# Sounds-alike phrases that must NOT wake him.
ADV_TEXT = ["Hey robot!", "Hey bot.", "Hey, biz bot.", "Hey this bot.", "Hey, his bot.",
            "Hey there!", "Hey Siri.", "Hey Jarvis.", "Hey, visit.", "Hey business.",
            "Hey, is it hot?", "Hey everybody!", "Hey, it's hot.", "Hey kids.", "Hey Vince.",
            "Have this pot.", "Hey vista.", "Hey Mister.", "Hey, Bob.", "The bot is here."]


def load16(path: str | Path) -> np.ndarray:
    a, sr = sf.read(str(path), dtype="float32", always_2d=False)
    if a.ndim > 1:
        a = a.mean(axis=1)
    if sr != RATE:
        g = np.gcd(sr, RATE)
        a = resample_poly(a, RATE // g, sr // g).astype(np.float32)
    return a


def save16(path: Path, a: np.ndarray) -> None:
    sf.write(str(path), np.clip(a, -1, 1), RATE, subtype="PCM_16")


# ---- gen: synthetic clips ---------------------------------------------------------

def gen_kokoro(out: Path, texts: list[str], n: int, seed: int) -> None:
    from kokoro_onnx import Kokoro
    kd = Path.home() / "Library" / "Application Support" / "vizbrain" / "kokoro"
    k = Kokoro(str(kd / "kokoro-v1.0.onnx"), str(kd / "voices-v1.0.bin"))
    voices = [v for v in k.get_voices() if v[:2] in ("af", "am", "bf", "bm")]
    rng = random.Random(seed)
    out.mkdir(parents=True, exist_ok=True)
    for i in range(n):
        if (out / f"{i}.wav").exists():
            continue
        a, b = rng.sample(voices, 2)
        w = rng.random()
        style = k.get_voice_style(a) * w + k.get_voice_style(b) * (1 - w)   # a new speaker
        lang = "en-gb" if a[0] == "b" else "en-us"
        audio, sr = k.create(rng.choice(texts), voice=style, speed=rng.uniform(0.8, 1.3), lang=lang)
        save16(out / f"{i}.wav", resample_poly(audio, 2, 3) if sr == 24000 else audio)
        if i % 500 == 0:
            print(f"  kokoro {out.name} {i}/{n}", flush=True)


def gen_piper(out: Path, text: str, n: int) -> None:
    out.mkdir(parents=True, exist_ok=True)
    have = len(list(out.glob("*.wav")))
    if have >= n:
        return
    subprocess.run([sys.executable, "-m", "piper_sample_generator", text,
                    "--model", str(PSG / "models" / "en_US-libritts_r-medium.pt"),
                    "--max-samples", str(n), "--batch-size", "50",
                    "--length-scales", "0.8", "0.9", "1.0", "1.1", "1.25",
                    "--output-dir", str(out)],
                   check=True, env={**os.environ, "PYTHONPATH": str(PSG)},
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def gen_say(out: Path, texts: list[str]) -> None:
    out.mkdir(parents=True, exist_ok=True)
    voices = [l.split()[0] for l in subprocess.run(["say", "-v", "?"], capture_output=True, text=True).stdout.splitlines()
              if re.search(r"\sen_(US|GB|AU|IE|IN|ZA|NZ)\s", l) and "(" not in l.split()[0]]
    i = 0
    for v in voices:
        for rate in (150, 185, 220):
            p = out / f"{i}.wav"
            if not p.exists():
                subprocess.run(["say", "-v", v, "-r", str(rate), "-o", str(p),
                                "--data-format=LEI16@16000", random.choice(texts)], capture_output=True)
            i += 1


def cmd_gen(_args) -> None:
    print("Kokoro positives...", flush=True)
    gen_kokoro(CLIPS / "pos_kokoro", POS_TEXT, 4000, seed=1)
    print("Kokoro near-misses...", flush=True)
    gen_kokoro(CLIPS / "adv_kokoro", ADV_TEXT, 3000, seed=2)
    print("macOS voices...", flush=True)
    gen_say(CLIPS / "pos_say", POS_TEXT)
    print("Piper positives...", flush=True)
    gen_piper(CLIPS / "pos_piper", "hey viz bot", 3000)
    print("Piper near-misses...", flush=True)
    for j, t in enumerate(ADV_TEXT[:10]):
        gen_piper(CLIPS / f"adv_piper_{j}", t.lower().strip(".!?"), 200)
    print("done", flush=True)


# ---- real: Kevin's clips through the bot's mic -------------------------------------

WAKE_RE = re.compile(r"^(hey|hay|hi)$")
VIZ_RE = re.compile(r"^(vi[sz]|wis|bis|biz)\w*bo[tdp]?$|^vi[sz]+$")


def cmd_real(args) -> None:
    """Cut every 'Hey vizBot' out of the recorded bursts; everything else is room negatives."""
    import mlx_whisper
    src = Path(args.dir)
    pos_dir, neg_dir = CLIPS / "kevin_pos", CLIPS / "room_neg"
    pos_dir.mkdir(parents=True, exist_ok=True)
    neg_dir.mkdir(parents=True, exist_ok=True)
    log = {}
    npos = 0
    for f in sorted(src.glob("*.wav")):
        a = load16(f)
        r = mlx_whisper.transcribe(str(f), path_or_hf_repo="mlx-community/whisper-small.en-mlx",
                                   word_timestamps=True, initial_prompt="Hey vizBot.")
        words = [(re.sub(r"[^a-z]", "", w["word"].lower()), w["start"], w["end"])
                 for s in r["segments"] for w in s.get("words", [])]
        cuts = []
        for (w1, s1, _), (w2, _, e2) in zip(words, words[1:]):
            if WAKE_RE.match(w1) and (VIZ_RE.match(w2) or w2 in ("vizbot", "visbot")) and e2 - s1 < 2.0:
                cuts.append((max(0.0, s1 - 0.15), min(len(a) / RATE, e2 + 0.2)))
        log[f.name] = {"text": r["text"].strip(), "cuts": cuts}
        if cuts:
            for c0, c1 in cuts:
                seg = a[int(c0 * RATE):int(c1 * RATE)]
                if np.abs(seg).max() > 0.02:
                    save16(pos_dir / f"{f.stem}_{npos}.wav", seg)
                    npos += 1
        else:
            save16(neg_dir / f.name, a)
    (CLIPS / "real_log.json").write_text(json.dumps(log, indent=1))
    print(f"{npos} 'Hey vizBot' cuts -> {pos_dir}")
    print(f"{len(list(neg_dir.glob('*.wav')))} room clips -> {neg_dir}")


def cmd_studio(args) -> None:
    """Labelled clips from the studio page: wake words -> kevin_pos, sound-alikes -> kevin_neg."""
    src = Path(args.dir)
    pos_dir, neg_dir = CLIPS / "kevin_pos", CLIPS / "kevin_neg"
    pos_dir.mkdir(parents=True, exist_ok=True)
    neg_dir.mkdir(parents=True, exist_ok=True)
    n = {True: 0, False: 0}
    for line in (src / "labels.jsonl").read_text().splitlines():
        rec = json.loads(line)
        # One clip per take: "studio_<take>_0" keeps each take its own hold-out group.
        stem = "studio" + Path(rec["file"]).stem.replace("_", "")
        save16((pos_dir if rec["wake"] else neg_dir) / f"{stem}_0.wav", load16(src / rec["file"]))
        n[rec["wake"]] += 1
    print(f"{n[True]} wake words -> {pos_dir}, {n[False]} sound-alikes -> {neg_dir}")


# ---- features ----------------------------------------------------------------------

class Roughen:
    """Make a clean clip sound like the bot's mic heard it from across a room."""

    def __init__(self, seed: int):
        self.rng = np.random.default_rng(seed)
        self.rirs = [load16(p) for p in (PSG / "piper_sample_generator" / "impulses").glob("*.wav")]
        bg = [load16(p) for p in sorted((CLIPS / "room_neg").glob("*.wav"))]
        self.bg = np.concatenate(bg) if bg else np.zeros(RATE, np.float32)

    def __call__(self, x: np.ndarray, real: bool = False) -> np.ndarray:
        r = self.rng
        x = x / (np.abs(x).max() + 1e-6) * r.uniform(0.05, 0.9)
        if not real and r.random() < 0.5 and self.rirs:
            rir = self.rirs[r.integers(len(self.rirs))]
            rir = rir[: int(RATE * 0.6)] / (np.abs(rir).max() + 1e-6)
            y = fftconvolve(x, rir)[: len(x) + RATE // 4]
            x = (y / (np.abs(y).max() + 1e-6) * np.abs(x).max()).astype(np.float32)
        if r.random() < 0.6:
            lo, hi = r.uniform(80, 300), r.uniform(3500, 7500)
            x = sosfilt(butter(2, [lo, hi], btype="band", fs=RATE, output="sos"), x).astype(np.float32)
        return x

    def place(self, x: np.ndarray, end_jitter_s: float = 0.25) -> np.ndarray:
        """Put the clip at the end of a 2 s window over room background."""
        r = self.rng
        x = x[-WIN + 400:]
        out = np.zeros(WIN, np.float32)
        end = WIN - int(r.uniform(0, end_jitter_s) * RATE)
        start = max(0, end - len(x))
        out[start:end] = x[len(x) - (end - start):]
        if r.random() < 0.85:
            o = r.integers(0, max(1, len(self.bg) - WIN))
            bg = self.bg[o:o + WIN]
            if len(bg) == WIN:
                sig = np.sqrt(np.mean(x ** 2)) + 1e-6
                snr = r.uniform(3, 25)
                bg = bg / (np.sqrt(np.mean(bg ** 2)) + 1e-6) * sig / (10 ** (snr / 20))
                out += bg
        out += r.normal(0, r.uniform(1e-4, 3e-3), WIN).astype(np.float32)
        return out


def embed(windows: np.ndarray) -> np.ndarray:
    from openwakeword.utils import AudioFeatures
    af = AudioFeatures(ncpu=8)
    pcm = (np.clip(windows, -1, 1) * 32767).astype(np.int16)
    f = af.embed_clips(pcm, batch_size=256, ncpu=8)
    return f[:, -FRAMES:, :].astype(np.float16)


def clip_windows(rough: Roughen, files: list[str], reps: int, real: bool = False) -> np.ndarray:
    out = []
    for _ in range(reps):
        for f in files:
            out.append(rough.place(rough(load16(f), real=real)))
    return np.stack(out) if out else np.zeros((0, WIN), np.float32)


def room_windows(files: list[str]) -> np.ndarray:
    """Every 2 s window (hop 0.5 s) of the real room recordings."""
    out = []
    for f in files:
        a = load16(f)
        a = np.concatenate([np.zeros(WIN // 2, np.float32), a])
        for s in range(0, max(1, len(a) - WIN + 1), RATE // 2):
            w = a[s:s + WIN]
            if len(w) == WIN:
                out.append(w)
    return np.stack(out) if out else np.zeros((0, WIN), np.float32)


def cmd_features(args) -> None:
    FEATS.mkdir(parents=True, exist_ok=True)
    rough = Roughen(seed=7)
    rng = random.Random(3)
    kevin = sorted(glob.glob(str(CLIPS / "kevin_pos" / "*.wav")))
    # Hold out whole recording bursts (a burst's cuts sound alike).
    bursts = sorted({Path(k).stem.rsplit("_", 1)[0] for k in kevin})
    rng.shuffle(bursts)
    test_b = set(bursts[: max(1, len(bursts) // 5)])
    k_test = [k for k in kevin if Path(k).stem.rsplit("_", 1)[0] in test_b]
    k_train = [k for k in kevin if k not in k_test]
    room = sorted(glob.glob(str(CLIPS / "room_neg" / "*.wav")))
    rng.shuffle(room)
    r_test, r_train = room[: len(room) // 5], room[len(room) // 5:]
    json.dump({"kevin_test": k_test, "room_test": r_test}, open(FEATS / "split.json", "w"), indent=1)

    synth_pos = sum((sorted(glob.glob(str(CLIPS / d / "*.wav"))) for d in ("pos_kokoro", "pos_piper", "pos_say")), [])
    adv = sorted(glob.glob(str(CLIPS / "adv_*" / "*.wav")))
    kneg = sorted(glob.glob(str(CLIPS / "kevin_neg" / "*.wav")))
    sets = {
        "pos_synth": (synth_pos, 2, False),
        "pos_kevin": (k_train, 40, True),
        "adv": (adv, 1, False),
        "neg_kevin": (kneg, 40, True),
    }
    for name, (files, reps, real) in sets.items():
        print(f"{name}: {len(files)} clips x{reps}", flush=True)
        np.save(FEATS / f"{name}.npy", embed(clip_windows(rough, files, reps, real)))
    print("room negatives...", flush=True)
    np.save(FEATS / "room_train.npy", embed(room_windows(r_train)))
    np.save(FEATS / "room_test.npy", embed(room_windows(r_test)))
    # Test positives: Kevin's held-out clips, lightly placed (no extra roughening).
    print(f"kevin test: {len(k_test)} clips", flush=True)
    t = [rough.place(load16(f) / (np.abs(load16(f)).max() + 1e-6) * 0.5, end_jitter_s=0.1) for f in k_test]
    np.save(FEATS / "kevin_test.npy", embed(np.stack(t)) if t else np.zeros((0, FRAMES, 96), np.float16))
    print("done", flush=True)


# ---- train -------------------------------------------------------------------------

def make_model(hidden: int = 32, dropout: float = 0.0):
    import torch.nn as nn
    return nn.Sequential(
        nn.Flatten(),
        nn.Linear(FRAMES * 96, hidden), nn.LayerNorm(hidden), nn.ReLU(), nn.Dropout(dropout),
        nn.Linear(hidden, hidden), nn.LayerNorm(hidden), nn.ReLU(), nn.Dropout(dropout),
        nn.Linear(hidden, 1), nn.Sigmoid(),
    )


def false_accepts(scores: np.ndarray, thr: float, gap: int = 13) -> int:
    """Count separate triggers (scores above thr at least ~1 s apart)."""
    n, last = 0, -10 ** 9
    for i in np.flatnonzero(scores > thr):
        if i - last > gap:
            n += 1
        last = i
    return n


def cmd_train(args) -> None:
    import torch
    import torch.nn.functional as F
    torch.manual_seed(0)
    dev = "mps" if torch.backends.mps.is_available() else "cpu"
    L = lambda n: torch.from_numpy(np.load(FEATS / f"{n}.npy").astype(np.float32))
    pos = torch.cat([L("pos_synth"), L("pos_kevin")])
    neg_small = torch.cat([L("adv"), L("room_train"), L("neg_kevin")])
    acavs = [np.load(p, mmap_mode="r") for p in sorted((W / "dl").glob("acav_slice*.npy"))]
    sizes = np.array([len(a) for a in acavs])
    print(f"positives {len(pos)}  near-miss+room {len(neg_small)}  background {sizes.sum()}", flush=True)

    def acav_batch(n: int) -> np.ndarray:
        which = rng.choice(len(acavs), size=n, p=sizes / sizes.sum())
        return np.concatenate([acavs[k][np.sort(rng.integers(sizes[k], size=(which == k).sum()))]
                               for k in range(len(acavs))]).astype(np.float32)

    model = make_model(args.hidden, args.dropout).to(dev)
    opt = torch.optim.AdamW(model.parameters(), lr=1e-3, weight_decay=1e-4)
    steps, B = args.steps, 1024
    nP, nN = int(B * args.pos_frac), int(B * 0.15)    # positives, near-miss+room; the rest is background
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=1e-3, total_steps=steps)
    rng = np.random.default_rng(0)
    for step in range(steps):
        pi = torch.from_numpy(rng.integers(len(pos), size=nP))
        ni = torch.from_numpy(rng.integers(len(neg_small), size=nN))
        x = torch.cat([pos[pi], neg_small[ni], torch.from_numpy(acav_batch(B - nP - nN))]).to(dev)
        y = torch.cat([torch.ones(nP), torch.zeros(B - nP)]).to(dev)
        # Ramp up how much false accepts cost, like openWakeWord does.
        w = torch.ones_like(y)
        w[nP:] = 1 + (args.neg_weight - 1) * min(1.0, step / (steps * 0.5))
        p = model(x).squeeze(1).clamp(1e-6, 1 - 1e-6)
        loss = (F.binary_cross_entropy(p, y, reduction="none") * w).mean()
        opt.zero_grad()
        loss.backward()
        opt.step()
        sched.step()
        if step % 1000 == 0 or step == steps - 1:
            print(f"  step {step:5d}  loss {loss.item():.4f}", flush=True)

    model = model.cpu().eval()
    score = lambda a: model(torch.from_numpy(np.asarray(a, np.float32))).squeeze(1).detach().numpy()
    with torch.no_grad():
        kt = score(np.load(FEATS / "kevin_test.npy"))
        rt = score(np.load(FEATS / "room_test.npy"))
        val = np.load(W / "dl" / "validation_set_features.npy", mmap_mode="r")
        idx = np.arange(FRAMES)[None, :] + np.arange(len(val) - FRAMES + 1)[:, None]
        vs = np.concatenate([score(val[idx[i:i + 20000]]) for i in range(0, len(idx), 20000)])
    hours = len(val) * 0.08 / 3600
    report = {"kevin_test_n": int(len(kt)), "room_test_windows": int(len(rt)), "validation_hours": round(hours, 1),
              "thresholds": {}}
    for thr in (0.5, 0.9, 0.97, 0.99, 0.995, 0.999):
        report["thresholds"][thr] = {
            "recall_kevin": round(float((kt > thr).mean()) if len(kt) else 0, 3),
            "false_accepts_per_10h": round(false_accepts(vs, thr) / hours * 10, 2),
            "room_test_windows_over": int((rt > thr).sum()),
        }
    print(json.dumps(report, indent=1))
    out = W / "hey_vizbot.onnx"
    torch.onnx.export(model, torch.zeros(1, FRAMES, 96), str(out), input_names=["input"],
                      output_names=["score"], opset_version=13, dynamo=False)
    (W / "hey_vizbot_report.json").write_text(json.dumps(report, indent=1))
    print(f"saved {out}")


def main() -> int:
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("gen")
    r = sub.add_parser("real")
    r.add_argument("dir")
    sub.add_parser("features")
    t = sub.add_parser("train")
    t.add_argument("--steps", type=int, default=20000)
    t.add_argument("--neg-weight", type=float, default=20.0)
    t.add_argument("--dropout", type=float, default=0.0)
    t.add_argument("--hidden", type=int, default=32)
    t.add_argument("--pos-frac", type=float, default=0.1)
    st = sub.add_parser("studio")
    st.add_argument("dir")
    a = p.parse_args()
    {"gen": cmd_gen, "real": cmd_real, "studio": cmd_studio, "features": cmd_features, "train": cmd_train}[a.cmd](a)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
