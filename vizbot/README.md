# vizBot — Animated Bot Companion Firmware

vizBot is the primary ESP32-S3 firmware target in the vizPow platform. It renders an animated desktop companion character on LCD displays with procedurally drawn faces, smooth tween-based animations, speech bubbles, weather overlays, and WLED integration — all driven by a dual-core FreeRTOS architecture.

## Hardware Targets

vizBot runs on four boards, selected in `config.h`:

| Board Define | Target | Display | Touch | Notes |
|---|---|---|---|---|
| `BOARD_ESP32S3_LCD_169` | TARGET_LCD | 240x280 ST7789V2 | CST816S | Primary dev board |
| `BOARD_ESP32S3_LCD_13` | TARGET_LCD | 240x240 ST7789VW | None | Battery powered, no touch |
| `BOARD_M5CORES3` | TARGET_CORES3 | 320x240 ILI9342C | FT6336 | Audio, proximity, camera |
| `BOARD_ESP32S3_MATRIX` | TARGET_LED | 8x8 WS2812B only | None | LED-only mode |

All LCD targets use `DisplayProxy` wrapping LovyanGFX (or M5Unified's internal LovyanGFX) for a unified `beginCanvas()`/`flushCanvas()` double-buffered rendering API.

## Architecture

```
Core 0 (Protocol CPU)                Core 1 (Application CPU)
─────────────────────────            ─────────────────────────
wifiServerTask (8KB static BSS)      Arduino loop()
├── server.handleClient()            ├── readIMU()
├── dnsServer.processNextRequest()   ├── handleTouch()
├── pollWifiConnectTask()            ├── drainCommandQueue()  ←── FreeRTOS Queue
├── pollWledDisplay()                ├── updateBotMode()
├── pollWeatherFetch()               ├── renderBotMode()
├── pollCloudSync()         (TLS)    ├── FastLED.show()
├── pollScheduledCommands()          └── tween updates
├── pollScheduledContent()
├── pollMeshBroadcast()    (ESP-NOW)
└── vTaskDelay(2ms)
```

**Why dual-core?** FastLED disables interrupts during `show()` (2-5ms), which conflicts with WiFi radio timing. Running WiFi on Core 0 and rendering on Core 1 eliminates dropped connections and frame stutters.

**Cross-core communication:**
- **Command Queue** (FreeRTOS, depth 8) — Web/cloud handlers push `Command` structs; render loop drains them atomically between frames
- **I2C Mutex** (FreeRTOS semaphore) — IMU and touch share the I2C bus
- **Volatile flags** — `wledData.sendState`, `meshScanRequested`, etc.

## File Guide

### Core

| File | Purpose |
|------|---------|
| `vizbot.ino` | Entry point — `setup()`, `loop()`, mode dispatch, command drain |
| `config.h` | Board selection, pin definitions, target-level config, WiFi/cloud constants |
| `settings.h` | NVS persistence layer with debounced writes (2s after last change) |
| `device_id.h` | Per-device unique SSID/mDNS from eFuse MAC or user-set custom name |
| `system_status.h` | `SystemStatus` struct — tracks subsystem health (IMU, touch, WiFi, etc.) |
| `boot_sequence.h` | Visual LCD boot diagnostics (9 stages with pass/fail indicators) |
| `task_manager.h` | FreeRTOS tasks, I2C mutex, command queue, `drainCommandQueue()` |
| `partitions.csv` | Custom partition table (+2MB app space on 4MB flash boards) |

### Face & Display

| File | Purpose |
|------|---------|
| `bot_faces.h` | 25 expression definitions (`BotExpression` structs) + LERP interpolation |
| `bot_eyes.h` | Eye/pupil/brow/mouth rendering, look-around, blink system, face color |
| `bot_overlays.h` | Speech bubbles, time overlay, weather overlay, notification banners |
| `layout.h` | Resolution-independent UI positions (derived from `LCD_WIDTH`/`LCD_HEIGHT`) |
| `display_lcd.h` | LovyanGFX initialization, `DisplayProxy` struct, `beginCanvas()`/`flushCanvas()` |
| `tween.h` | `TweenManager` — 16-slot animation engine with 8 easing functions |

### Bot Behavior

| File | Purpose |
|------|---------|
| `bot_mode.h` | Bot state machine (Active/Idle), personality system, update/render pipeline |
| `bot_sayings.h` | 14 saying categories, 80+ phrases (greetings, idle, reactions, time-of-day) |
| `bot_sounds.h` | Sound effect system for Core S3 (boot chime, tap boop, shake rattle, etc.) |

### Web & Network

| File | Purpose |
|------|---------|
| `web_server.h` | Neo-brutalist web UI (PROGMEM HTML/CSS/JS) + all API endpoint handlers |
| `wifi_provisioning.h` | AP+STA dual mode, captive portal, credential NVS storage, scan/connect |
| `cloud_client.h` | vizCloud HTTPS client — registration, sync, command dispatch, TLS pinning |
| `content_cache.h` | LittleFS caching for cloud content (sayings, personalities, metadata) |
| `esp_now_mesh.h` | ESP-NOW peer-to-peer mesh — state broadcast, coordinated WLED, peer tracking |

### WLED Integration

| File | Purpose |
|------|---------|
| `wled_display.h` | DDP pixel control (32x8), state capture/restore, cross-core queue, hologram mode |
| `wled_emoji.h` | Emoji sprite slideshow on WLED matrix with fade transitions |
| `wled_font.h` | 3x5 pixel font for rendering text into the 32x8 pixel buffer |
| `wled_weather_view.h` | Weather card cycling on WLED (current conditions, forecast, fade transitions) |
| `wled_scheduled_content.h` | Periodic weather/emoji content cycling on WLED display |
| `emoji_sprites.h` | Pixel art sprite data (palette-indexed compression) |

### Effects & Palettes

| File | Purpose |
|------|---------|
| `effects_ambient.h` | 11 ambient effects with hi-res LCD variants (plasma, fire, ocean, aurora, etc.) |
| `palettes.h` | 15 color palette definitions (custom gradients for small LED displays) |

### Sensors & Input

| File | Purpose |
|------|---------|
| `touch_control.h` | Touch menu gestures and UI (long-press, swipe, shared I2C mutex) |
| `audio_analysis.h` | Microphone audio analysis — spike/speech/silence detection (Core S3 only) |
| `proximity_light.h` | Proximity/ambient light sensor — peek-a-boo, cover, hand wave (Core S3 only) |

### Data & Info

| File | Purpose |
|------|---------|
| `info_mode.h` | Weather dashboard with mini eyes, 3-day forecast bar graph, page dots |
| `weather_data.h` | Open-Meteo API client, geocoding, forecast parsing, NTP time sync |
| `weather_icons.h` | Weather condition icons (44px sprites for info mode) |

## Expressions (25)

| Index | Name | Description |
|-------|------|-------------|
| 0 | Neutral | Default resting face |
| 1 | Happy | Upturned mouth, raised brows |
| 2 | Sad | Downturned mouth, lowered brows |
| 3 | Surprised | Wide eyes, open mouth |
| 4 | Chill | Relaxed half-closed eyes |
| 5 | Angry | Furrowed brows, tight mouth |
| 6 | Love | Heart-shaped eyes |
| 7 | Dizzy | Spiral eyes |
| 8 | Thinking | Raised brow, side-looking |
| 9 | Excited | Star-shaped eyes |
| 10 | Mischievous | Asymmetric grin |
| 11 | Skeptical | One raised brow |
| 12 | Worried | Curved brows, small mouth |
| 13 | Confused | Tilted expression |
| 14 | Proud | Puffed up, wide smile |
| 15 | Shy | Looking away, small eyes |
| 16 | Annoyed | Half-lidded, frowning |
| 17 | Focused | Concentrated look |
| 18 | Winking | One eye closed |
| 19 | Devious | Narrowed scheming eyes |
| 20 | Shocked | Extra-wide eyes and mouth |
| 21 | Kissing | Puckered lips |
| 22 | Nervous | Darting eyes, wobbly mouth |
| 23 | Glitching | Distorted/corrupted face |
| 24 | Sassy | Head-tilt attitude |

Expressions are defined as `BotExpression` structs in `bot_faces.h` with parameters for eye mode, pupil size, brow angle, mouth shape, and more. Transitions between expressions use LERP interpolation over configurable durations.

## Personality System

Three built-in personalities with distinct behavior profiles:

| Personality | Idle Timeout | Expression Rate | Say Rate | Say Chance | Favorites |
|-------------|-------------|----------------|----------|------------|-----------|
| **Chill** (default) | 90s | 4-10s | 16-40s | 30% | Neutral, Happy, Thinking, Mischief, Winking, Shy, Proud, Sassy |
| **Hyper** | 180s | 2-6s | 8-24s | 45% | Happy, Excited, Surprised, Love, Proud, Winking, Kissing, Sassy |
| **Grumpy** | 45s | 6-18s | 20-55s | 30% | Angry, Annoyed, Mischief, Skeptical, Devious, Nervous, Glitching, Focused |

Each personality also has favorite palettes and ambient effects for background cycling.

**Cloud personalities** (slots 3-11) are synced from vizCloud, cached in LittleFS, and loaded into the `runtimePersonalities[]` array at runtime. Up to 12 total personalities can be active.

**Personality rotation** cycles through a configurable list at a set interval (default 5 minutes). Setting a single personality via web UI or cloud command stops rotation.

## Tween Animation Engine

`tween.h` provides a lightweight animation system:

- **16 concurrent tween slots** — drives float values from start to end over a duration
- **8 easing functions**: Linear, InQuad, OutQuad, InOutQuad, OutCubic, OutBounce, OutElastic, OutBack
- **Auto-eviction**: When all slots are full, the oldest tween is snapped to its end value and replaced
- **Deduplication**: Starting a tween on an already-tweening target reuses the existing slot

Used for expression transitions, info mode enter/exit, overlay fade-in/out, and eye look-around.

## Web Control Panel

The web UI is embedded as a PROGMEM string in `web_server.h` (~10KB). It uses a **neo-brutalist** design: thick black borders (3px), hard offset shadows (zero blur), square corners, saturated accent colors, off-white card surfaces on warm cream background.

**Layout:** Two-column dashboard (60/40 desktop, 50/50 tablet, single-column mobile). All sections visible with collapsible headers. Collapse state persisted to localStorage.

**Left column:** Expressions (25 buttons, 5-column grid), Say Something (text input), Personality (dropdown + rotation), Appearance (face color, background style, ambient effects), WLED Sprites.

**Right column:** Device (brightness, volume, time overlay, hi-res toggle), Weather & Info, WiFi provisioning, WLED Display config.

## WLED Integration

vizBot controls a WLED-connected 32x8 LED matrix via **DDP (Distributed Display Protocol)**:

1. Bot speech text is rendered to a 32x8 pixel buffer using a 3x5 font
2. Multi-word phrases are split and sequenced one word at a time
3. Pixel data sent as a single UDP packet (10-byte DDP header + 768 bytes RGB = 778 bytes)
4. WLED auto-enters realtime mode; vizBot restores the previous effect via HTTP after display

**Palette sync:** vizBot polls WLED's current palette via HTTP and maps it to a local palette index, keeping the LCD background visually consistent with the LED matrix.

**Hologram mode:** Horizontal mirror for Pepper's ghost prism displays — mirrors both LCD face and WLED pixel buffer.

**Mesh coordination:** When multiple vizBots share a WLED target, ESP-NOW mesh prevents DDP collisions by deferring sends until the active peer finishes.

## vizCloud Integration

Cloud connectivity via HTTPS to a DigitalOcean App Platform server:

- **TLS pinning**: GTS Root R4 certificate (Google Trust Services), NOT `esp_crt_bundle` (crashes generic ESP32-S3)
- **Registration**: POST `/api/bots/register` with MAC, hardware type, firmware version, capabilities
- **Sync polling**: POST `/api/bots/{id}/sync` at configurable interval (default 60s)
- **Command dispatch**: Supports expression, say, personality, brightness, background, ambient_effect, sound, volume, sleep, reboot, mesh_scan
- **Scheduled commands**: ISO-8601 `execute_at` timestamps, buffered in 8 slots
- **Content sync**: Cloud-managed sayings and personalities cached to LittleFS
- **Group management**: Multi-bot groups with sync modes, shared WLED ownership
- **Fleet telemetry**: Reports expression, personality, RSSI, heap, uptime, NTP time, IMU, lux, mesh peers

Runs cooperatively inside `wifiServerTask` on Core 0 — TLS naturally serialized with WLED HTTP to prevent heap fragmentation.

See `FIRMWARE-INTEGRATION.md` for the full integration spec.

## ESP-NOW Mesh

Peer-to-peer mesh networking between vizBot devices (`esp_now_mesh.h`):

- Periodic broadcast of device state (expression, personality, WLED activity)
- Automatic peer discovery and stale peer eviction
- Coordinated WLED display — prevents DDP collisions when multiple bots share a WLED target
- Deferred speech: LCD bubble delayed if a mesh peer is currently using the same WLED

## Graphics Stack

- **LovyanGFX** for TARGET_LCD (custom LGFX class, DMA SPI at 40MHz, ST7789V2/ST7789VW)
- **M5Unified** for TARGET_CORES3 (wraps LovyanGFX internally)
- **DisplayProxy** struct provides unified API: `beginCanvas()`, `flushCanvas()`, `fillRect()`, `drawLine()`, etc.
- **Double-buffering**: All rendering goes to an offscreen LGFX_Sprite, then flushed to the display in one atomic SPI transfer — zero flicker
- **Resolution-independent layout**: `layout.h` derives all UI positions from `LCD_WIDTH` and `LCD_HEIGHT` at compile time

## Core S3 Extras

The M5Stack Core S3 target enables additional sensor-reactive behaviors:

- **Audio analysis** (`audio_analysis.h`): Built-in mic detects spikes (clap → Surprised), speech (→ Focused), and extended silence (→ Chill)
- **Proximity/light** (`proximity_light.h`): Detects hand approaching (→ Surprised/Shy), peek-a-boo (3+ cover/uncover cycles → "Peekaboo!"), sustained cover (→ Worried + "Dark")
- **Sound effects** (`bot_sounds.h`): Boot chime, tap boop, shake rattle, clap react, wake chime
- **Auto-brightness**: Ambient light sensor adjusts LCD backlight

## Building

1. Set board target in `config.h` (uncomment one `BOARD_*` define)
2. Arduino IDE: Board = `ESP32S3 Dev Module`, USB CDC On Boot = Enabled
3. For 4MB flash boards: Partition Scheme = Custom, select `partitions.csv`
4. Required libraries: FastLED, SensorLib, LovyanGFX (TARGET_LCD), M5Unified (TARGET_CORES3), ArduinoJson
5. Upload `vizbot.ino`

## API Endpoints

All endpoints served on port 80 via the captive portal AP (default `vizBot-XXXX` / `12345678`).

See the root [README.md](../README.md) for the complete API endpoint reference.
