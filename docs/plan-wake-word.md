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

(filled in as steps finish)
