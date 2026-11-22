# Stage 4 plan: BusAO + UiAO

Owner of the internal I2C bus (`M5.In_I2C`, port 1, SDA 12 / SCL 11) and of the screen + base LEDs.
Written 2026-11-22 from source (read-only). **[V]** = read in source, **[I]** = inferred, **[?]** =
not verified. Line numbers are the working tree (3.12.0 + uncommitted touch-context logging).
GitNexus was 11 commits behind HEAD, so every caller was confirmed with grep. M5GFX is read as the
locally patched 0.2.36 in `vizbot/.pio/libdeps/stackchan/M5GFX`.

## 1. Every I2C caller today

Loop = `loopTask`, core 1, prio 1, ~30 fps (`BOT_FRAME_PERIOD_MS` 33). Web = `wifi_srv`, core 0,
prio 1. Voice = `voice`, core 0, prio 1. "App lock" = `i2cMutex` (`task_manager.h:26-55`) via
`i2cAcquire` / `ScI2cLock`. **Every** `M5.In_I2C` / BSP / M5Unified transaction also takes M5GFX's
recursive port mutex for that one transaction (`common.inl:1564`, `:2375`, wait = `portMAX_DELAY`).

| Site | Device (addr) | kHz | Task / core | App lock | Rate |
|---|---|---|---|---|---|
| `stackchan_base.h:747` `scReadTouch` → BSP `Si12T.cpp:233` | Si12T 0x68 | 100 | loop 1 | **taken, result ignored**; BSP ignores the read error too | every frame |
| `stackchan_base.h:121` chip start (14 writes) via `stackchan_touch.h:126` heal, `:156` restart, `:176` boot, `web_server.h:2747` | Si12T | 100 | loop 1 / web 0 | checked (100–300 ms) | boot, heal, rail switch |
| `stackchan_base.h:94` reg dump, `web_server.h:2739` touchraw | Si12T | 100 | boot / web 0 | checked | debug |
| `stackchan_leds.h:294-296` → `stackchan_base.h:285/297` | PY32 0x6F LED RAM | 100 | loop 1 | **13 acquires per frame, all ignored** | 12 writes + refresh RMW = **14 tx/frame ≈ 420/s** |
| `stackchan_base.h:303`, `:389/:394` (recover rail), `web_server.h:2428` | PY32 VM_EN | 100 | loop 1 / web 0 | **ignored** | rail cycles |
| `stackchan_base.h:633/648` rest / wake, `:566` `sc5vSwitch`, `:574` USB probe | PY32 + AXP2101 0x34 | 100/400 | loop 1 | checked (500 ms) | battery events |
| `stackchan_base.h:692` battery tick, `power_log.h:134` sample, `:74` row | INA226 0x41, AXP, RTC | 400 | loop 1 | checked (20/150 ms) | 0.5 + 1 Hz |
| `stackchan_base.h:762/767` `scGetBattery*` ← `web_server.h:1422` `/state`, `:2376` | INA226 | 400 | web 0 | **ignored** | every `/state` poll |
| `stackchan_base.h:600`, `power_log.h:48` | AXP | 400 | setup | none (boot, alone) | once |
| `vizbot.ino:283-289` `readIMU` → `IMU_Class.inl:489` | BMI270 0x69 | 400 | loop 1 | **none**; skips on long-hold flag | 1 burst/frame [I] |
| `proximity_light.h:90-110` | LTR553 0x23 | 400 | loop 1 | **none**; skips on long-hold flag | 6 single reads @ 10 Hz |
| `touch_ui.h:1871` (`touch_control.h:514` legacy) `M5.update()` | FT6336 0x38 + AXP key (`M5Unified.inl:3783`, every call) | 400 | loop 1 | **runs even when not acquired** | 2 tx/frame |
| `display_lcd.h:482` `setLCDBacklight` (M5GFX `Light_M5StackCoreS3`, DLDO1 RMW + write) ← `task_manager.h:309`, `touch_ui.h:748` | AXP | 400 [I] | loop 1 | **none** | on change |
| `voice_client.h:239-262, 651-656, 893, 967-969, 1085`; `audio_spectrum.h:134/153/160`; `bot_sounds.h:869` | ES7210 0x40, AW88298 0x36 (+AW9523) via M5Unified enable callbacks (`M5Unified.inl:986`, `:601`) | 400 | voice 0 / loop 1 / web 0 | **none** | ~4 codec inits per voice turn |
| `web_server.h:2873-2917` `/debug/power` | AXP | 400 | web 0 | checked, **held across `delay(500)`** | debug |
| `web_server.h:2212/2218` LED set, `:2429-2440` power off | PY32, AXP | 100/400 | web 0 | **ignored / none** | rare |
| `stackchan_camera.h:151` start, `:164` stop → `esp_camera_init/deinit` (IDF `i2c_master`, SCCB) | GC0308 0x21 | 100 [?] | web 0 | `i2cAcquire(1000)` + long-hold flag; **runs init even if not acquired**; **outside the M5GFX mutex** | per photo, ~1 s window |

**Findings [V]:**
- The M5GFX mutex already serialises every In_I2C transaction from every task, M5Unified included.
  The camera's IDF driver is the only code that drives the controller outside it. The patch says so
  (`common.inl:2105-2109`: "the two sides are not serialized … the application must not run them
  concurrently"). The app lock only adds multi-transaction atomicity and the camera fence.
- `M5.update()` is not "skipped if the lock is busy" (interactions doc): it runs unlocked.
- LED refresh is a read-modify-write (`PY32IOExpander.cpp:306`), so it is 14 tx/frame, not 13.
  PY32 has a bulk write (`setLedData`, `:299`: one 24-byte write to 0x30) that BSP never uses.
- Total ≈ 600 tx/s at idle [I]; LEDs are ~70 % of it.

## 2. Design

### Recommendation: one correct lock, plus BusAO as owner of the schedule. Not a request queue.

The one lock is **M5GFX's own port mutex**, exported by a two-function addition to the stage-0
patch (`lgfx::i2c::lockPort(port, ms)` / `unlockPort(port)`, plus max-wait / max-hold stats).
Reasons:
1. **Nothing can bypass it.** Every BSP, M5Unified and M5GFX transaction already takes it
   internally. An app-level queue or app mutex is bypassed by any library call (codec, backlight,
   `M5.update`), which is exactly today's gap. Only the camera needs wrapping, and it is one place.
2. **Library calls stay where they belong.** A queue would have to run `M5.Mic.begin()` (I2S, DMA,
   task creation) or `esp_camera_init` on BusAO, and AudioAO would wait on BusAO: a cross-AO wait.
3. **Latency.** A transaction holds the mutex 0.1–0.5 ms. Touch and screen touch keep frame
   latency with no round trip, and `M5.Touch` state stays on the task that reads it.
4. **Never block.** Every AO takes it with `busTryLock()` (timeout 0) and skips or re-posts itself
   on failure. Only the camera worker waits (it blocks for ~1 s inside `esp_camera_init` anyway).

BusAO (core 0, prio 3, queue 16, `ao_bus.h`) owns the *schedule* and the app-owned devices:
- **Tick 33 ms**, in this order: Si12T read (100k) → `SIG_TOUCH_SAMPLE` (a = byte, b = ok) to
  TouchAO; LED frame push (100k) if changed; then the 400k group: IMU burst every tick, LTR553 every
  3rd, INA226 + AXP VBUS/VSYS every 30th. Results go to an owner-held snapshot (seqlock) that old
  getters read; the web never touches the bus for `/state`.
- **Requests:** `SIG_BUS_LED_FRAME` (UiAO's double buffer), `SIG_BUS_TOUCH_START` (TouchAO),
  `SIG_BUS_RAIL_SET` a = VM_EN/EXT5V, b = on (MotionAO / battery care; MotionAO still publishes
  `SIG_RAIL_SWITCH`), `SIG_BUS_WINDOW_REQ/END` (camera), `SIG_BUS_POWER_OFF`.
- **Out:** `SIG_BUS_DONE` (a = request, b = ok), `SIG_CAMERA_START/END`, `SIG_BUS_FAULT`
  (a = device, b = consecutive fails).
- **Rules:** a failed try-lock or failed read is dropped, never delivered as a value. Si12T bytes
  with bits 6-7 set are rejected [I: reserved bits]. Mixed speeds: 100k group first, then 400k,
  never interleaved within a tick.

### UiAO (core 1: the loop task's frame section becomes `onEvent`)

States: `BOOT` → `RUN` ⇄ `CAPTURE` (frame held for a reader) → `OFF` (power-off, display dark).
`MENU` stays a sub-mode of `RUN` (the touch UI owns it).

| Event in | From | Action |
|---|---|---|
| `SIG_TICK_FRAME` | own timer | `M5.update()` under `busTryLock` (skip on fail), render, SPI push, compute LED frame; if any RGB565 differs from the last pushed frame, post `SIG_BUS_LED_FRAME` |
| `SIG_UI_CMD` | old command queue (shim) | apply between frames (unchanged behaviour) |
| `SIG_SCREEN_CAPTURE_REQ` | web | finish the current frame, enter `CAPTURE`, reply `SIG_SCREEN_CAPTURE_READY` (a = buffer id) |
| `SIG_SCREEN_CAPTURE_DONE` / 2 s timeout | web / tick | back to `RUN` |
| `SIG_LED_MODE`, `SIG_LED_SET` | web, settings | change mode or solid colour (no direct PY32 write from web) |
| `SIG_BACKLIGHT` | auto-brightness, settings | AXP write under `busTryLock`, retry next tick |
| `SIG_POWER_OFF` | web, touch UI, battery | blank screen + LEDs, then `SIG_BUS_POWER_OFF` |

Out: `SIG_LED_PUNCH` (a = peak level 0-255, b = ms) when `scLeds.flash()` starts and on the rising
edge of a beat punch above a threshold, published *before* the frame is pushed; `SIG_BUS_LED_FRAME`.
**Render hold fix:** the check-then-set race (`bot_mode.h:833` vs `web_server.h:2508/2532`, plus
the 100 ms give-up) disappears: only UiAO reads or writes the frame state; the web asks and waits
for `READY`.

## 3. Migration steps (each builds all 5 envs; bench version `3.12.1.busN`)

Guards: BusAO, the lock export and LED dedupe are `#ifdef BOARD_HAS_STACKCHAN_BASE`. `m5cores3` and
`faces` (CoreS3 without the base) and `lcd-169` / `lcd-13` keep `i2cMutex` and `i2cAcquire` as is
(`touch_control.h:191`, `vizbot.ino:291`). The shared LED engine changes only behind the backend.

1. **Instrument (no behaviour change).** Timeline counters: bus tx/s per device, LED pushes/s,
   "I/O after failed lock" count (each ignored-result site above), lock max wait / max hold, camera
   window ms. Baseline cold boot + 1 h. Needs stage 0 (patch in repo) and stage 1 (timeline).
2. **Stop using the bus without the lock.** Every ignored-result site checks `held` and skips
   (keep the last good value; touch: no sample, not a stale one). `M5.update()` runs only when held.
   `readIMU`, prox and backlight take the lock with a 0-20 ms try. Camera aborts start if not held.
3. **LEDs: write only on change.** Stackchan backend gets `scPushBaseLedFrame()`: RGB565 compare
   with a shadow, one bulk write to 0x30 (24 bytes) + one refresh write (cached `LED_CFG`, read
   once at boot), forced re-push every 2 s. Publish `SIG_LED_PUNCH`. Still called from the loop.
   **Soak 4 h** here: the biggest single bus change.
4. **One lock.** Add `lockPort/unlockPort` + stats to the patch; on stackchan `i2cAcquire/Release`
   become wrappers onto it. Camera start/stop hold it for the whole window; delete the long-hold
   flag (`task_manager.h:38`) because every other path now try-locks and skips.
5. **BusAO.** Create `ao_bus.h`; move Si12T, IMU, prox, INA226/AXP sampling and the LED push into
   its tick. Old entry points (`scReadTouch`, `readIMU`, `proxLight.update`, `scGetBattery*`,
   `plogTick` samples) read the snapshot. TouchAO switches to `SIG_TOUCH_SAMPLE`.
6. **Writes as requests.** Chip start, VM_EN, EXT5V, power-off, `/debug/power ?ext`, web LED set
   post to BusAO / UiAO. `/debug/power` drops its `delay(500)` under the lock.
7. **UiAO.** Frame section of `loop()` becomes UiAO `onEvent`; capture handshake replaces
   `botRenderHold`; web power-off stops calling `gfx->flushCanvas()` from core 0.
8. **Remove shims.** `ScI2cLock` and `i2cMutex` leave the stackchan build. Cold boot + **4 h soak**.

## 4. Risks and unverified items

1. **M5GFX waits forever inside a transaction.** A bare library bus call (e.g. a codec init) from
   an AO during the camera window blocks that AO ~1 s. Mitigation: try-lock wrappers everywhere,
   plus the max-wait stat; any wait > 20 ms on an AO task is a bug on the timeline.
2. **Core 0 CPU.** M5GFX polls with `taskYIELD()` (`common.inl:1806`), so BusAO at prio 3 spins
   through each transaction; the 24-byte LED write at 100k is ~2.5 ms. Measure; fall back to prio 1
   or batch the LED push every other frame.
3. **Camera bus coexistence [?].** Which driver created the IDF bus on port 1, and whether the
   `foreign_bus` path is active in this Arduino build (`common.inl:2111`) is unknown. If not, the
   register save/restore around the camera may not cover its changes. Log `foreign_bus` at boot.
4. **Patch surface.** The lock export makes stage 0's patch larger; a library update drops it.
   The build must fail loudly (static assert on the symbol), not fall back to no lock.
5. **[?] items:** Si12T / PY32 tolerance of 400k traffic to other addresses; whether PY32 needs a
   periodic refresh after a brown-out; Si12T reserved bits; LTR553 burst read (auto-increment);
   FT6336 polled every `M5.update()` or only on INT; camera SCCB speed.
6. Cached battery values on `/state` are up to 1 s old (was: fresh, but read unlocked).

## 5. What to watch on the timeline and soak

- Bus tx/s: ~600 → under 150 at idle (rainbow ≈ 60 LED tx/s; static modes ≈ 1/2 s).
- "I/O after failed lock" = 0 from step 2 on. Lock max hold < 2 ms outside camera windows.
- Try-lock skips per AO per minute; any AO wait > 20 ms.
- `SIG_LED_PUNCH` vs touch contacts: share of contacts within 300 ms of a punch (before/after).
- Si12T read fails and rejected bytes; `SIG_BUS_FAULT`; M5GFX bus-recover count.
- Camera window duration and the frame-time histogram during it (was: loop stalls up to 3 s).
- UiAO frame time and skipped frames; capture timeouts; LED `updateMaxUs`.
- Scorecard: contacts/h, heals/h, reboots: no row worse than the stage-3 soak.

> **Correction 2026-11-22:** there is no stage-0 M5GFX patch: M5GFX is the stock 0.2.36 release. Exporting the per-port mutex means a real patch file applied at build (or a public M5GFX API, if one exists). Decide that at the start of stage 4.
