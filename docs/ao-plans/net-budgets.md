# Stage 5 plan: NetAO, budgets, task plan

2026-11-22, read-only, from `feat/active-objects` (3.12.0). **[V]** read in source, **[I]**
inferred. Conventions: [README.md](README.md). Tasks: `wifi_srv` (core 0, prio 1), `cloud` (0, idle), `voice` (0, 1).

## 1. What transmits today, and which web handlers act on hardware

**WiFi TX paths [V].** Radio: `setSleep(false)`, 19.5 dBm (`config.h:65`), 8 static TX
buffers, 16 cache TX, BA window 6 (framework sdkconfig). lwIP: 16 sockets, TCP send buf 5.7 KB.

| # | Path | Where | Task | Size, rate | Gated today |
|---|---|---|---|---|---|
| 1 | Wake stream UDP | `voice_client.h:920` `wakeSend`; pre-roll loop 993–998 | voice | 1036 B / 32 ms while open; 32-packet burst, `vTaskDelay(1)` apart; beat / 5 s | `cloudNetHeld()` (915) + errno-12 back-off 1–8 s |
| 2 | Voice upload | `voicePostWav` 401 (writes 418–425), from `voiceRunTurn` 693 | voice | ≤ 384 KB WAV in 8 KB writes, 4 KB TX buf | **none** |
| 3 | Reply fetch (ACKs) | `voiceStreamSpeak` 487 | voice | RX ≤ 2.9 MB | none |
| 4 | Brain small calls | `voiceAskListen` 442, `voicePostEvent` 774 (from `voicePollSenses` 832), `voiceBrainRequest` 798 | voice, **and wifi_srv** via `/brain/voices` | < 2 KB | none |
| 5 | mDNS query | `voiceResolveBrain` 370 | voice | multicast, blocking | none |
| 6 | Cloud register / sync / OTA | `cloudPost` `cloud_client.h:521`, `cloudRegister` 1058, `cloudSync` 1214, `cloudOtaUpdate` 899, all from `pollCloudSync` 1568 | cloud | TLS ~43 KB / 60 s; OTA MBs | sets hold (1559/1564); `memRoomFor` 1647 |
| 7 | Manual register | `handleCloudSync` `web_server.h:2044` → `cloudRegister()` | **wifi_srv** | TLS | **no hold; can overlap #6** |
| 8 | WLED DDP / HTTP | `wled_display.h:275/308/346`; emoji `wled_emoji.h:376` | wifi_srv | frames | emoji only, on hold |
| 9 | Weather, geocode | `weather_data.h:279`; `handleInfoZip` `web_server.h:1779` | wifi_srv | small; blocks ≤ 6 s | none |
| 10 | ESP-NOW | `meshBroadcast` `esp_now_mesh.h:259` | wifi_srv | 1 packet / 3 s | none |
| 11 | mDNS responder, DNS | `web_server.h:3341`; `task_manager.h:486` | mdns (0,1); wifi_srv | small | — |
| 12 | Web responses | `server.handleClient` `task_manager.h:487` | wifi_srv | `/state` 2–3 KB per poll. Bulk: photo 2465, `/brain/lastwav` 3026 (≤ 384 KB), `/debug/screen` 2503 (150 KB), `/debug/crash?raw` 2605, `/debug/powerlog` 2929 (≤ 384 KB) | none |

**Web handlers that act on hardware or block (`web_server.h`, wifi_srv, core 0) [V].** Earlier
stages convert their own; stage 5 converts the rest. Already queued: brightness, expression,
say, hires, background, party, sound, volume, WiFi, WLED config.

| Handler | Line | Today | Posts instead | Owner |
|---|---|---|---|---|
| `handleState`, `handleScBatteryStatus` | 1315, 2371 | INA226 read under lock every poll (`scGetBatteryVoltage/Current`) | nothing: read BusAO's cached `SIG_POWER_SAMPLE` | BusAO |
| `handleScBaseLeds` | 2197 | I2C LED writes | `UI_LED_SET(idx, rgb)` | UiAO |
| `handleScBaseLedMode` | 2234 | writes `scLeds` globals cross-core | `UI_LED_MODE(mode, bri/speed)` | UiAO |
| `handleScHeadSetAngles`, `Recenter` | 2293, 2347 | `Motion.move*` direct | `MOTION_MOVE`, `MOTION_HOME` | MotionAO |
| `handleScHeadPreset` | 2318 | **blocking** nod/shake 1.2–1.5 s | `MOTION_GESTURE(kind)` | MotionAO |
| `handleScServoReinit` | 2357 | **blocking** rail cycle 2.1–4.6 s, no re-entry guard | `MOTION_RECOVER`; reply 202 | MotionAO |
| `handleDebugServo` | 2815 | servo pings under Motion lock (~0.5 s) | `MOTION_PROBE`; answer by event | MotionAO |
| `handleScChillToggle`, `Minutes` | 2403, 2412 | `scFireChillMode`: face, bubble, LEDs, head move | `TOUCH_CHILL(ms)` → `SIG_CHILL_ON/OFF` | TouchAO |
| `handleDebugTouchRaw` | 2732 | Si12T recal / sleep / start / cold writes | `TOUCH_DEBUG(cmd)` | TouchAO |
| `handleAudioFx` | 2101 | `M5.Mic.begin/end` | `AUDIO_FX(on)` | AudioAO |
| `handleSynthPort` | 2124 | MIDI UART re-pin | `AUDIO_SYNTH_PORT(p)` | AudioAO |
| `handleBrainListen`, `Wake`, `Play` | 3097, 3103, 3109 | voice flags | `VOICE_LISTEN`, `VOICE_WAKE`, `VOICE_PLAY` | AudioAO |
| `handleScPhotoCapture` | 2465 | camera init on wifi_srv, 30 KB DMA, 1 s bus hold | `CAM_PHOTO` → `SIG_CAMERA_DONE(buf)`, then a WEB_BULK lease | BusAO, NetAO |
| `handleScCameraEnable` | 2444 | NVS write + flag | `CAM_ENABLE(on)`; NVS via persist queue | BusAO |
| `handleDebugPower` | 2860 | holds I2C through `delay(500)`, AXP writes | `BUS_POWER_DEBUG(ext)` | BusAO |
| `handleScPowerOff` | 2421 | delays 500 ms, servos, LEDs, display, AXP off | publish `SIG_POWER_OFF`; BusAO cuts power last | all |
| `handleDebugScreen`, `PanelCheck` | 2503, 2527 | `botRenderHold` + `delay` polling | `UI_SNAPSHOT` → PSRAM copy, then WEB_BULK | UiAO, NetAO |
| `handleCloudSync` | 2044 | TLS on wifi_srv | `NET_CLOUD_SYNC` | NetAO |
| `handleBrainVoices`, `Voice` | 3050, 3062 | HTTP to brain on wifi_srv | `NET_JOB(brain)` | NetAO |
| `handleInfoZip` | 1779 | blocking geocode, `delay(10)` loop | `NET_JOB(geocode)` | NetAO |
| `handleDebugWifiDrop` | 3142 | `delay(100)` + `WiFi.disconnect` | `NET_DEBUG_DROP` | NetAO |
| `handleSetDeviceName`, `BrainConfig`, `Schedule`, `DebugPowerLog`, `BatteryLog` | 1896, 3075, 2153, 2929, 3153 | NVS / LittleFS writes from core 0 | persist queue (section 2) | NetAO job |

## 2. Design

**NetAO** (`vizbot/ao_net.h`) owns the TX plan and every transfer. It is three tasks, because
sockets block: the **NetAO** state machine (never blocks); **`net_job`**, which runs blocking
transfers one at a time, so upload and sync cannot overlap by construction; and the **web pump**
(today's wifi_srv), which may wait for a lease. Big payloads stay in their owner's buffer
(`voice.recBuf`, the JPEG, the snapshot), and the event carries a handle.

States: `OFFLINE` → `IDLE` ⇄ `STREAM` (wake burst open) ⇄ `BULK(kind)` → `OTA` (web upload: all
jobs held). Kinds by priority: `upload`, `fetch` > `web_bulk` > `cloud` > `weather`, `wled`.
- **One BULK at a time.** In BULK, wake chunks and WLED frames are dropped and ESP-NOW waits.
- **Cloud starts only when** voice is idle with no listen pending, `memRoomFor` passes and the
  TX back-off is clear. A voice job waiting behind cloud raises the job's priority to 1 and sets
  a cancel flag that `cloudPost`'s read loop checks. Voice waits at most 8 s.
- **Wake stream:** AudioAO posts `NET_WAKE_CHUNK(idx, flags)`. NetAO reads the PSRAM ring,
  paces pre-roll at 1 packet per 4 ms with at most 2 in flight, and keeps the errno-12 back-off.
- **Events.** In: `NET_CLOUD_SYNC`, `NET_JOB(kind, handle, bytes)`, `NET_WAKE_CHUNK`,
  `NET_LEASE(kind, bytes)`, `NET_DEBUG_DROP`, `tick` (100 ms). Out: `SIG_NET_TX_START/END(kind,
  bytes)` (per job, per wake burst, not per packet), `SIG_NET_JOB_DONE(job, status)` to the
  requester, `SIG_NET_LINK_UP/DOWN`, `SIG_MEM_DENY(tag, bytes)`.

**Budgets: internal RAM** (~137 KB at boot; 53 KB free, 31.7 KB largest at 2.6 min). TLS is
already in PSRAM (`memInit`); `SPIRAM_MALLOC_ALWAYSINTERNAL=4096` keeps mallocs ≤ 4 KB internal.

| Consumer | Size | When | Budget / rule | Checked by |
|---|---|---|---|---|
| WiFi static TX + RX | 8 + 8 × ~1.6 KB ≈ 26 KB [I] | always | fixed | — |
| WiFi dynamic RX + lwIP pbufs | up to ~26 KB per AMPDU burst [I] | bulk RX | one bulk RX at a time | NetAO |
| HTTP + socket | voice 1 + 4 KB, cloud 1 + 0.5 KB, 5.7 KB TCP send | per job | ≤ 12 KB per job | NetAO, `memRoomFor` |
| Static stacks + bufs (BSS) | wifi_srv, cloud, voice 8 KB each; `voiceIoBuf` 8 KB | always | fixed | — |
| Heap task stacks | loop 8, esp_timer 8, motion 4, mic 4, spk 2.5, tiT 4, mdns 4, Tmr 3 KB | mic/spk per begin | mic/spk allocated once (stage 3) | AudioAO |
| I2S DMA | mic 4 × 512 frames, spk 8 × 256 [size I] | per begin | allocated once | AudioAO |
| **Camera DMA** | **30,720 B contiguous** | per photo | start only if largest ≥ 36 KB and no BULK | BusAO |
| Web strings | `/state` 2–3 KB | per request | > 4 KB must stream | NetAO |
| Floor | — | always | min free ≥ 40 KB; largest ≥ 36 KB at idle | `ramLowTick` → `SIG_MEM_LOW` |

**WiFi:** our sends use ≤ 6 of 8 TX buffers, wake ≤ 2 in flight, sockets ≤ 10 of 16.
**Stacks:** high-water ≥ 25 %. **Where:** `mem.h` gets `memCheck(tag, internal, contiguous)`
(counts denials per tag) and `memNote(tag, bytes)` for library allocations (camera, I2S, WiFi);
`/debug/mem` reports budget, held, denials; `/debug/tasks` adds core, prio, run-time % [V].

**Task table** (today [V] → AO era):

| Task | Today core / prio / stack | AO era | Watchdog |
|---|---|---|---|
| wifi 23, esp_timer 22, tiT 18, sys_evt, ipc | 0 / IDF / IDF | unchanged | IDF |
| `motion_task` (BSP) | **any** / 10 / 4 KB | 0 / 10 (BSP patch kept in repo) | MotionAO beat |
| MotionAO | (loop) | 0 / 5 / 4 KB, tick 20 ms | handler > 50 ms logged, no beat 2 s → stall |
| BusAO | (scattered) | 0 / 6 / 4 KB | handler > 20 ms logged; camera window ≤ 1.2 s exempt |
| TouchAO | (loop) | 0 / 4 / 4 KB | no beat 1 s → stall |
| AudioAO | voice 0 / 1 / 8 KB | 0 / 4 / 8 KB | mic chunk gap > 500 ms → stall |
| `mic_task`, `spk_task` | **any** / 2 / 4, 2.5 KB | 1 / 5 via `cfg.task_pinned_core` (out of WiFi's way) | AudioAO |
| NetAO | (none) | 0 / 3 / 4 KB, queue 32 | handler > 10 ms logged, no beat 2 s → stall |
| `net_job` | cloud 0 / idle / 8 KB | 0 / 1 for voice jobs, 0 for cloud TLS / 8 KB | job deadline: upload 20 s, fetch 60 s, cloud 30 s, web 10 s |
| web pump (`wifi_srv`) | 0 / 1 / 8 KB | unchanged | hang 30 s → 15 s |
| UiAO (`loopTask`) | 1 / 1 / 8 KB | 1 / 3 / 8 KB | frame > 200 ms logged; hang 30 s |
| mdns, Tmr Svc | 0 / 1; any / 1 | unchanged | — |

Each AO records its current handler's start, longest handler and queue high-water. The hang
timer (`task_manager.h:573`) checks all AOs each second: soft limit → `SIG_AO_STALL(ao)`; 10 ×
(≥ 15 s) → `esp_system_abort(name)`. Task WDT covers IDLE0 only [V]; add AOs with `esp_task_wdt_add`.

**Flash cadence.** A write stops cache on both cores and PSRAM (render, wake ring, mic task);
an append may erase a sector, tens of ms to ~400 ms [I]; WiFi RX isn't in IRAM, so packets drop.
- **Power log:** sample at 1 Hz, buffer rows in RAM, write every 60 s; at once on BOOT,
  RAIL_*, battery < 3.5 V, USB loss, power-off. **Settings:** 2 s → 10 s debounce.
- **Persist queue:** every NVS / LittleFS write runs on `net_job` in a quiet window (no motion,
  BULK, mic start or camera), waiting at most 10 s.

## 3. Migration steps (each builds all 6 envs and works)

New code sits behind `NET_AO_ENABLED`, defined in `config.h` for `BOARD_HAS_STACKCHAN_BASE`
only. Other boards keep `cloudTask` + `wifi_srv`. `cloudNetHeld()`, `cloudNetHoldMs` and
`ksCloudStepMs` stay as shims that NetAO sets, so lcd-169 Kaleido and WLED emoji still work.
1. **Measure only.** Timeline events at the hold, `voicePostWav`, wake bursts, bulk web
   replies; internal free per `cloudPost` stage (the 41 KB dip); run-time % in `/debug/tasks`.
2. **NetAO + leases.** `cloudNetHoldBegin/End`, `voicePostWav`, `voiceStreamSpeak` take
   `netLease(kind)`: upload and sync now exclusive. Soak 4 h, no overlapping `TX_START` pairs.
3. **Cloud into `net_job`.** NetAO's tick schedules sync; `/cloud/sync` posts `NET_CLOUD_SYNC`
   (fixes #7); `otaUploadActive()` → state `OTA`; `cloudTask` under `#ifndef NET_AO_ENABLED`.
4. **Voice HTTP into jobs.** Upload, fetch, ask-listen, events, `/brain/voices`. AudioAO waits
   for `SIG_NET_JOB_DONE` instead of calling. Weather, geocode and WLED HTTP become jobs too.
5. **Wake stream TX into NetAO**, paced; drop `cloudNetHeld()` there. ESP-NOW, WLED DDP → tick.
6. **Remaining web handlers** from section 1 post events. Bulk responses take a `web_bulk`
   lease. Grep check: no `delay(`, `ScI2cLock`, `M5.` or `scChan.` left in `handle*`.
7. **Budgets:** `memCheck` at camera, cloud, upload, mic begin; `SIG_MEM_LOW`; `/debug/mem`.
8. **Tasks + watchdogs:** pin mic, spk, motion; priorities; stall supervisor; task WDT.
9. **Flash cadence:** persist queue; power-log batching.

## 4. Risks and unverified items

- **Voice waits behind cloud.** A TLS handshake at idle priority takes seconds of CPU [I];
  boost + cancel bound it, but `cloudPost` can only cancel between reads.
- **errno 12 may be lwIP, not WiFi:** pbuf pool or UDP mailbox (6) [I]. Pacing numbers are
  guesses; measure `sendFails` per hour.
- **The 41 KB dip is unexplained.** Suspects: AMPDU RX bursts, the `String` response growing
  through ≤ 4 KB internal reallocs, LittleFS caches. Budgets are provisional until step 1.
- **Camera block. Biggest RAM risk.** 30.7 KB contiguous internal DMA against a measured
  31.7 KB largest block. The PSRAM DMA mode is broken in this driver [V]. If the camera gets the
  block, the next mic begin or TLS socket may fail. Denying when largest < 36 KB makes photos
  fail softly. Stage 3's always-on mic is what keeps the block whole.
- **Wake ring overrun:** the ring holds 1.28 s; if NetAO lags, pre-roll is lost.
- **Core 0 crowding:** five AOs plus WiFi. If core 0 > 70 %, move TouchAO or MotionAO to core 1.
- **Patches and losses:** pinning `motion_task` needs a BSP patch kept in the repo (like
  M5GFX). The power log loses up to 60 s of beats on a sudden power cut.
- **Not verified:** I2S DMA and WiFi buffer bytes, flash erase time, whether
  `SPIRAM_TRY_ALLOCATE_WIFI_LWIP` moves dynamic RX buffers.

## 5. What to watch on the timeline and soak

- `SIG_NET_TX_START/END`: **zero overlaps** of `upload` with `cloud`; wake `sendFails` / h → 0.
- Upload lease wait p95 < 300 ms, max < 8 s; `lastBrainMs` and cloud success no worse.
- Internal min free ≥ 40 KB, largest ≥ 36 KB at idle, `SIG_MEM_DENY` count, photo failures 0.
- Per AO: stalls, queue drops, longest handler, stack ≥ 25 %; run-time % per core.
- Flash writes per hour, the duration of each, and none inside a motion, BULK or mic window.
- **Touch contacts against `NET_TX` windows.** This answers whether WiFi bursts at 19.5 dBm
  cause blips (collision 1). If they do, try 15 dBm on mac as a separate experiment.

## Measured 2026-11-22: the internal-RAM dips were WiFi TX buffers (fixed)

The RAM tracer (`env:stackchan-ramtrace`, `vizbot/ram_trace.cpp`, `GET /debug/ramtrace`)
wraps the allocator and freezes the allocations leading to each new internal-RAM low.
Arduino's WiFi uses dynamic TX buffers (no `WiFi.useStaticBuffers`), so every queued
packet holds a ~1.2 KB internal buffer until it is on the air. The wake pre-roll sent
32 packets 1 ms apart and queued 24 of them in 70 ms: free internal RAM 51.7 → 22.2 KB
idle, 3.8 KB when it landed in a voice turn. Fix (`voice_client.h`): the pre-roll waits
while more than `WAKE_TX_INFLIGHT` (6) packets' worth of internal RAM is held. After:
lowest 38.6 KB over 7 min including a full voice turn (the remaining dip is a ~2.4 KB
String in a web handler).
