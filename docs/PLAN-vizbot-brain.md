# vizBot Brain — LLM Voice, Lab Control and Senses

**Status:** Stages 0–1 built and verified on hardware (no API key yet); Stage 2 tools built
**Started:** October 1, 2026
**Current production version:** `vizbot-stackchan-v3.4.2`
**First firmware release from this plan:** `vizbot-stackchan-v3.5.0` (Stage 1, voice)
**Companion overview:** [vizBot Brain — LLM Plan](https://claude.ai/code/artifact/80c62773-0978-4fab-8dd5-f3c01ebb9964) (plain-language summary, diagrams, cost tables)

---

## Build log

### October 1, 2026 — Stages 0, 1 and 2 (tools) built

| What | Result |
|---|---|
| vizbrain service (`vizbrain/` in this repo) | Running on the Mac at :4050, Bonjour `_vizbrain._tcp` |
| Firmware `vizbot-stackchan-v3.5.5` | Flashed to vizBot-skunky over USB |
| Voice turn, end to end | 3 of 3 spoken questions from a fresh boot transcribed and answered; reply starts ~2.6 s after end of speech with the offline stand-in |
| Lab tools | WLED set + restore verified on "Pipe cox"; vizMac keyboard flash verified; 8 WLED lights discovered |
| `vizlab` MCP server | 9 tools listed and called |
| Claude | **Not yet exercised:** no Anthropic API key exists on the Mac. Run `vizbrain set-key` |
| Middle-pad tap | **Not yet tested by hand.** Every hardware test used `GET /brain/listen` |

Deviations from the plan above:

- **vizbrain lives in this repo** (`vizbrain/`), not a sibling repo. One branch rolls back firmware and brain together.
- **Text to speech is macOS `say` for v1** (D3 said Kokoro). No model download, ~0.4 s per reply, a different voice per personality. Kokoro stays a drop-in later behind `speech.synthesize()`.
- **Speech to text is `mlx-whisper` with `whisper-small.en`** (~0.6 s for a short question on the M5 Mac).
- **Thinking:** Sonnet 5.5 runs with `thinking: {type: "between_tools"}` at effort `low`. History between turns is plain text only, so trimming old turns never edits a request that carried thinking blocks.
- **Bubbles during a voice turn bypass WLED** (shown on the LCD only), so the WLED matrix isn't spammed with "Listening...".

Hardware findings:

- **Mic chunk completion must be checked, not inferred.** Assuming a chunk is done once two more are queued read unwritten PSRAM, which looked like a constant loud noise and kept recordings open for 15 s. Each chunk's last sample is now preset to `INT16_MAX`, a value M5Unified never writes, and the chunk counts as complete once that is overwritten.
- **The mic is restarted at the start of every turn,** with an automatic re-restart if the noise floor reads above 1500 RMS. Real room noise on skunky is 55–130 RMS; speech peaks are 2,000–3,000.
- Bot debug endpoints added: `/brain/status?trace=1` (per-chunk mic RMS) and `/brain/lastwav` (last raw recording).

---

## Premise

vizBot gets a brain. Stackchan stays the **body**: mic, speaker, face, head servos, base LEDs, and later the camera and radar. A new Python service on the Mac, **vizbrain**, is the **brain**. It turns speech into text, asks Claude what to say and do, turns the reply into speech, and calls tools that drive the bot and the lab.

Three goals, in order:

1. **Talk to him** and get spoken answers.
2. **Lab control:** WLED lights, the vizMac keyboard, and the bot himself, through one tool layer that is also an MCP server.
3. **Senses:** radar presence and camera photos shape his behavior.

Ground rules:

- **The bot keeps his own personality engine.** Idle drift, sayings, moods and touch reactions keep running on-device. The brain is called only on speech and on events.
- **Brain offline = v3.4 behavior.** If the brain can't be reached, he acts exactly like today and shows a "brain offline" face.
- **This reactivates two 3.0 deferrals:** voice (3.0 Q9) and the MCP device server (3.0 Q8). The 3.0 rule that new endpoints be single-purpose and named like tools is why most of this is glue code.
- **Target:** Stackchan (`BOARD_HAS_STACKCHAN_BASE`) first. The bare CoreS3 port is a Stage 4 item.

---

## Decisions made

| # | Topic | Decision | Rationale |
|---|---|---|---|
| D1 | Brain location | Mac, in a sibling repo `~/projects/vizbrain` | Free, already on, and reaches WLED and vizMac directly. Plain HTTP keeps it portable to vizCloud later behind a tunnel (e.g. Tailscale). |
| D2 | Conversation start | Push-to-talk by head pat | No false triggers and no wake-word model in v1. Wake word moves to Stage 4. |
| D3 | Speech services | Local on the Mac: whisper (whisper.cpp or mlx-whisper) for speech to text; Kokoro (fallback Piper) for text to speech | Free and private. Audio never leaves the LAN. |
| D4 | Models | Claude Sonnet 5.5 (`claude-sonnet-5-5`) for conversation; Claude Haiku 4.5 (`claude-haiku-4-5`) for background event reactions | Sonnet balances speed, tool use and vision. Haiku keeps always-on reactions cheap. |
| D5 | Transport | Plain HTTP on the LAN, request/response, versioned `/v1` | No WebSocket, no TLS on the bot side, nothing new to keep alive. Streaming is a Stage 4 upgrade. |
| D6 | Tool layer | One Python tool registry inside vizbrain, also exposed as an MCP server `vizlab-mcp` (stdio) | Write each tool once. Claude Code and Claude Desktop can drive the lab with the same tools. |
| D7 | Safety allow-list | Destructive endpoints are never tools: `/bot/poweroff`, `/wifi/reset`, `/update`, `/bot/servo/reinit`, `/device/name` | The model can change looks and lights, never connectivity, firmware or power. |
| D8 | Secrets | The Anthropic API key lives in the environment or the macOS Keychain, never in source | Lesson from `CLOUD_BOT_SECRET` hardcoded at `vizbot/config.h:306`. |

---

## Architecture

```
                 LAN (plain HTTP)                        Internet (HTTPS)
┌──────────────────────┐        ┌──────────────────────────┐      ┌──────────────┐
│ vizBot (Stackchan)    │ ─────▶ │ vizbrain  (Mac, :4050)   │ ───▶ │ Claude API   │
│  mic · speaker · face │  WAV,  │  speech → text (whisper) │ ◀─── │ Sonnet 5.5   │
│  head · base LEDs     │ events │  Claude + tool runner    │      │ Haiku 4.5    │
│  camera · radar (S3)  │ ◀───── │  text → speech (Kokoro)  │      └──────────────┘
│  own personality      │  /bot/*│  tool registry ──┬───────┤
└──────────────────────┘  calls  │  vizlab-mcp      │       │
                                  └──────────────────┼───────┘
                                     ┌───────────────┴──────────────┐
                                     ▼                              ▼
                              WLED (/json/state)        vizMac (127.0.0.1:4049)
```

### One voice turn

1. Kevin pats the middle of the head. The bot plays a short chirp and shows **FOCUSED** (listening).
2. The bot records 16 kHz mono audio until 800 ms of silence (max 15 s).
3. The bot shows **THINKING** and POSTs the WAV to `POST /v1/voice`.
4. vizbrain transcribes the audio and runs Claude with the tools. Tool calls hit the bot's existing `/bot/*` endpoints during the turn (expression, head move, LEDs) and the lab APIs.
5. vizbrain synthesizes the reply and returns `{text, expression, audio_id, audio_ms}`.
6. The bot GETs `/v1/audio/<id>`, shows the text in the speech bubble, plays the audio and moves the mouth with the volume.
7. The bot returns to IDLE and his own personality.

### Latency budget (target: reply starts ≤ 3 s after you stop talking, median)

| Step | Budget |
|---|---|
| Silence hangover (end-of-speech detection) | 0.8 s |
| Upload WAV (~5 s clip ≈ 160 KB) | 0.2 s |
| Speech to text on the Mac | 0.3–0.6 s |
| Claude reply, including tool calls | 0.8–1.5 s |
| Text to speech | 0.3–0.5 s |
| Fetch audio and start playback | 0.2 s |
| **Total** | **2.6–3.8 s** |

If the median misses 3 s, cut in this order: shorter silence hangover (600 ms), `thinking: {type: "between_tools"}`, smaller whisper model, then Stage 4 streaming.

---

## Interface contracts

### vizbrain API (port 4050)

Ports in use on the Mac: vizMac HTTP 4049, DDP 4048.

| Method | Path | Body / query | Response |
|---|---|---|---|
| `POST` | `/v1/voice` | `audio/wav`, 16 kHz, mono, s16le. Header `X-Bot-Id: <device name>` | `200 {"text": str, "expression": int, "audio_id": str, "audio_ms": int}` · `503 {"error": "busy"}` if a turn is in progress |
| `GET` | `/v1/audio/<id>` | — | `audio/L16; rate=24000` raw s16le PCM. Each clip is kept for 60 s. |
| `POST` | `/v1/text` | `{"text": str}` (Stage 0 typing page and debugging) | Same as `/v1/voice` |
| `POST` | `/v1/event` | `{"type": "presence_enter" \| "presence_leave" \| "lean_in" \| "still", "distance_cm"?: int}` (Stage 3) | `202` (the brain reacts by calling bot tools) |
| `GET` | `/v1/health` | — | `{"ok": true, "model": str, "stt": str, "tts": str}` |

Discovery: vizbrain advertises `_vizbrain._tcp` by running `dns-sd -R` as a child process. This is the same pattern as vizMac's Bonjour advertising (`~/projects/vizmac/vizmac/service.py:742-750`). The bot also accepts a manual host, stored as `brainHost`.

### Bot endpoints the brain calls

Existing:

- `/bot/say?text=&dur=`
- `/bot/expression?v=`
- `/bot/head/set_angles?yaw=&pitch=&time=`
- `/bot/base_leds/set` and `/bot/base_leds/mode`
- `/bot/personality`
- `/state`

New in this plan:

| Path | Stage | Purpose |
|---|---|---|
| `/brain/config?host=&enabled=` | 1 | Set and persist `brainHost` and `voiceEnabled` |
| `/brain/status` | 1 | Voice state, last error, brain reachable |
| `/bot/photo/capture`, `/bot/photo/get` | 3 | Fill in the camera stubs at `vizbot/web_server.h:2139-2140` (routes registered at `web_server.h:2276-2279`) |

### Lab APIs the brain calls

- **WLED:** JSON API `POST http://<ip>/json/state` (`on`, `bri`, `seg[0].col`, `seg[0].fx`).
  - Discovery reuses vizMac's approach: `dns-sd -B _wled._tcp`, resolved by name (`~/projects/vizmac/vizmac/wled.py:55-75`).
  - Devices are addressed by name from an allow-list in vizbrain settings.
- **vizMac:** calls from the same Mac need no token (`service.py:786-800`). POSTs must send `Content-Type: application/json`.
  - `POST http://127.0.0.1:4049/api/effect` with `{"name": "<effect>"}`
  - `POST /api/overlay` with `{"kind": "flash", "color": "#RRGGBB", "ms": 100–120000}`
  - `GET /api/effects` for the effect list the model may pick from

---

## Firmware design (Stage 1, Stackchan only)

All voice code is guarded by a new `VOICE_ENABLED` flag. It is defined in `vizbot/config.h` under `BOARD_HAS_STACKCHAN_BASE`, next to the `CLOUD_ENABLED` block (`config.h:299-302`). Other targets build exactly as today.

### New module: `vizbot/voice_client.h`

State machine:

```
IDLE → LISTENING → UPLOADING → THINKING → FETCHING → SPEAKING → IDLE
                         any state ── error/timeout ──▶ ERROR (CONFUSED face, 3 s) → IDLE
```

| State | Face | Exit condition |
|---|---|---|
| LISTENING | FOCUSED (17) | 800 ms silence, 15 s max, or a second pat (cancel) |
| UPLOADING / THINKING | THINKING (8) | Response JSON, or a 12 s timeout |
| FETCHING | THINKING (8) | PCM fully in the buffer |
| SPEAKING | Reply expression + lip-sync | Playback ends |
| ERROR | CONFUSED (13) + bubble "brain offline" | 3 s |

### Audio hardware arbitration

The CoreS3 mic and speaker share `I2S_NUM_1`. Nothing arbitrates them today. `voice_client.h` owns the sequence:

1. `audioSpectrum.setEnabled(false)` (pause the FFT visualizer).
2. `M5.Speaker.end()`.
3. `M5.Mic.begin()`, then record.
4. `M5.Mic.end()`.
5. `M5.Speaker.begin()` and `M5.Speaker.setVolume(botSounds volume)`.
6. Restore `audioSpectrum` to its saved setting.

Known issue to verify first: the boot order `botSounds.init()` then `audioSpectrum.init()` (`vizbot/boot_sequence.h:481-483`) may leave the speaker dead when the SAM2695 synth is absent. `Mic.begin()` uninstalls the I2S driver the speaker started. Stage 1 confirms this on hardware and fixes it with the same arbitration. The SAM2695 runs on a UART, not I2S, so it is unaffected.

### Capture

- 16 kHz mono s16. The buffer holds up to 15 s (480 KB) in PSRAM via `heap_caps_malloc(n, MALLOC_CAP_SPIRAM)`. It is allocated once at first use and kept.
- This is the project's first explicit PSRAM allocation; comment it as such. Today PSRAM is only used through LovyanGFX's canvas and large `String`s.
- `M5.Mic.record()` is asynchronous: it queues a buffer and M5Unified's mic task fills it later. Use `M5.Mic.isRecording()` to wait for completion before reading a chunk. The current spectrum code reads the previous frame; don't copy that.
- Write a 44-byte WAV header in front of the samples so the brain can read the buffer as a file.

### End-of-speech detection

Lift the RMS logic from the unused `vizbot/audio_analysis.h`: RMS over 256-sample windows, speech floor 600. Change it in three ways:

- **Hangover:** speech ends after 800 ms continuously below `floor × 0.5`.
- **Limits:** clips under 0.4 s are dropped silently (an accidental pat). Recording is capped at 15 s.
- **Servo quiet:** head idle drift is paused while LISTENING so motor noise doesn't keep the detector open.

### Trigger

- Add a new return code `2` (middle-pad tap) to `ScTouchState::update()` (`vizbot/stackchan_touch.h:72-124`). The middle pad is ignored today (line 24).
- Dispatch it in the touch block at `vizbot/vizbot.ino:511-523`. Front tap = nod and back tap = shake stay unchanged; the 2 s hold stays chill mode.
- A middle-pad tap during LISTENING cancels the recording.

### Network

- **New task:** `voiceTask` on Core 0 with a **static** 6 KB stack in BSS, following the rule at `vizbot/task_manager.h:441-445` that protects the contiguous heap block TLS needs.
- **Requests:**
  - Upload uses plain-HTTP `esp_http_client` in `open → write chunks → fetch_headers → read` form, the same shape as `vizbot/cloud_client.h:292-380` but with no cert.
  - Download streams the PCM straight into a PSRAM playback buffer: 20 s × 24 kHz × 2 B = 960 KB max.
  - The web server task keeps serving during a turn, so the brain's mid-turn `/bot/*` tool calls are handled normally.

### Playback and lip-sync

- `M5.Speaker.playRaw(buf, samples, 24000, false)` from PSRAM.
- Before playing, compute an amplitude envelope: one byte per 33 ms frame.
- In the render loop, next to the blink write at `vizbot/bot_mode.h:533`, set `face.mouthType = MOUTH_OPEN_O` and drive `face.mouthCurve` from the envelope while SPEAKING. When speaking ends, restore the expression's own mouth.

### Speech bubble

`Command.say.text` is 60 bytes (`vizbot/task_manager.h:77-97`), but `MAX_SAY_LEN` (`vizbot/config.h:253`, used by the bubble at `vizbot/bot_overlays.h:39`) allows 96. Raise `say.text` to `MAX_SAY_LEN` so replies aren't cut at 59 characters. Long replies show their first sentence in the bubble; the full text is spoken.

### Settings

Two new keys in the `"vizbot"` Preferences namespace, loaded and saved with the existing debounced flow (`vizbot/settings.h:53`, `123-157`):

- `brainHost` (string, empty = use mDNS `_vizbrain._tcp`)
- `voiceEnabled` (bool, default true when `VOICE_ENABLED`)

The web panel gets a "Brain" card with host, status and a test button.

---

## Brain design (`vizbrain/` in this repo)

A sibling repo that mirrors vizMac's structure, so both feel the same to run and maintain.

| Area | Choice |
|---|---|
| Runtime | Python ≥ 3.11, setuptools `pyproject.toml`, `.venv`, console script `vizbrain` |
| HTTP | Stdlib `ThreadingHTTPServer` on 4050 (vizMac's pattern) |
| Settings | `~/Library/Application Support/vizbrain/settings.json` (bot host, WLED allow-list, voice, model) |
| Run | launchd user agent; logs in `~/Library/Logs/vizbrain/` |
| Secrets | `ANTHROPIC_API_KEY` from the environment or the Keychain |
| Dependencies | `anthropic`, STT (whisper.cpp binding or `mlx-whisper`), `kokoro-onnx`, `mcp` |

### Claude configuration

- **Conversation model:** `claude-sonnet-5-5`, `output_config.effort: "low"`, `max_tokens` around 1024.
- **Event model:** `claude-haiku-4-5` for `/v1/event`, with a short prompt and the bot tools only.
- **Prompt caching:** on the system prompt and tool definitions, so most of each request is billed at the cache-read rate.
- **Tool loop:** the SDK tool runner (`client.beta.messages.tool_runner`) with typed `@beta_tool` functions.
- **Refusal fallback:** server-side fallback on Sonnet 5.5 (`fallbacks: "default"`, beta `server-side-fallback-2026-07-01`). Always check `stop_reason` before reading content.
- **Latency lever:** if replies are slow, test `thinking: {type: "between_tools"}` (accepted at effort `high` or below) against the eval prompts.
- **Output style:** the system prompt tells him to answer in one to three spoken sentences, without markdown or lists.

### Conversation state

- Rolling window of the last 10 turns, reset after 5 minutes of silence.
- The system prompt carries the active personality (Chill, Hyper or Grumpy), read from `GET /bot/personality` at the start of each session.
- Long-term memory (names, preferences, a daily journal) is Stage 4.

### Tool registry (v1)

| Tool | Calls | Stage |
|---|---|---|
| `set_expression(name)` | `/bot/expression?v=` | 0 |
| `move_head(yaw, pitch, ms)` | `/bot/head/set_angles` | 0 |
| `set_base_leds(color, mode)` | `/bot/base_leds/*` | 0 |
| `wled_set(device, on?, brightness?, color?, effect?)` | WLED `/json/state` | 2 |
| `vizmac_effect(name)` | vizMac `/api/effect` | 2 |
| `vizmac_flash(color, ms)` | vizMac `/api/overlay` | 2 |
| `look()` | `/bot/photo/capture` then `/bot/photo/get`, sent to Claude as an image | 3 |
| `who_is_here()` | Latest radar state from events | 3 |

The spoken reply is the model's final text, not a tool. `vizlab-mcp` imports the same registry and serves it over stdio.

---

## Stages

**Stage 0 — Text brain (~3 hours)**

Goal: prove the brain and the tools with no firmware change.

- Scaffold `~/projects/vizbrain`: settings, HTTP server, `/v1/text`, `/v1/health`.
- Personality prompts and the three Stage 0 bot tools.
- A one-page typing UI served by vizbrain.

**Exit criteria for Stage 0:** typing a message produces a speech bubble, a matching expression and a head move on the Stackchan.

**Stage 1 — Voice (~2 weekends)** → `vizbot-stackchan-v3.5.0`

Goal: pat, talk, hear an answer.

- Firmware: `VOICE_ENABLED`, `voice_client.h`, I2S arbitration, capture, end-of-speech detection, middle-pad trigger, `voiceTask`, playback, lip-sync, `/brain/*` endpoints, `say.text` size, web panel card.
- Brain: `/v1/voice`, `/v1/audio/<id>`, whisper and Kokoro integration, `_vizbrain._tcp` advertising.
- Benchmark speech-to-text and text-to-speech options on the Mac and pick one of each.

**Exit criteria for Stage 1:**

- 10 consecutive pat-to-reply turns with no failure.
- Median ≤ 3 s from end of speech to start of reply.
- Speaker and mic both work after a cold boot and after 20 turns.
- With the brain stopped, a pat shows "brain offline" and the bot otherwise behaves like v3.4.2.

**Stage 2 — Lab control (~1 weekend)**

Goal: voice commands drive the office.

- WLED discovery plus allow-list, `wled_set`, `vizmac_effect`, `vizmac_flash`.
- `vizlab-mcp` server, registered in Claude Code.

**Exit criteria for Stage 2:** "make the lights purple" changes the allow-listed WLED devices and the keyboard. The same tools work from Claude Code through `vizlab-mcp`.

**Stage 3 — Senses (~2–3 weekends)**

Goal: he notices people and can look.

- Wire the LD2412 radar per `docs/plan-environmental-awareness-sensors.md` (Port B, `Serial1`, 115200).
- The bot POSTs presence events to `/v1/event`.
- Land the GC0308 camera driver (3.0 Q11) and the `/bot/photo/*` endpoints. Add the `look` and `who_is_here` tools.
- Brain event rules: greet on arrival with a 10-minute cooldown; quiet mode during still presence.

**Exit criteria for Stage 3:**

- Walking in triggers one greeting, and a second walk-in within 10 minutes does not.
- "What am I holding?" gets a correct answer for 4 of 5 common objects.

**Stage 4 — Polish (open-ended, pick any)**

- Wake word on-device ("Hey vizBot") with ESP-SR.
- Streaming upload and playback, so replies start in about 1 s.
- Long-term memory.
- Bare CoreS3 port (same audio path, no servos or radar).
- Routines: morning greeting, lights off at the end of the day.

---

## Cost

Prices as of October 1, 2026.

| Model | Input $/M tokens | Cache read $/M | Output $/M | Est. per reply |
|---|---|---|---|---|
| Claude Sonnet 5.5 (conversation) | $2 | $0.20 | $10 | ~$0.006 |
| Claude Haiku 4.5 (events) | $1 | $0.10 | $5 | ~$0.003 |

Per-reply estimate: ~3,000 cached prompt tokens, ~1,000 fresh, ~300 output. A VGA camera photo adds ~400 input tokens. Speech runs locally on the Mac at no cost.

| Usage | Monthly |
|---|---|
| Normal: 30 replies a day | ~$5 |
| Heavy: 150 replies a day | ~$27 |
| Events: ~20 Haiku reactions a day | ~$2 |

One-time hardware: HLK-LD2412 radar, about $10. Set a monthly spend limit in the Claude Console before Stage 1.

Sources:

- [Claude API pricing](https://platform.claude.com/docs/en/about-claude/pricing)
- [OpenAI API pricing](https://developers.openai.com/api/docs/pricing) (cloud speech alternative)
- [ElevenLabs API pricing](https://elevenlabs.io/pricing/api) (cloud voice alternative)

---

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| Mic and speaker fight over `I2S_NUM_1`, leaving one dead | Single owner (`voice_client.h`) with an explicit end/begin sequence. Stage 1 exit requires a cold-boot test and a 20-turn test. |
| Servo noise or his own voice keeps the mic "hearing speech" | Pause idle drift while listening; the mic is never on while speaking. |
| Replies slower than 3 s | The latency levers listed under Architecture, in order. Instant chirp and THINKING face so the wait reads as alive. |
| Mac asleep or vizbrain down | 2 s connect timeout, then ERROR state and v3.4 behavior. `/brain/status` shows reachability. |
| PSRAM or heap pressure from audio plus camera | Buffers allocated once in PSRAM and reused. Static task stacks. Camera waits for Stage 3. |
| Model drives something it shouldn't | Allow-list (D7). WLED devices addressed only by allow-listed name. |
| Cost creep | Console spend limit, prompt caching, Haiku for events, event cooldowns. |
| Privacy of audio and photos | Speech never leaves the LAN (D3). Photos are taken only by the `look` tool, never streamed. |

---

## Explicitly deferred / out of scope

- Wake word → Stage 4
- Streaming audio → Stage 4
- Brain on vizCloud → after Stage 2, if the Mac proves inconvenient
- Bare CoreS3 port → Stage 4
- Multi-bot conversations through the ESP-NOW mesh → after Stage 4
- Cloud speech services → only if local quality or speed falls short in the Stage 1 benchmark

---

## Open items requiring future decision

1. Speech to text: whisper.cpp binding vs. `mlx-whisper`, and model size. Decide from the Stage 1 benchmark on the Mac.
2. Kokoro voice pick per personality. Chill, Hyper and Grumpy may each get their own voice.
3. Whether to add a dedicated LISTENING expression instead of reusing FOCUSED.
4. Whether `vizlab-mcp` should also wrap vizMac media controls (`/api/media`).
5. Whether to put [Hypercolor](https://hyperb1iss.github.io/hypercolor/) behind the lab tools. It's an Apache-2.0 Rust daemon that unifies 400+ RGB devices (WLED, Razer, Corsair, Hue, Nanoleaf, Govee) behind REST/WebSocket and its own 18-tool MCP server. It could replace the hand-rolled WLED client, but vizMac's ROCCAT keyboard support would need checking first.
