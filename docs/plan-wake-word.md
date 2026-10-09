# "Hey vizBot" wake word — plan

Say "Hey vizBot" and the Stackchan starts a voice turn, same as a front head-pad tap.

## Design

```
Stackchan (idle)                         vizbrain (Mac)
 mic 16 kHz ──level gate──▶ UDP :4051 ──▶ wake.py: openWakeWord "hey_vizbot" model
   (only sends while someone's talking,      │ score > threshold
    with 1 s of pre-roll)                    ▼
 /brain/wake  ◀──────────── HTTP ───────── start a turn (beep → listen → reply)
```

- Detection runs on the Mac (decided in sprint 2): the model is easy to retrain and swap, and the bot only needs a mic streamer.
- The bot sends audio only while the room is louder than its noise floor (plus 1 s pre-roll), so a quiet lab sends nothing.
- `/brain/wake` starts a turn only when idle. It never cancels one (unlike `/brain/listen`).
- ESP-SR WakeNet is out: custom words need Espressif's paid service.

## Training

openWakeWord trains a small classifier on top of a frozen speech-embedding model.

| Data | Source |
|---|---|
| Positives | Kevin saying "Hey vizBot" through the bot's own mic (captured by step 2), plus thousands of synthetic clips (Piper libritts, ~900 speakers; Kokoro voices) |
| Near misses | "hey robot", "hey biz bot", "vizpow", "hey Siri"... synthetic, labelled negative |
| Background negatives | ~200 h slice of openWakeWord's ACAV100M features (~1.7 GB of the 17 GB file, via HTTP range) |
| False-accept check | openWakeWord validation set (~11 h, 185 MB) |

Downloads run one at a time, in the foreground.

## Steps

| # | Step | Done when |
|---|---|---|
| 1 | Firmware: gated mic stream over UDP, `/brain/wake`, wake stats in `/brain/status` | Mac receives clean audio; Kevin listens to a saved clip |
| 2 | vizbrain receiver saves each burst as a WAV; Kevin records ~50 "Hey vizBot"s + a few minutes of normal talk | Clips on disk |
| 3 | Train `hey_vizbot.onnx` | Recall on Kevin's real clips ≥ 90%, < 1 false accept per 10 h on the validation set |
| 4 | vizbrain runs the model on the stream, calls `/brain/wake` | Kevin says "Hey vizBot" and gets a turn |
| 5 | Soak a day with music/TV on; tune the threshold; release | Few or no false wakes |

## Results

### October 9, 2026 — steps 1–4 done, first live wake worked

| What | Result |
|---|---|
| Firmware 3.7.13 | Gated UDP stream works; ~1 lost chunk in 500; heap flat (~83 KB), 0 WiFi rejoins over 90 min. Audio FX shares the mic through `wakeTapBuf` |
| Labelling | Whisper can't label quiet clips (it invents "Thanks for watching!", and the "Hey vizBot." prompt makes it hear the phrase in music). Fixed with `python -m vizbrain.wake studio`: a page with a Record button and a script; one labelled clip per press |
| Data | 47 real "Hey vizBot"s + 13 real sound-alikes (bot mic), 7,129 synthetic positives (Kokoro voice blends, Piper libritts, macOS `say`), 5,000 synthetic sound-alikes, 600 h ACAV100M negatives |
| Training recipe | First model: ~1,000 false accepts / 10 h. Fix = openWakeWord's recipe: negative weight ramped to 1500, 32-unit net, 10% positives per batch, 8,000 steps (longer overfits) |
| Model | `hey_vizbot.onnx` at threshold 0.5: 6.5 false accepts / 10 h on the 10.7 h validation set (openWakeWord's hey_jarvis: 4.7 with the same eval); 11/11 held-out real clips score 1.0 when streamed; room/TV clips 0.0. "Hey, this bot" still fires |
| Live | Kevin said "Hey vizBot" → score 0.99 → beep → answered. Reply started ~1.5 s after he stopped talking |

Rebuild: `training/wakeword.py` (gen → real/studio → features → train). Data and models live in `~/Library/Application Support/vizbrain/wakeword/`; vizbrain loads `models/hey_vizbot.onnx`.

Next: step 5, a day of normal use with music/TV on, then tune `wake_threshold` in settings.json.
