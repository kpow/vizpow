# vizBot — Firmware Technical Reference

vizBot is the primary ESP32-S3 firmware target in the vizPow platform: an animated
desktop-companion character rendered on LCD displays, with procedural faces, tween-based
animation, speech bubbles, weather overlays, WLED integration, ESP-NOW mesh, and a
captive-portal web UI, on a dual-core FreeRTOS architecture.

This document is a technical reference for development and feature planning, not a user guide.

## Contents

- [Hardware Targets](#hardware-targets)
- [Architecture](#architecture)
- [Modes & Overlays](#modes--overlays)
- [File Guide](#file-guide)
- [Peripheral Inventory](#peripheral-inventory)
- [Expressions (25)](#expressions-25)
- [Personality System](#personality-system)
- [Tween Animation Engine](#tween-animation-engine)
- [Web Control Panel](#web-control-panel)
- [HTTP API Surface](#http-api-surface)
- [Configuration & Persistence](#configuration--persistence)
- [WLED Integration](#wled-integration)
- [vizCloud Integration](#vizcloud-integration)
- [ESP-NOW Mesh](#esp-now-mesh)
- [Graphics Stack](#graphics-stack)
- [Core S3 Extras](#core-s3-extras)
- [Building](#building)
- [Version History](#version-history)
- [Licenses](#licenses)

## Hardware Targets

A board is selected by a `BOARD_*` build flag set per PlatformIO env (see [Building](#building)).
`config.h` derives a `TARGET_*` from each board flag.

| Board flag | PlatformIO env | Target | Display | Touch | Notes |
|---|---|---|---|---|---|
| `BOARD_ESP32S3_LCD_169` | `lcd-169` | `TARGET_LCD` | 240x280 ST7789V2 | CST816T | Primary dev board |
| `BOARD_ESP32S3_LCD_13` | `lcd-13` | `TARGET_LCD` | 240x240 ST7789VW | none | Battery powered, no touch |
| `BOARD_M5CORES3` | `m5cores3` | `TARGET_CORES3` | 320x240 ILI9342C | FT6336 | Audio, proximity, MIDI synth |
| `BOARD_M5CORES3` + `BOARD_HAS_STACKCHAN_BASE` | `stackchan` | `TARGET_CORES3` | 320x240 ILI9342C | FT6336 | StackChan K151-R flagship (3.0) |
| `BOARD_ESP32S3_MATRIX` | `matrix` | `TARGET_LED` | 8x8 WS2812B only | none | LED-only, 4MB flash |

All LCD targets use `DisplayProxy`, which wraps LovyanGFX (or M5Unified's internal LovyanGFX)
behind a unified `beginCanvas()`/`flushCanvas()` double-buffered API.

### StackChan Target (vizBot 3.0, in progress)

The M5Stack StackChan (K151-R) is the new flagship target, added as an *extension* of
`BOARD_M5CORES3` via `BOARD_HAS_STACKCHAN_BASE` (PlatformIO env `stackchan`). All
StackChan-specific code is gated by the extension flag — bare Core S3 builds remain unchanged
and license-clean. See [Licenses](#licenses) for the GPL-3.0 obligation.

**Current status (alpha.1):** The StackChan build boots vizBot on the StackChan hardware with
all existing features working (face, personalities, WLED, captive portal, mesh). New
StackChan-specific subsystems are **stubbed** — present in the boot diagnostics as DEFER, with
driver bring-up planned for Phase 2+.

| Subsystem | Boot stage | Status | Driver phase |
|---|---|---|---|
| PY32L020 IO expander (0x6F) | IO Exp | DEFER | Phase 2 |
| VM_EN servo power rail | VM_EN | DEFER | Phase 2 |
| SCS0009 yaw servo | Servo X | DEFER | Phase 2 |
| SCS0009 pitch servo | Servo Y | DEFER | Phase 2 |
| WS2812C 12-LED base ring | Base LED | DEFER | Phase 2 |
| Si12T head touch (0x68) | Touch SC | DEFER | Phase 2 |
| INA226 battery monitor (0x41) | Battery | DEFER | Phase 2 |
| GC0308 camera | Camera | DEFER | Phase 4 |
| LittleFS photo storage | Photo FS | DEFER | Phase 4 |

**Stub HTTP endpoints** (return 501 with `{"status":"deferred","phase":N}`):

| Endpoint | Phase |
|---|---|
| `/bot/head/set_angles` | 2 |
| `/bot/head/preset` | 2 |
| `/bot/head/recenter` | 2 |
| `/bot/base_leds/set` | 2 |
| `/bot/battery/status` | 2 |
| `/bot/photo/capture` | 4 |
| `/bot/photos` | 4 |
| `/bot/photo/get` | 4 |
| `/bot/photo/delete` | 4 |

## Architecture

```
Core 0 (Protocol CPU)                Core 1 (Application CPU)
─────────────────────────            ─────────────────────────
wifiServerTask (8KB static BSS)      Arduino loop()  (~30 FPS, BOT_FRAME_DELAY_MS)
├── pollWifiConnectTask()            ├── readIMU()
├── pollWledDisplay()                ├── handleTouch()
├── pollWeatherFetch()               ├── tweenManager.update()
├── pollCloudSync()         (TLS)    ├── drainCommandQueue()  ←── FreeRTOS Queue (depth 8)
├── pollScheduledCommands()          ├── runInfoMode() OR runBotMode()
├── pollScheduledContent()           └── showDisplay()  (FastLED.show / renderToLCD)
├── pollMeshBroadcast()    (ESP-NOW)
├── dnsServer.processNextRequest()
├── server.handleClient()    (HTTP)
└── vTaskDelay(2ms)
```

**Why dual-core?** FastLED disables interrupts during `show()` (2-5ms), which conflicts with
WiFi radio timing. WiFi/network on Core 0 and rendering on Core 1 eliminates dropped connections
and frame stutters.

**Boot.** `setup()` runs `runBootSequence()` (`boot_sequence.h`), which initializes each
subsystem as its own diagnostic stage drawn to the LCD with a pass/fail indicator (9 stages on
LCD/Matrix boards, more on Core S3). Results populate the global `SystemStatus sysStatus`, which
the rest of the firmware checks before touching hardware so dead/absent peripherals are skipped
rather than crashing.

**Cross-core communication:**
- **Command Queue** (FreeRTOS, depth 8) — web/cloud/touch handlers push `Command` structs;
  the render loop drains them atomically between frames (`drainCommandQueue()`). Handlers never
  mutate render state or touch I2C directly.
- **I2C Mutex** (FreeRTOS semaphore) — IMU and touch share the I2C bus (`i2cAcquire()`/`i2cRelease()`).
- **Volatile flags** — `wledData.sendState`, `meshScanRequested`, etc.

## Modes & Overlays

There is no general mode enum. The render loop dispatches exactly two top-level states:

| State | Entry | What it draws |
|---|---|---|
| **Bot** (default) | `runBotMode()` when `!infoMode.active` | The procedural face. Sub-state machine `BotState` = `BOT_ACTIVE` / `BOT_IDLE` / `BOT_SLEEPING`. |
| **Info** | `runInfoMode()` when `infoMode.active` | Weather dashboard (mini eyes, 3-day forecast). Toggled by sustained shake or `/info/toggle`. |

Two features layer *on top of* bot mode and are sometimes referred to loosely as "modes":

- **Ambient background ("viz")** — one of 11 ambient effects renders behind the face, auto-cycled.
- **Time overlay ("clock")** — a clock drawn over the face, toggled via `/bot/time`.

> Note for planning: adding a genuinely new mode (e.g. a future camera mode) requires building a
> dispatch layer; today the loop is a two-way branch, not a registry.

## File Guide

### Core

| File | Purpose |
|------|---------|
| `vizbot.ino` | Entry point — `setup()`, `loop()`, state dispatch, command drain, shake detection |
| `config.h` | Board selection, pin definitions, target-level config, firmware identity, WiFi/cloud constants |
| `settings.h` | NVS persistence layer with debounced writes (2s after last change) |
| `device_id.h` | Per-device unique SSID/mDNS from eFuse MAC or user-set custom name |
| `system_status.h` | `SystemStatus` struct — tracks subsystem health (IMU, touch, WiFi, etc.) |
| `boot_sequence.h` | Visual LCD boot diagnostics (per-subsystem stages with pass/fail) + defines `sysStatus` |
| `task_manager.h` | FreeRTOS tasks, I2C mutex, command queue, `drainCommandQueue()` |
| `partitions.csv` | Custom partition table (+2MB app space on 4MB flash boards) |

### Face & Display

| File | Purpose |
|------|---------|
| `bot_faces.h` | 25 expression definitions (`BotExpression` structs) + LERP interpolation |
| `bot_eyes.h` | Eye/pupil/brow/mouth rendering, look-around, blink system, face color |
| `bot_overlays.h` | Speech bubbles, time overlay, weather overlay, notification banners |
| `layout.h` | Resolution-independent UI positions (derived from `LCD_WIDTH`/`LCD_HEIGHT`) |
| `display_lcd.h` | LovyanGFX init, `DisplayProxy` struct, `beginCanvas()`/`flushCanvas()` |
| `tween.h` | `TweenManager` — 16-slot animation engine with 8 easing functions |

### Bot Behavior

| File | Purpose |
|------|---------|
| `bot_mode.h` | Bot state machine (Active/Idle/Sleeping), personality system, update/render pipeline |
| `bot_sayings.h` | Saying categories + phrases (greetings, idle, reactions, time-of-day) |
| `bot_sounds.h` | Sound effect + MIDI sequence system for Core S3 (boot chime, tap boop, etc.) |

### Web & Network

| File | Purpose |
|------|---------|
| `web_server.h` | Neo-brutalist web UI (PROGMEM HTML/CSS/JS) + all HTTP endpoint handlers |
| `wifi_provisioning.h` | AP+STA dual mode, captive portal, credential NVS storage, scan/connect |
| `ota_update.h` | OTA firmware upload + GitHub release check, boot-valid marking (rollback guard) |
| `cloud_client.h` | vizCloud HTTPS client — registration, sync, command dispatch, TLS pinning |
| `content_cache.h` | LittleFS caching for cloud content (sayings, personalities, metadata) |
| `esp_now_mesh.h` | ESP-NOW mesh — state broadcast, coordinated WLED, peer tracking |

### WLED Integration

| File | Purpose |
|------|---------|
| `wled_display.h` | DDP pixel control (32x8), state capture/restore, cross-core queue, hologram mode |
| `wled_emoji.h` | Emoji sprite slideshow on WLED matrix with fade transitions |
| `wled_font.h` | 3x5 pixel font for rendering text into the 32x8 pixel buffer |
| `wled_weather_view.h` | Weather card cycling on WLED |
| `wled_scheduled_content.h` | Periodic weather/emoji content cycling on WLED |
| `emoji_sprites.h` | Pixel art sprite data (palette-indexed compression) |

### Effects, Sensors, Data

| File | Purpose |
|------|---------|
| `effects_ambient.h` | 11 ambient effects with hi-res LCD variants (plasma, fire, ocean, aurora, etc.) |
| `palettes.h` | 15 color palette definitions |
| `touch_control.h` | Touch menu gestures and UI (long-press, swipe, shared I2C mutex) |
| `audio_analysis.h` | Mic audio analysis — spike/speech/silence detection (Core S3 only) |
| `proximity_light.h` | Proximity/ambient light sensor reactions (Core S3 only) |
| `midi_synth.h` | SAM2695 MIDI synth driver (Core S3 Grove Port C) |
| `info_mode.h` | Weather dashboard with mini eyes, 3-day forecast bar graph, page dots |
| `weather_data.h` | Open-Meteo API client, geocoding, forecast parsing, NTP time sync |
| `weather_icons.h` | Weather condition icons (44px sprites for info mode) |

## Peripheral Inventory

I2C runs at the pins below per board; the IMU and touch controller share the bus (guarded by the
I2C mutex). On Core S3, M5Unified owns and drives the internal bus.

| Board | I2C SDA / SCL | IMU | Touch | LED data | Display bus |
|---|---|---|---|---|---|
| LCD 1.69 | 11 / 10 | QMI8658 @ 0x6B | CST816T @ 0x15 | GPIO14 (ext) | SPI: SCK6 MOSI7 CS5 DC4 RST8 BL15 |
| LCD 1.3 | 47 / 48 | QMI8658 @ 0x6B | none | GPIO14 (ext) | SPI: SCK40 MOSI41 CS39 DC38 RST42 BL20 |
| Core S3 | 12 / 11 | BMI270 (M5Unified) | FT6336 @ 0x38 | GPIO38 (onboard RGB) | ILI9342C (M5Unified) |
| Matrix | 11 / 12 | — | none | GPIO14 (8x8 WS2812B) | none |

**Core S3 additional:** SAM2695 MIDI synth on Grove Port C (TX 17 / RX 18 @ 31250 baud);
mic, speaker, proximity/light, and AXP2101 PMU managed by M5Unified.

**Common:** `NUM_LEDS` 64, matrix 8x8. WiFi AP SSID base `vizBot` (+ 4-hex MAC suffix),
password `12345678`, mDNS base `vizbot` → `vizbot-xxxx.local`.

## Expressions (25)

| Idx | Name | Idx | Name | Idx | Name |
|--|--|--|--|--|--|
| 0 | Neutral | 9 | Excited | 18 | Winking |
| 1 | Happy | 10 | Mischievous | 19 | Devious |
| 2 | Sad | 11 | Skeptical | 20 | Shocked |
| 3 | Surprised | 12 | Worried | 21 | Kissing |
| 4 | Chill | 13 | Confused | 22 | Nervous |
| 5 | Angry | 14 | Proud | 23 | Glitching |
| 6 | Love | 15 | Shy | 24 | Sassy |
| 7 | Dizzy | 16 | Annoyed | | |
| 8 | Thinking | 17 | Focused | | |

Expressions are `BotExpression` structs in `bot_faces.h` (eye mode, pupil size, brow angle, mouth
shape, etc.). Transitions LERP over configurable durations.

## Personality System

| Personality | Idle timeout | Expression rate | Say rate | Say chance | Favorites |
|---|---|---|---|---|---|
| **Chill** (default) | 90s | 4-10s | 16-40s | 30% | Neutral, Happy, Thinking, Mischief, Winking, Shy, Proud, Sassy |
| **Hyper** | 180s | 2-6s | 8-24s | 45% | Happy, Excited, Surprised, Love, Proud, Winking, Kissing, Sassy |
| **Grumpy** | 45s | 6-18s | 20-55s | 30% | Angry, Annoyed, Mischief, Skeptical, Devious, Nervous, Glitching, Focused |

Each personality also has favorite palettes and ambient effects for background cycling.
**Cloud personalities** (slots 3-11) sync from vizCloud, cache to LittleFS, and load into
`runtimePersonalities[]` (12 total max). **Rotation** cycles a configurable list at an interval
(default 5 min); setting a single personality stops rotation.

## Tween Animation Engine

`tween.h`:
- **16 concurrent slots** driving a float from start to end over a duration
- **8 easing functions**: Linear, InQuad, OutQuad, InOutQuad, OutCubic, OutBounce, OutElastic, OutBack
- **Auto-eviction**: when full, the oldest tween snaps to its end value and is replaced
- **Deduplication**: re-tweening an active target reuses its slot

Drives expression transitions, info-mode enter/exit, overlay fades, and eye look-around.

## Web Control Panel

Embedded as a PROGMEM string in `web_server.h`. **Neo-brutalist** design: 3px black borders, hard
zero-blur offset shadows, square corners, saturated accents on a warm cream background. Two-column
dashboard (60/40 desktop, single-column mobile) with collapsible sections persisted to localStorage.

- **Left:** Expressions (25-button grid), Say Something, Personality (+ rotation), Appearance
  (face color, background style, ambient effects), WLED Sprites.
- **Right:** Device (brightness, volume, time overlay, hi-res toggle), Weather & Info, WiFi
  provisioning, WLED Display config.

## HTTP API Surface

All endpoints served on port 80 via the captive-portal AP (`vizBot-XXXX` / `12345678`) and over STA.
Most write endpoints take query args and return `text/plain` `"OK"`; read endpoints return JSON.

| Endpoint | Method | Params | Returns |
|---|---|---|---|
| `/` | GET | — | HTML web UI |
| `/state` | GET | — | JSON full device state |
| `/brightness` | GET | `v` 1-255 | OK |
| `/bot/expression` | GET | `v` 0-24 | OK |
| `/bot/say` | GET | `text`, `dur` 1000-10000 | OK |
| `/bot/time` | GET | `v` 0/1/2 (2=toggle) | OK |
| `/bot/hires` | GET | `v` 0/1 | OK |
| `/bot/background` | GET | `v` or `style` 0-4 | OK |
| `/bot/ambient` | GET | `v` 0..N-1 | OK |
| `/bot/personality` | GET/POST | GET: list JSON; set: `v` index | OK / JSON |
| `/bot/personality/rotation` | POST | JSON body (list + interval) | OK |
| `/bot/sound` | GET | `seq` id, or `freq`+`dur` | OK |
| `/bot/volume` | GET | `v` 0-255 | OK |
| `/bot/sequences` | GET | — | JSON MIDI sequence list |
| `/bot/mic` | GET | — | JSON mic analysis |
| `/info/toggle` | GET | — | OK |
| `/info/location` | GET | `lat`, `lon` | OK |
| `/info/zip` | GET | `zip` | JSON geocode result |
| `/wifi/scan` | GET | — | OK (results polled via `/wifi/status`) |
| `/wifi/connect` | GET | `ssid`, `pass` | OK |
| `/wifi/status` | GET | — | JSON WiFi/provisioning state |
| `/wifi/reset` | GET | — | OK |
| `/device/name` | GET | `name` (empty clears) | text status |
| `/wled/status` | GET | — | JSON WLED state |
| `/wled/config` | GET | `ip`,`on`,`speed`,`ix`,`hologram`,`r`,`g`,`b` | OK |
| `/wled/test` | GET | — | OK |
| `/wled/emoji/add`,`/remove`,`/clear`,`/toggle`,`/settings` | GET | varies (`v`,`cycle`,`fade`) | OK |
| `/cloud/status` | GET | — | JSON cloud registration/sync |
| `/cloud/sync` | GET | — | text status |
| `/schedule` | GET | `enabled`, `intervalMin` 1-120 | JSON schedule state |
| `/update` | GET/POST | OTA upload form / firmware POST | HTML / result |
| captive-portal probes | GET | `/generate_204`, `/hotspot-detect.html`, etc. | 302 redirect |

> New endpoints added in 3.0 are single-purpose, JSON-in/JSON-out, and tool-named
> (e.g. `/bot/head/set_angles`) so they can be wrapped by a future MCP server without an API rewrite.

## Configuration & Persistence

Settings persist to NVS (`Preferences`, namespace `vizbot`) with debounced writes — dirty state is
flushed 2s after the last change via `flushSettingsIfDirty()`.

| NVS key | Field | Notes |
|---|---|---|
| `bright` | `brightness` | LED brightness 1-255 |
| `lcdBr` | `lcdBrightness` | LCD backlight |
| `effect` | `effectIndex` | Ambient effect |
| `palette` | `paletteIndex` | Palette |
| `autoCyc` | `autoCycle` | Auto-cycle effects/palettes |
| `bgStyle` | `botBackgroundStyle` | Bot background style 0-4 |
| `hiRes` | `hiResMode` | Hi-res ambient |
| `wLat` / `wLon` | `weatherLat` / `weatherLon` | Weather location |
| `sndOn` / `sndVol` | sound enabled / volume | Core S3 |

Other namespaces: `vizwifi` (WiFi credentials), `vizcloud` (cloud meta). WLED + schedule settings
have their own load/save paths (`loadWledSettings()`, `loadScheduleSettings()`).

## WLED Integration

vizBot drives a WLED 32x8 matrix via **DDP**:
1. Speech text rendered to a 32x8 buffer with a 3x5 font.
2. Multi-word phrases split and sequenced one word at a time.
3. Pixels sent as one UDP packet (10-byte DDP header + 768 bytes RGB).
4. WLED enters realtime mode; vizBot restores the previous effect over HTTP afterward.

**Palette sync** maps WLED's current palette to a local index. **Hologram mode** horizontally
mirrors LCD + WLED buffer for Pepper's-ghost prisms. **Mesh coordination** defers DDP sends when a
mesh peer is using the same WLED target.

## vizCloud Integration

HTTPS to a DigitalOcean App Platform server:
- **TLS pinning**: GTS Root R4 (Google Trust Services), *not* `esp_crt_bundle` (crashes generic ESP32-S3).
- **Registration**: POST `/api/bots/register` (MAC, hardware type, firmware version, capabilities).
- **Sync polling**: POST `/api/bots/{id}/sync` (default 60s).
- **Commands**: expression, say, personality, brightness, background, ambient_effect, sound, volume, sleep, reboot, mesh_scan.
- **Scheduled commands**: ISO-8601 `execute_at`, 8 slots.
- **Content sync**: sayings + personalities cached to LittleFS.
- **Telemetry**: expression, personality, RSSI, heap, uptime, NTP, IMU, lux, mesh peers.

Runs cooperatively inside `wifiServerTask` on Core 0 — TLS serialized with WLED HTTP to limit heap
fragmentation. See `FIRMWARE-INTEGRATION.md` for the full spec.

## ESP-NOW Mesh

Peer-to-peer mesh between vizBots (`esp_now_mesh.h`): periodic state broadcast, peer discovery +
stale eviction, coordinated WLED to prevent DDP collisions, and deferred speech (LCD bubble delayed
while a peer uses the shared WLED).

## Graphics Stack

- **LovyanGFX** for `TARGET_LCD` (custom LGFX, DMA SPI @ 40MHz, ST7789V2/VW).
- **M5Unified** for `TARGET_CORES3` (wraps LovyanGFX).
- **DisplayProxy** unifies the API: `beginCanvas()`, `flushCanvas()`, `fillRect()`, `drawLine()`, etc.
- **Double-buffering**: all rendering goes to an offscreen `LGFX_Sprite`, flushed in one atomic SPI transfer.
- **Resolution-independent layout** (`layout.h`) from `LCD_WIDTH`/`LCD_HEIGHT` at compile time.

## Core S3 Extras

- **Audio analysis** (`audio_analysis.h`): mic detects spikes (clap → Surprised), speech (→ Focused), silence (→ Chill).
- **Proximity/light** (`proximity_light.h`): hand-approach (→ Surprised/Shy), peek-a-boo, sustained cover (→ Worried).
- **Sound + MIDI** (`bot_sounds.h`, `midi_synth.h`): boot chime, reactions, and SAM2695 MIDI sequences.
- **Auto-brightness**: ambient light sensor adjusts the LCD backlight.

## Building

Builds use **PlatformIO** (`platformio.ini` in this folder). One env per board:

```bash
pio run -e lcd-169       # build
pio run -e m5cores3 -t upload    # build + flash
pio run -e lcd-13 -t upload -t monitor
```

A post-build script (`name_firmware.py`) publishes `vizbot-<env>-v<FIRMWARE_VERSION>.bin` (and a
merged `-factory.bin`) into `../builds/`. The version comes from `FIRMWARE_VERSION` in `config.h`;
the board portion of the name comes from the env name.

**Per-board gotchas:**
- **Core S3 PSRAM is QSPI, not OPI.** The `m5cores3` env intentionally omits
  `board_build.arduino.memory_type` and uses the board default; do **not** add `qio_opi` or set OPI
  (gives `octal_psram: PSRAM ID read error` and 0KB PSRAM).
- **Matrix board: do not call `setCpuFrequencyMhz()`** — 80MHz and 160MHz both break USB CDC serial.
- **`-factory.bin` flash size must match the hardware** (16MB Core S3 / LCD, 4MB Matrix); a mismatch
  in the bootloader header bricks the device into a fast boot loop. `name_firmware.py` derives this
  from the env's board config.
- `StackType_t` is `uint8_t` on ESP-IDF — a static task stack array's size is bytes, not words.

## Version History

| Version | Boards | Notes |
|---|---|---|
| `3.0.0-dev` | all | vizBot 3.0 line in progress — adds StackChan flagship (`stackchan` env). |
| `2.2.1` | m5cores3 | Correct `flash_size` in merged `-factory.bin`. |
| `2.2.0` | m5cores3 | SAM2695 MIDI synth, 37 built-in sequences. |

## Licenses

vizBot firmware is **MIT** licensed (see [`LICENSE`](../LICENSE) at the repo root).

**StackChan build target (`stackchan` env) — GPL-3.0.** The vizBot 3.0 StackChan target vendors
driver code (SCS0009 servo, GC0308 camera, head-angle control) lifted from M5Stack's StackChan
factory firmware (<https://github.com/m5stack/StackChan>), which is GPL-3.0. Consequently the
**StackChan binary as a whole is distributed under GPL-3.0**, and its corresponding source is
available in this public repository. Original copyright headers are preserved in the vendored files,
crediting both mongonta0716 (original author) and M5Stack (port).

This GPL obligation applies **only** to the StackChan binary. All other board targets
(`lcd-169`, `lcd-13`, `m5cores3` bare, `matrix`) contain no GPL code — StackChan sources are gated
behind `BOARD_HAS_STACKCHAN_BASE` — and remain MIT.

Third-party libraries: FastLED (MIT), LovyanGFX (FreeBSD), M5Unified (MIT), ArduinoJson (MIT),
SensorLib (MIT), M5-SAM2695 (MIT).
