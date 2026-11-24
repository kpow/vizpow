# StackChan vizBot: missing platform patterns

Phase B of `/stackchan-stable`, 2026-11-22. Each gap: the pattern an ESP32 +
FreeRTOS device with shared peripherals normally uses, and where vizBot does
something else instead. Evidence and line numbers are in
[stackchan-interactions.md](stackchan-interactions.md).

## 1. One owner per shared resource

| Resource | Today | Where |
|---|---|---|
| I2C bus | Three lock layers that don't cover each other: our `ScI2cLock` / `i2cAcquire`, M5GFX's per-transaction mutex, and the camera's IDF SCCB driver under neither | `stackchan_base.h`, `stackchan_camera.h` |
| | Callers that skip our lock: IMU read, LTR553 proximity, M5Unified Mic / Speaker / Power, backlight | `vizbot.ino` `readIMU`, proximity poll, M5Unified |
| | Callers that carry on after the lock times out: touch read, battery read | `stackchan_base.h` `scReadTouch`, battery read |
| | Base LEDs write 13 transactions every frame whether or not anything changed | `scLeds.update` |
| I2S (mic / speaker) | `voiceOwnsAudio` flag plus a 60 ms sleep; `micRunning` written from 3 tasks; `Mic.record()` silently restarts a mic another task ended | `voice_client.h` `voiceTakeMic`, audio spectrum, `web_server.h` `handleAudioFx` |
| Servo bus | BSP Motion mutex taken by the loop watchdog, `/debug/servo`, web presets and idle drift; recovery has no re-entry guard | `scRecoverServos`, `web_server.h` head handlers |
| Screen | `botRenderHold` check-then-set | render path, `/debug/screen` |

## 2. Explicit arbitration

- **Audio:** no state machine. The mic is ended and restarted twice per voice
  turn (codec reset, MICBIAS step each time). The amp stays on for the whole
  reply even at volume 0 in Mac mode (`voice_client.h` ~522).
- **Power:** no budget. Servo torque-on, LED punch, amp on and WiFi TX at
  19.5 dBm can all land in the same few ms.
- **Motion:** gestures, web presets and idle drift drive the head separately.
  Web presets don't set `shakeReacting`.

## 3. Events, not shared variables

- Plain globals written from both cores: `scHeadBusyUntilMs`,
  `scTouchRestartAtMs`, `scTouchQuietUntilMs`, voice request flags, `cloudState`,
  party requests, camera flags.
- **The touch chip is told about almost nothing.** It isn't told about servo
  torque, LED flashes, the amp, mic restarts or WiFi bursts. Only rail switches
  ask for a restart, and that restart lands during the next glide.
- **Touch reference timing:** the boot start runs during servo homing
  (`VizStackChan::beginHardware`), and heals run during the reaction the last
  phantom fired. After a cold boot the chip never gets a quiet reference
  (measured 2026-11-22).

## 4. Nothing blocks the loop

- Blocking calls in the loop: `scFireNod` 1.5 s, `scFireShake` 1.2 s,
  `scRecoverServos` 2.1–4.6 s, battery rest and wake 1.3–3 s,
  `sc5vLearnAtRest` 1.1 s, any I2C lock during a photo up to 3 s.
- Web handlers on core 0 that act on hardware directly instead of queueing:
  head preset (blocking), servo reinit, photo, audio FX, chill, debug power
  (holds the bus through `delay(500)`), debug touchraw (rewrites the chip).
- **Task plan:**
  - The mic and speaker tasks (priority 2) and motion (priority 10) can run on
    either core.
  - Core 1 has only the 30 s hang timer.
  - The power log writes to flash every 15 s, which stalls both cores.

## 5. Budgets

- No written budget for internal RAM, DMA, task stacks or WiFi buffers.
- The camera allocates a 32 KB contiguous block without checking first. The
  measured largest free block 2.6 minutes after boot was 31.7 KB.
- A voice upload (up to 384 KB) isn't gated against the cloud TLS sync.
- The 41 KB internal dip during cloud sync is unexplained.

## 6. One system timeline

- The touch context bits are the only state log. They don't record LED flashes,
  amp on/off, HTTP transmit, photo or servo torque-on, so a failure can't be read
  against everything that was running.

## 7. Reproducible build

- Corrected 2026-11-22: M5GFX is the stock 0.2.36 release, so there was no local
  patch. M5-SAM2695 was the only unpinned library; it is now pinned (stage 0).
