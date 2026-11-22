# Stage 3 plan: AudioAO

**Status 2026-11-22: built (fw 3.12.1.ao6/ao7), bench-checked on mac.** The voice task is the
only owner of `M5.Mic` and `M5.Speaker`. The mic starts once at boot and runs through LISTEN
and THINK. Silent replies (Mac speaker, or volume 0) never start the bot's speaker or amp: a
clock paces the mouth instead. The FX analyzer only reads the wake tap; it never opens the mic.
Timeline check: one `micStart` at boot, no mic stop or amp edge in a Mac-speaker turn, the reply
was answered, and FX on/off left the mic alone. Also fixed on the way: OTA uploads starved IDLE0
into a watchdog reset (yield every 16 chunks), and the wake pre-roll burst queued about 24 WiFi
TX buffers (paced, see net-budgets.md).

RAM: `/debug/tasks` `heapMinFree` used to be `heap_caps_get_minimum_free_size`, which adds up
each region's own low taken at different moments (it said 8.4 KB). An allocator tracer
(`env:stackchan-ramtrace`, `/debug/ramtrace`) showed the real low was 34 KB at boot and about
39–47 KB in a voice turn. `heapMinFree` is now the free RAM sampled every 2 ms. The old number
stays as `heapRegionMinSum`, and `allocFails` counts refused allocations (0).

Written 2026-11-22 from a read of `feat/active-objects` (3.12.0 + working tree) and
M5Unified 0.2.29 in `vizbot/.pio/libdeps/stackchan`. Nothing changed or flashed.
**[V]** = read in source. **[I]** = inferred, to measure. GitNexus (11 commits
behind, weak on header-only C++) gave no useful edges; every site is from grep + read.

## Verdict: mic running while the speaker plays? No. [V]

- Mic and speaker share `I2S_NUM_1` and BCK 34 / WS 33 (`M5Unified.inl:2664-2677`,
  `:2885-2895`). Each makes its own simplex channel in a separate `i2s_new_channel`
  (`Mic_Class.inl:313`, `Speaker_Class.inl:200`), and on the S3 each task writes the
  clock divider for its own rate (`m5unified_i2s.h:71-89`). Rates differ (16 k vs
  48 k / 24 k). M5's Microphone example ends one before starting the other.
- IDF 5.5 pairs two std channels into full duplex only when configs match
  (`i2s_std.h:302`). That needs our own duplex driver: **not this stage** (risk 1).

**Feasible [V API, I behaviour]:** the mic runs from boot, between turns, and through
LISTEN and THINK with no codec reset. It also stays on through SPEAK whenever the bot
is silent: Mac-speaker replies (today played at volume 0 just to pace the mouth) and
volume 0. Those turns have no mic stop and no amp. A reply on the bot's own speaker
costs 1 mic stop + 1 start (today: 2 begins + 2 ends per turn). Continuous capture is
supported: the `setBufferReleaseCallback` callback runs on `mic_task` and may re-arm
`record()` (`Mic_Class.hpp:142-156`).

## 1. Call sites (stackchan unless noted)

Tasks: voice = core 0 prio 1 · loop = core 1 prio 1 · wifi_srv = core 0 prio 1 ·
M5Unified `mic_task` / `spk_task` = prio 2, **unpinned** (`Mic_Class.hpp:90-93`).

| File:line | What | Task |
|---|---|---|
| `voice_client.h:236-245` `voiceTakeMic` | `voiceOwnsAudio=true` + `vTaskDelay(60)`, Speaker.end, Mic.end + begin (always) | voice |
| `voice_client.h:247-251` `voiceTakeSpeaker` | Mic.end, Speaker.begin (**amp on**), setVolume | voice |
| `voice_client.h:254-265` `voiceRestoreAudio` | Mic.end, Speaker.end (MIDI), Mic.begin if FX, writes `micRunning` | voice |
| `voice_client.h:292,337` `voiceRecord` | `Mic.record` (lazy-begins), spins on `isRecording` | voice |
| `voice_client.h:521-590` `voiceStreamSpeak` | takeSpeaker **before the first byte**, `setVolume(0)` if Mac, playRaw / isPlaying / stop | voice |
| `voice_client.h:642-656` `voiceRunTurn` | polls `botSounds.playing` for the beep; takeMic; garbage retry end + begin ×2; Mic.end | voice |
| `voice_client.h:762-763` `voiceRunPlay` (/brain/play) | `voiceOwnsAudio=true` + `vTaskDelay(60)` | voice |
| `voice_client.h:868-894` `wakeShouldRun` / `wakeStart` | `!Speaker.isRunning()` gate; Mic.begin; writes `micRunning` | voice |
| `voice_client.h:964-972,1026-1032` wake chunk / step | garbage restart end + begin; isRunning; `record()` | voice |
| `voice_client.h:1083-1086` `voiceTask` | Mic.end when wake stops and FX is off; writes `micRunning` | voice |
| `audio_spectrum.h:124-136` `init` | Mic.config (4×512 DMA), Mic.begin (not on Faces) | setup, core 1 |
| `audio_spectrum.h:146-166` `setEnabled` | Mic.begin / end, writes `micRunning` (Faces: LED release / reclaim) | loop **and** wifi_srv |
| `audio_spectrum.h:186-196` `update` | reads `voiceOwnsAudio` / `botSounds.playing`; `record()` **lazy-begins a mic another task ended** | loop |
| `web_server.h:2101-2108` `handleAudioFx` | `setEnabled` from core 0, ignores the voice turn | wifi_srv |
| `web_server.h:3097-3136` listen / wake / `handleBrainPlay` | set voice request flags | wifi_srv |
| `settings.h:128-130`, `touch_ui.h:1541`, `touch_control.h:475` | `setEnabled` | loop / setup |
| `bot_sounds.h:759,858-871,885,993` | speaker begin / setVolume / stop / playRaw **only if `!useMidi`** (stackchan normally uses UART MIDI) | loop |
| `vizbot.ino:110` / `:446` / `:688` | `Speaker.isPlaying()` touch-context bit 10 / Speaker.end (**Faces only**) / `audioSpectrum.update()` | loop / setup |
| `audio_analysis.h` | Mic.begin / record; **included nowhere** (dead) | — |
| `M5Unified.inl:986-1040` | Mic.begin = ES7210 RESET + 29 writes (MICBIAS on); Mic.end = RESET | caller |
| `M5Unified.inl:601-627` | Speaker.begin = AW9523 bit + 5 AW88298 writes, amp at full; end = I2SEN off + bit off; `setVolume` is software only | caller |
| `Mic_Class.inl:1133`, `Speaker_Class.inl:1371` | `record()` and `playRaw()` **begin lazily** | caller |

## 2. State machine (`vizbot/ao_audio.h`, core 0, prio 3, queue 32)

`mic_task` is pinned to core 0. Its release callback is the only reader: it re-arms
`record()` into the next slot of a 40-chunk PSRAM ring (today's `wake.ring`) and
posts `SIG_MIC_CHUNK(idx)`.

| State | Mic | Speaker / amp | Per chunk |
|---|---|---|---|
| OFF (voice and FX off) | stopped | off | — |
| IDLE / WAKE_STREAM (gate open) | running | off | noise floor, wake gate → `SIG_WAKE_SEND`, FX tap |
| LISTEN | running | off | copy into `recBuf`; today's end-of-speech logic |
| THINK | running | off | FX tap; wake gate held closed |
| SPEAK_SILENT (Mac / volume 0) | running | **off** | mouth clock from `millis()` at 24 kHz |
| SPEAK_AMP | **stopped** | on only while PCM is queued | `playRaw` from `playBuf` |
| PLAY (/brain/play) | as SPEAK_*, origin = brain | | |
| TONE (only if `!useMidi`) | stopped | on for the tone | — |

**SPECTRUM** is an orthogonal flag. FX on publishes the tap (seqlock, as
`wakeTapBuf`) in every mic-running state. `audioSpectrum.update()` (loop) only reads
the tap, never `record()`, and mutes on `SIG_SOUND_START/END` and in SPEAK_AMP.

- **In:** `SIG_MIC_CHUNK(idx)`, `SIG_AUDIO_TICK` (33 ms), `SIG_LISTEN_REQ(a=hangoverMs,
  b=followUp)`, `SIG_LISTEN_CANCEL`, `SIG_SPEAK_BEGIN(a=silent, b=origin)`,
  `SIG_SPEAK_DATA(a=samples, b=eof)`, `SIG_SPEAK_CANCEL`, `SIG_FX_SET(a)`,
  `SIG_VOICE_ENABLE(a)`, `SIG_SOUND_START/END` (botSounds; replaces the beep poll),
  `SIG_TONE_REQ`.
- **Out:** `SIG_MIC_START/STOP(a=boot|speak|garbage|stall|disable)`,
  `SIG_AMP_ON/OFF`, `SIG_AUDIO_STATE(a)`, `SIG_LISTEN_DONE(a=samples,
  b=cancelled|garbage)`, `SIG_SPEAK_DONE(a=ms, b=starved)`, `SIG_WAKE_SEND(a=idx,
  b=flags)` (the voice task sends the UDP until NetAO).
- **Buffers:** AudioAO owns `recBuf`, `playBuf`, `env` and the ring. The voice task
  (a blocking HTTP client until stage 5) fills `playBuf` before posting counts, and
  waits on a small inbox for `*_DONE`.

## 3. Migration steps

Each step builds every env and ends with a bench build (bump `FIRMWARE_VERSION`
first, dot suffix: `…own3`, `…own3b`, …) checked on the timeline. **Guard:**
AudioAO code sits under `AUDIO_AO`, defined in `config.h` only for
`BOARD_HAS_STACKCHAN_BASE && VOICE_ENABLED`. `m5cores3` / `faces` keep
`audio_spectrum.h`'s direct path (M5Unified 0.2.13 semantics; Faces' GPIO13 LED
sharing). LCD and matrix boards have no I2S audio.

1. **Instrument, no behaviour change.** Add `audioMicBegin/End(reason)` and
   `audioSpkBegin/End()` to `ao_audio.h`; route every stackchan M5 Mic / Speaker
   begin / end above through them to publish `SIG_MIC_*` / `SIG_AMP_*`. Before each
   `record()` / `playRaw`, publish `reason=lazy` when the call would start the
   hardware. This gives the per-turn baseline.
2. **AudioAO owns capture; IDLE, WAKE_STREAM and FX move in.** It begins the mic
   once at boot (instead of `audioSpectrum.init` / `wakeStart`) with the callback and
   `task_pinned_core = 0`. The wake gate, noise floor and garbage detector move out
   of `wakeProcessChunk`; `wakeStep` only sends what `SIG_WAKE_SEND` names. Shim: on
   `AUDIO_AO`, `setEnabled()` posts `SIG_FX_SET`. The old turn code still runs, its
   Mic end / begin turned into AudioAO pause / resume shims. Removes: `micRunning`
   from 3 tasks, the `record()` lazy restart, FX-off killing the wake mic.
3. **LISTEN.** `voiceRunTurn` posts `SIG_LISTEN_REQ` after `SIG_SOUND_END` and waits
   for `SIG_LISTEN_DONE`. `voiceTakeMic`, the garbage retry and the post-record
   `Mic.end` go. Warm-up chunks apply only after a real mic start. Removes: 1 begin
   + 1 end per turn, the beep polling.
4. **SPEAK.** `voiceStreamSpeak` posts `SIG_SPEAK_BEGIN(silent = mac || volume ==
   0)`, then `SIG_SPEAK_DATA` as bytes land, and waits for `SIG_SPEAK_DONE`.
   SPEAK_SILENT runs `voiceSpeakT0` and the starve slide off the clock. SPEAK_AMP
   stops the mic and begins the speaker **at preroll, not at HTTP open**, then ends
   it when the queue drains at EOF and restarts the mic. `voiceTakeSpeaker`,
   `voiceRestoreAudio` and the `setVolume(0)` trick go. Removes: amp on for a whole
   Mac reply, the second mic cycle.
5. **PLAY + cleanup.** `voiceRunPlay` uses the same path (its flag + 60 ms go).
   Delete `voiceOwnsAudio`, `wakeShouldRun`'s speaker check and `audio_analysis.h`.
   On stackchan `!useMidi`, botSounds posts `SIG_TONE_REQ` instead of holding the
   speaker from boot. Touch-context bit 10 reads the `SIG_AMP_*` state. Then a cold
   boot and a 4 h soak.

## 4. Risks and unverified items

1. **Speaker replies still cycle the mic once.** Zero needs a custom full-duplex I2S
   driver (TX + RX from one `i2s_new_channel`, ES7210 + AW88298 at one rate), i.e.
   forking M5's Mic / Speaker. Defer; decide on stage 3's numbers. [V limit, I fix]
2. **A long-running mic is new.** Today's per-turn restart partly clears stale DMA
   after a warm reset (`voice_client.h:240`, "measured on HW"). Keep the garbage
   check on every start and add a stall watchdog (no chunk for 500 ms → restart,
   published). Unknown: whether ES7210 / I2S RX drifts or goes deaf over 24 h. [I]
3. **The local M5Unified may be patched.** Its Mic / Speaker locking (`_begin_lock`,
   re-arm from the callback) is far more elaborate than the 0.2.13 copies, and nothing
   proves it is stock 0.2.29 (compare the local M5GFX patch). If patched, these
   semantics must go into the repo with stage 0. [unverified]
4. **Mouth sync in SPEAK_SILENT** follows the bot's clock, not a muted I2S clock.
   Neither is the Mac's clock, so expect drift of the same order; check long replies
   by eye. [I]
5. **Echo / self-wake.** The always-on mic hears MIDI cues, the Mac speaker and the
   bot's own voice. Keep the wake gate closed outside IDLE (today the stream just
   stops) and mute FX on `SIG_SOUND_*`. [I]
6. **Constant MICBIAS and BCK.** Codec current and the 34 / 33 clock now run all the
   time. Steady load should suit the touch chip better than steps; unmeasured. [I]
7. **Callback discipline.** The callback runs in `mic_task`: memcpy-free index +
   non-blocking `aoPost` only. A full queue drops `MIC_CHUNK`; the 1.28 s ring
   covers it and the AO catches up by index. [V API]
8. **No SAM2695.** Without the MIDI synth, botSounds starts the speaker at boot and
   the wake stream never runs today. Step 5 changes this; test with the synth
   unplugged. [V]

## 5. What to watch on the timeline and soak

- **Mic per turn:** stop / start 0 / 0 for Mac, silent and failed turns; 1 / 1 for a
  speaker reply (step-1 baseline: 2 / 2). None between turns; one `boot` start.
- **Amp:** `SIG_AMP_ON` only in SPEAK_AMP, on-time ≈ audio played + preroll. Never
  in Mac mode, THINK or HTTP waits.
- **Touch:** contacts within ±2 s of `SIG_MIC_*` / `SIG_AMP_*`, and phantom listens
  just after a turn (chain 2), against the step-1 baseline. Both should drop.
- **Health:** `micRestarts` (garbage, stall), chunk gaps over 100 ms, AudioAO queue
  drops, ring overruns, `reason=lazy` (must be 0 from step 2).
- **Function:** wake bursts / wakes / `sendFails` steady; FX `alive` across a turn;
  listen first-chunk latency; `/brain/status` `speakerUp` and `starved`; follow-up
  listen; `/brain/play` with FX on.
- **Scorecard:** no row worse after a 4 h soak from a cold boot.

> **Checked 2026-11-22:** the local M5Unified is byte-identical to the 0.2.29 release tag (and M5GFX to 0.2.36). No local patches exist; risk 2 is closed.
