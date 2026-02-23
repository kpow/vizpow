# vizPow

A motion-reactive display controller platform for wearable/portable/alternate displays. Supports three firmware targets across multiple hardware boards:

- **vizBot** (ESP32-S3) — Animated bot companion with dual-core FreeRTOS architecture, captive portal WiFi provisioning, WLED integration, info mode, and 20 facial expressions
- **vizPow** (ESP32-S3) — Full-featured LED controller with IMU, LCD display, touch control, and 4 display modes
- **vizPow 8266** (ESP8266) — Lightweight WiFi-only port with 2 display modes (ambient + emoji)

Supported boards: Waveshare ESP32-S3-Touch-LCD-1.69, Waveshare ESP32-S3-Matrix, M5Stack Core S3, and ESP8266 (NodeMCU/Wemos D1 Mini).

All platforms drive an 8x8 WS2812B LED matrix (64 LEDs) with a web interface for control from any phone or browser. LCD targets render to their full screen resolution (240x280 ST7789, 320x240 IPS on M5Stack). Each vizBot device gets a unique network identity (SSID and mDNS hostname) derived from its MAC address, with optional user-settable custom names.

## Architecture Overview

```
                    +----------------------------------+
                    |           Phone / Browser         |
                    |         http://192.168.4.1        |
                    +---------------+------------------+
                                    | HTTP (port 80)
                    +---------------v------------------+
                    |        WiFi AP / Captive Portal   |
                    |   DNS wildcard -> 192.168.4.1     |
                    |   mDNS: vizbot.local              |
                    +---------------+------------------+
                                    |
        +---------------------------+---------------------------+
        |  Core 0 (Protocol)        |    Core 1 (Application)   |
        |                           |                           |
        |  WiFi Task                |    Render Task (loop)     |
        |  +- handleClient()        |    +- readIMU()           |
        |  +- DNS server            |    +- handleTouch()       |
        |  +- captive portal        |    +- drainCommandQueue() |
        |  +- WiFi provisioning     |    +- runBotMode()        |
        |                           |    +- FastLED.show()      |
        |      +-----------+        |    +- renderToLCD()       |
        |      | Command   |--------|--->                       |
        |      |  Queue    |        |    I2C Mutex protects     |
        |      +-----------+        |    IMU + Touch bus        |
        +---------------------------+---------------------------+
```

### Why Dual-Core?

The ESP32-S3 has two CPU cores. FastLED disables interrupts during `show()` (2-5ms), which conflicts with the WiFi radio timing. Running WiFi on Core 0 and rendering on Core 1 eliminates dropped connections and frame stutters.

### Shared State Protection

| Mechanism | Purpose |
|-----------|---------|
| **I2C Mutex** (FreeRTOS semaphore) | IMU and touch share the I2C bus — mutex prevents transaction corruption |
| **Command Queue** (FreeRTOS queue, depth 8) | WiFi handlers push commands; render task drains them atomically between frames |
| **Debounced NVS Writes** | Settings flush to flash 2 seconds after last change to avoid wear |

## Networking

### Captive Portal and WiFi Provisioning (vizBot)

vizBot implements a full captive portal with WiFi provisioning so the device works out of the box and can optionally connect to a home network for internet features.

**Default behavior:**
1. Device boots as WiFi AP (`vizBot` / `12345678`)
2. DNS server responds to ALL queries with `192.168.4.1`
3. Phone/laptop detects captive portal and auto-opens browser to the control page
4. mDNS registers `vizbot.local` for easy access

**Home network provisioning flow:**
```
User connects to "vizBot" AP
     |
     v
Captive portal auto-opens control page
     |
     v
User taps "Scan Networks" -> GET /wifi/scan (async, non-blocking)
     |
     v
Selects SSID + enters password -> GET /wifi/connect?ssid=X&pass=Y
     |
     v
Device switches to AP+STA mode, attempts connection (10s timeout)
     |
     +-- Success: credentials saved to NVS (verified=true)
     |            AP lingers 30s for user to switch networks, then shuts down
     |
     +-- Failure: stays AP-only, credentials NOT saved
                  failReason returned to UI
```

On subsequent boots, verified credentials auto-connect in the background. The AP always starts initially for re-provisioning access.

**Captive portal detection endpoints:**

| Endpoint | Platform |
|----------|----------|
| `/generate_204`, `/gen_204` | Android |
| `/hotspot-detect.html` | Apple |
| `/connecttest.txt` | Windows |
| `/redirect` | Firefox |
| `/check_network_status.txt` | Kindle |

### WiFi Modes by Platform

| Aspect | vizBot | vizPow (ESP32) | vizPow 8266 |
|--------|--------|----------------|-------------|
| **WiFi Mode** | AP + STA (dual provisioning) | AP-only | AP-only |
| **Captive Portal** | DNS wildcard + detection endpoints | None | None |
| **mDNS** | `vizbot-xxxx.local` (per-device, user-settable) | None | None |
| **Persistent Credentials** | NVS flash (verified flag) | Flash (SSID/pass) | None |
| **Server** | WebServer on Core 0 FreeRTOS task | Synchronous WebServer | ESP8266WebServer |
| **Concurrent Clients** | Non-blocking (Core 0 dedicated) | Blocking (~100ms/request) | Blocking |

### NTP and Weather (vizBot)

When connected to a home network via WiFi provisioning, vizBot enables internet features:
- **NTP Time Sync** — automatic clock sync with background retry and reconnect
- **Weather Overlay** — live temperature and icon via Open-Meteo API (configurable lat/lon)

## Features

### vizBot (ESP32-S3-Touch-LCD-1.69 / M5Stack Core S3)
- **20 Facial Expressions**: Neutral, Happy, Sad, Surprised, Sleepy, Angry, Love, Dizzy, Thinking, Excited, Mischievous, Dead, Skeptical, Worried, Confused, Proud, Shy, Annoyed, Bliss, Focused
- **4 Personalities**: Chill, Hyper, Grumpy, Sleepy — each with distinct idle behavior and speech patterns
- **Ambient Overlay**: Hi-res animated effects (plasma, fire, ocean, aurora, etc.) render behind the bot face
- **Speech Bubbles**: 30+ contextual phrases, reactions, greetings
- **Activity States**: Active -> Idle -> Sleepy -> Sleeping (interaction wakes)
- **Info Mode**: Shake to toggle a weather dashboard with mini eyes, current conditions, and 3-day forecast bar graph
- **Overlays**: Time (NTP-synced), weather (Open-Meteo API), notifications
- **WLED Integration**: Forward bot speech and weather data to a WLED-controlled LED matrix via DDP protocol, with palette sync
- **Per-Device Identity**: Each device gets a unique SSID and mDNS hostname from its eFuse MAC (e.g. `vizBot-A3F2` / `vizbot-a3f2.local`), with optional user-settable custom names
- **Captive Portal**: Auto-opens control page on any device
- **WiFi Provisioning**: Scan and connect to home networks from the web UI
- **Visual Boot Sequence**: LCD shows each subsystem initializing with pass/fail indicators
- **Persistent Settings**: Brightness, effects, palettes, background style saved to NVS flash
- **Dual-Core Architecture**: WiFi on Core 0, rendering on Core 1 — no frame drops or connection timeouts
- **Touch Menu**: Long-press for settings (effects, palettes, brightness, speed, hi-res toggle)
- **Shake Reactions**: IMU-driven dizzy expression and random utterances
- **Multi-Board Support**: Runs on Waveshare ESP32-S3-Touch-LCD-1.69, ESP32-S3-Matrix, and M5Stack Core S3 — select target in `config.h`
- **Resolution-Independent Effects**: Hi-res ambient effects adapt to any LCD size (240x280 or 320x240)

### vizPow (ESP32-S3)
- **4 Display Modes**: Motion-reactive, ambient, emoji, and bot companion
- **38 LED Effects**: 12 motion-reactive + 13 ambient + 13 hi-res LCD effects
- **Bot Mode**: Animated companion face with 20 expressions, 4 personalities, speech bubbles, weather, and time overlays
- **28 Emoji Sprites**: Palette-indexed compression with fade transitions
- **Shake to Change Mode**: Shake 3 times to cycle through modes
- **Hi-Res LCD Rendering**: Full 240x280 resolution effects on the touch LCD
- **Touch Menu**: Long-press for settings, effects, palettes, brightness, speed
- **15 High-Contrast Palettes**: Custom gradients designed for small LED displays
- **Motion Control**: Accelerometer and gyroscope-driven animations with tunable sensitivity
- **WiFi Configuration**: Home network credentials configurable via web API, saved to flash
- **Web Interface**: Control via WiFi AP from any phone/browser

### vizPow 8266 (ESP8266)
- **2 Display Modes**: Ambient and emoji (no IMU/motion mode)
- **13 Ambient Effects** + **41 Emoji Sprites**
- **Web Interface**: Same control UI as ESP32 (minus motion tab)
- **Lightweight**: Runs on ESP8266 with PROGMEM-optimized sprite storage

## Hardware

### ESP32-S3-Touch-LCD-1.69 (vizBot / vizPow TARGET_LCD)

- **Board**: [Waveshare ESP32-S3-Touch-LCD-1.69](https://www.waveshare.com/esp32-s3-touch-lcd-1.69.htm)
- **MCU**: ESP32-S3 (dual-core 240MHz, WiFi, BLE)
- **Displays**: 8x8 WS2812B LED Matrix + 240x280 ST7789 LCD
- **Touch**: CST816S capacitive touch
- **Sensors**: QMI8658 6-axis IMU

| Function | GPIO |
|----------|------|
| LED Data | 14 |
| I2C SDA | 11 |
| I2C SCL | 10 |
| LCD SCK | 6 |
| LCD MOSI | 7 |
| LCD CS | 5 |
| LCD DC | 4 |
| LCD RST | 8 |
| LCD BL | 15 |

### ESP32-S3-Matrix (vizPow TARGET_LED)

- **Board**: [Waveshare ESP32-S3-Matrix](https://www.waveshare.com/esp32-s3-matrix.htm)
- **MCU**: ESP32-S3FH4R2 (dual-core 240MHz, WiFi, BLE)
- **Display**: 8x8 WS2812B LED Matrix (64 LEDs)
- **Sensors**: QMI8658 6-axis IMU (accelerometer + gyroscope)
- **Power**: USB-C (battery charging circuit onboard)

| Function | GPIO |
|----------|------|
| LED Data | 14 |
| I2C SDA | 11 |
| I2C SCL | 12 |
| IMU INT | 10 |

### M5Stack Core S3 (vizBot TARGET_CORES3)

- **Board**: [M5Stack Core S3](https://docs.m5stack.com/en/core/CoreS3)
- **MCU**: ESP32-S3 (dual-core 240MHz, WiFi, BLE, 16MB flash)
- **Display**: 320x240 IPS LCD (ILI9342C, landscape)
- **Touch**: FT6336 capacitive touch
- **Sensors**: BMI270 6-axis IMU
- **Notes**: LCD reset/backlight via AW9523 I2C expander, all managed by M5Unified library. A `DisplayProxy` wrapper provides Arduino_GFX-compatible API so all existing rendering code works unchanged.

### ESP8266 (NodeMCU/Wemos D1 Mini)

- **MCU**: ESP8266 (160MHz, WiFi)
- **Display**: 8x8 WS2812B LED Matrix (64 LEDs)
- **No IMU, LCD, or touch**

| Function | GPIO |
|----------|------|
| LED Data | 2 |

## Boot Sequence (vizBot)

vizBot runs a visual boot sequence on the LCD, showing each subsystem initializing with pass/fail indicators:

```
[1/9] LCD .............. OK
[2/9] LEDs ............. OK  (64 LEDs on GPIO14)
[3/9] I2C Bus .......... OK  (SDA:11 SCL:10)
[4/9] IMU .............. OK  (QMI8658 @ 0x6B)
[5/9] Touch ............ OK  (CST816 @ 0x15)
[6/9] WiFi AP .......... OK  (vizBot-A3F2 192.168.4.1)
[7/9] DNS .............. OK  (captive portal)
[8/9] mDNS ............. OK  (vizbot-a3f2.local)
[9/9] Web Server ....... OK  (port 80)

All systems ready. Starting vizBot...
```

On failure, the subsystem shows RED and the firmware continues in degraded mode (skipping that hardware). A `SystemStatus` struct tracks what is alive so the rest of the firmware can check before accessing any subsystem.

## Board Configuration

### vizPow (ESP32-S3)

The vizPow ESP32-S3 firmware supports two board targets via `config.h`:

```cpp
// Uncomment ONE of these:
// #define TARGET_LED    // Waveshare ESP32-S3-Matrix (LED only)
#define TARGET_LCD       // ESP32-S3-Touch-LCD-1.69 (LCD + Touch)
```

- **TARGET_LED**: LED matrix only, no LCD or touch. Battery-optimized with power-save mode.
- **TARGET_LCD**: Enables LCD display, touch menu, and hi-res effects.

### vizBot (ESP32-S3)

vizBot supports three board targets via `config.h`:

```cpp
// Uncomment ONE of these:
// #define TARGET_LED    // Waveshare ESP32-S3-Matrix (LED only)
#define TARGET_LCD       // ESP32-S3-Touch-LCD-1.69 (LCD + Touch)
// #define TARGET_CORES3 // M5Stack Core S3 (320x240 IPS, touch, BMI270)
```

- **TARGET_LCD**: Primary target. Waveshare ESP32-S3-Touch-LCD-1.69 with 240x280 ST7789 LCD.
- **TARGET_LED**: LED matrix only, no LCD or touch.
- **TARGET_CORES3**: M5Stack Core S3 with 320x240 IPS LCD. Uses M5Unified library with a DisplayProxy wrapper for API compatibility. 16MB flash — no custom partition table needed.

A `layout.h` abstraction derives all UI positions from `LCD_WIDTH` and `LCD_HEIGHT` at compile time, so effects, overlays, and UI elements automatically scale to any screen size.

## Installation

### vizBot (ESP32-S3)

#### Prerequisites

1. [Arduino IDE](https://www.arduino.cc/en/software) (2.0+ recommended)
2. ESP32 Board Support:
   - Arduino IDE -> Settings -> Additional Board Manager URLs
   - Add: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools -> Board -> Boards Manager -> Install "esp32 by Espressif Systems"

#### Libraries Required

Install via Arduino Library Manager:

- **FastLED** — LED control
- **SensorLib** by Lewis He — QMI8658 IMU driver
- **Arduino_GFX_Library** — LCD display rendering (TARGET_LCD)
- **M5Unified** — Hardware abstraction for M5Stack Core S3 (TARGET_CORES3 only)

Built-in (no install needed):

- **DNSServer** — captive portal DNS
- **ESPmDNS** — `.local` hostname resolution
- **Preferences** — NVS persistent storage
- **WebServer** — HTTP server

#### Upload

1. Connect ESP32-S3 board via USB-C
2. Select Board: `ESP32S3 Dev Module`
3. Enable: `USB CDC On Boot -> Enabled`
4. For 4MB flash boards: `Tools > Partition Scheme > Custom` and select `vizbot/partitions.csv`
5. Select Port
6. Upload `vizbot/vizbot.ino`

### vizPow (ESP32-S3)

#### Prerequisites

Same as vizBot above.

#### Libraries Required

- **FastLED** — LED control
- **SensorLib** by Lewis He — QMI8658 IMU driver

#### Upload

1. Connect ESP32-S3 board via USB-C
2. Select Board: `ESP32S3 Dev Module`
3. Enable: `USB CDC On Boot -> Enabled`
4. Select Port
5. Upload `vizpow/vizpow.ino`

### vizPow 8266 (ESP8266)

#### Prerequisites

1. [Arduino IDE](https://www.arduino.cc/en/software) (2.0+ recommended)
2. ESP8266 Board Support:
   - Arduino IDE -> Settings -> Additional Board Manager URLs
   - Add: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
   - Tools -> Board -> Boards Manager -> Install "esp8266 by ESP8266 Community"

#### Libraries Required

- **FastLED** — LED control

#### Upload

1. Connect ESP8266 board via USB
2. Select Board: `NodeMCU 1.0 (ESP-12E Module)` (or your board variant)
3. Select Port
4. Upload `vizpow_8266/vizpow_8266.ino`

## Usage

### Connecting

1. Power on the device — a sparkle intro animation plays on startup (vizBot shows a visual boot sequence on the LCD)
2. On your phone, connect to WiFi:
   - vizBot: `vizBot-XXXX` (unique per device, e.g. `vizBot-A3F2`) / password: `12345678` (captive portal auto-opens the control page)
   - vizPow ESP32: `VizPow` / password: `12345678`
   - vizPow 8266: `VizPow-8266` / password: `12345678`
3. Open browser: `http://192.168.4.1` (or `http://vizbot-xxxx.local` for vizBot)

### Shake to Change Mode (vizPow ESP32 only)

Shake the device 3 times within 1.5 seconds to cycle through modes:

**Motion** -> **Ambient** -> **Emoji** -> **Bot** -> **Motion** ...

- A brief white flash confirms the mode change
- 2-second cooldown prevents accidental re-triggers
- When entering emoji mode with no queue, 8 random sprites are auto-loaded
- Bot mode greets you on entry and responds to shakes and taps

### Touch Menu (LCD targets only)

Long-press the touch screen to open the settings menu:

- **Page 1**: Effect prev/next, palette cycling, mode switch, auto-cycle toggle
- **Page 2**: Brightness up/down, speed up/down, hi-res mode toggle
- Menu auto-hides after 8 seconds of inactivity

### Web Interface

- **Mode tabs**: Switch between modes
- **Effect buttons**: Select current effect
- **Palette buttons**: Choose color scheme
- **Emoji picker**: Add sprites to the emoji queue
- **Brightness slider**: 1-50
- **Speed slider**: 5-100ms delay
- **Auto Cycle toggle**: Automatically rotate effects and palettes
- **WiFi Setup** (vizBot): Scan and connect to home networks

## Effects

### Motion Effects (vizPow ESP32 only — accelerometer/gyroscope)

| Effect | Description |
|--------|-------------|
| Tilt Ball | Ball follows device tilt |
| Motion Plasma | Speed increases with motion |
| Shake Sparkle | Sparks appear on shake |
| Tilt Wave | Wave direction follows tilt |
| Spin Trails | Rotation controlled by gyro |
| Gravity Pixels | Particles fall toward gravity |
| Motion Noise | Perlin noise shifts with tilt |
| Tilt Ripple | Ripple center follows tilt |
| Gyro Swirl | Swirl speed from rotation |
| Shake Explode | Hard shake triggers explosion |
| Tilt Fire | Fire hotspot follows tilt |
| Motion Rainbow | Speed/direction from motion |

### Ambient Effects (all platforms)

| Effect | Description |
|--------|-------------|
| Plasma | Classic color blend |
| Rainbow | Diagonal rainbow wave |
| Fire | Rising flames |
| Ocean | Perlin noise water |
| Sparkle | Random twinkles |
| Matrix | Falling rain drops |
| Lava | Flowing lava lamp |
| Aurora | Northern lights |
| Confetti | Colorful bursts |
| Comet | Traveling dot |
| Galaxy | Spinning galaxy |
| Heart | Pulsing heart animation |
| Donut | Rotating donut shape |

### Hi-Res LCD Effects (LCD targets)

When hi-res mode is enabled, ambient effects render at full 240x280 LCD resolution instead of the 8x8 LED grid. All 13 ambient effects have hi-res variants.

### Emoji Mode (vizPow only)

Display pixel art sprites from a built-in library of 41 icons:

Heart, Star, Smiley, Check, X, Question, Exclaim, Sun, Moon, Cloud, Rain, Lightning, Fire, Snow, Tree, Coin, Key, Gem, Potion, Sword, Shield, ArrowUp, ArrowDown, ArrowLeft, ArrowRight, Skull, Ghost, Alien, Pacman, PacGhost, ShyGuy, Music, WiFi, Rainbow, Mushroom, Skelly, Chicken, Invader, Dragon, TwinkleHeart, Popsicle

- **Queue system**: Add up to 16 sprites to a cycling queue
- **Fade transitions**: Smooth crossfade between emojis (configurable timing)
- **Auto-cycle**: Automatically cycle through the queue
- **Random fill**: Shake into emoji mode to get 8 random unique sprites (ESP32)

### Bot Mode

An animated desktop companion character rendered on the 240x280 LCD. The bot has a full face with eyes, eyebrows, pupils, and mouth — all procedurally drawn and smoothly animated.

#### Expressions (20 total)

Neutral, Happy, Sad, Surprised, Sleepy, Angry, Love (heart eyes), Dizzy (spiral eyes), Thinking, Excited (star eyes), Mischievous, Dead (X eyes), Skeptical, Worried, Confused, Proud, Shy, Annoyed, Bliss, Focused

#### Personalities (4 presets)

| Personality | Behavior |
|-------------|----------|
| **Chill** | Lively and expressive, balanced idle behavior (default) |
| **Hyper** | Constant expression changes and chatter, never sits still |
| **Grumpy** | Annoyed and sarcastic, still chatty |
| **Sleepy** | Drowsy, falls asleep quickly |

#### Activity States

**Active** -> **Idle** -> **Sleepy** -> **Sleeping**

- Any interaction (touch, shake, motion) wakes the bot
- Shake triggers dizzy reaction, tap triggers random expressions
- Bot talks via speech bubbles (30+ idle phrases, reactions, greetings)
- Eyes track IMU tilt and look around autonomously

#### Background Styles

| Style | Description |
|-------|-------------|
| 0 | Solid black (default) |
| 1 | Subtle gradient |
| 2 | Breathing glow |
| 3 | Twinkling starfield |
| 4 | **Ambient overlay** — hi-res ambient effects animate behind the bot face |

The ambient overlay (style 4) renders the full hi-res ambient effects as the background, with the bot face drawn on top. Effects and palettes auto-cycle via shuffle bags for non-repeating variety.

#### Overlays

- **Time**: NTP-synced clock display (top-right corner, cyan on dark gray)
- **Weather**: Live temperature and weather icon via Open-Meteo API (top-left corner)
- **Speech Bubbles**: Contextual phrases displayed above the face
- **Notifications**: Status banners for mode/personality changes

### Info Mode (vizBot)

A secondary display mode triggered by a sustained shake (~500ms). The bot face shrinks to animated mini eyes in the top-right corner while weather information fills the screen.

**Weather view includes:**
- Current temperature (large, color-coded by temperature)
- Weather condition text and 44px icon
- 3-day forecast bar graph with color-coded high/low temps, scaled bars, and day labels
- Page dots for future additional views

**Transitions:**
- **Enter**: Bot shows thinking expression + "Checking weather..." → face shrinks to corner over 600ms with easing → mini eyes take over with autonomous blinking and look-around
- **Exit**: Shake again to expand back to full bot face

### WLED Integration (vizBot)

vizBot can forward speech bubble text and weather data to a WLED-controlled LED matrix over WiFi, keeping a secondary display in sync with the bot's activity.

**How it works:**
- Uses **DDP (Distributed Display Protocol)** for real-time pixel control — a 10-byte header + pixel data sent over UDP
- Bot speech text is rendered to a 32x8 pixel buffer and streamed to WLED as frames
- Short text displays statically, long text scrolls in a single pass
- WLED auto-enters realtime mode on DDP frames and resumes its normal effect after a 2.5s timeout

**Palette sync:** vizBot polls the WLED device's current palette via HTTP and maps it to a local palette index, keeping the bot's LCD background visually consistent with the WLED display.

**Weather on WLED:** During info mode, weather cards (current conditions, 3-day forecast) cycle on the WLED display with fade transitions between cards.

**Configuration:** WLED IP address, enabled state, text color, and scroll speed are all configurable via the web UI and persisted to NVS. Includes 30-second retry backoff after failures.

### Per-Device Network Identity (vizBot)

When running multiple vizbots, each device needs a unique network identity. vizBot solves this automatically:

**MAC-based fallback (default):**
- On first boot, a 4-hex suffix is derived from the device's eFuse MAC address
- SSID: `vizBot-A3F2`, mDNS: `vizbot-a3f2.local`
- Saved to NVS and reused on subsequent boots

**User-settable custom name:**
- Set a friendly name via the web UI (e.g. "desk", "shelf")
- SSID becomes `vizbot-desk`, mDNS becomes `vizbot-desk.local`
- Clear the custom name to revert to MAC-based identity
- Takes effect on next reboot

## API Endpoints

### Core Controls (all platforms)

| Endpoint | Description |
|----------|-------------|
| `/` | Web interface |
| `/state` | Current state (JSON) |
| `/mode?v=0\|1\|2\|3` | Set mode (motion/ambient/emoji/bot) |
| `/effect?v=N` | Set effect index |
| `/palette?v=N` | Set palette index |
| `/brightness?v=N` | Set brightness (1-50) |
| `/speed?v=N` | Set speed delay (5-100ms) |
| `/autocycle?v=0\|1` | Toggle auto-cycle |

### Emoji Controls (vizPow)

| Endpoint | Description |
|----------|-------------|
| `/emoji/add?v=N` | Add sprite N to emoji queue |
| `/emoji/clear` | Clear emoji queue |
| `/emoji/settings?cycle=MS&fade=MS&auto=0\|1` | Configure emoji playback |

### Bot Controls (vizBot / vizPow TARGET_LCD)

| Endpoint | Description |
|----------|-------------|
| `/bot/expression?v=N` | Set bot expression (0-19) |
| `/bot/say?v=text` | Show speech bubble with custom text |
| `/bot/personality?v=N` | Set personality (0=Chill, 1=Hyper, 2=Grumpy, 3=Sleepy) |
| `/bot/background?v=N` | Set face color (0-4) |
| `/bot/background?style=N` | Set background style (0-4, 4=ambient overlay) |
| `/bot/time?v=1\|0` | Enable/disable time overlay |
| `/bot/weather?v=1\|0` | Enable/disable weather overlay |
| `/bot/weather/config?lat=X&lon=Y` | Set weather location |
| `/bot/state` | Full bot state (JSON) |

### WLED Controls (vizBot)

| Endpoint | Description |
|----------|-------------|
| `/wled/config` | Get WLED configuration (JSON) |
| `/wled/config?ip=X&enabled=0\|1&r=R&g=G&b=B&speed=N` | Set WLED IP, enable/disable, text color, scroll speed |
| `/wled/test` | Test WLED connectivity |

### Device Identity (vizBot)

| Endpoint | Description |
|----------|-------------|
| `/device/name?name=X` | Set custom device name (takes effect on reboot) |
| `/device/name?name=` | Clear custom name (reverts to MAC-based identity) |

### WiFi Provisioning (vizBot)

| Endpoint | Description |
|----------|-------------|
| `/wifi/scan` | Start async WiFi scan / get scan results (JSON) |
| `/wifi/connect?ssid=X&pass=Y` | Attempt STA connection to home network |
| `/wifi/status` | Current WiFi status (AP/STA state, IPs) |
| `/wifi/reset` | Clear saved credentials, fall back to AP-only |
| `/wifi/config` | Get WiFi STA status (JSON) |
| `/wifi/config?ssid=X&pass=Y` | Set home network credentials (saved to flash) |

### Persistent Storage (vizBot)

Settings are automatically saved to NVS flash and restored on boot:

| Key | Type | Description |
|-----|------|-------------|
| `brightness` | uint8_t | LED/LCD brightness |
| `lcdBr` | uint8_t | LCD backlight PWM |
| `effect` | uint8_t | Current ambient effect |
| `palette` | uint8_t | Current color palette |
| `autoCyc` | bool | Auto-cycle toggle |
| `bgStyle` | uint8_t | Background style (0-4) |
| `hiRes` | bool | Hi-res mode toggle |
| `devName` | String | Custom device name (empty = MAC fallback) |
| `wledIP` | String | WLED device IP address |
| `wledOn` | bool | WLED integration enabled |

WiFi credentials are stored separately in the `vizwifi` NVS namespace with a `verified` flag — only auto-connect if previously successful.

## Project Structure

```
vizpow/
├── vizbot/                      # ESP32-S3 bot companion (dual-core architecture)
│   ├── vizbot.ino               # Main sketch — setup(), dual-core task launch
│   ├── config.h                 # Hardware pins, WiFi/mDNS config, board selection (3 targets)
│   ├── layout.h                 # Resolution-independent UI layout (derived from LCD_WIDTH/HEIGHT)
│   ├── device_id.h              # Per-device unique SSID/mDNS from eFuse MAC or custom name
│   ├── system_status.h          # SystemStatus struct — tracks subsystem health
│   ├── boot_sequence.h          # Visual boot diagnostics on LCD (9 stages)
│   ├── task_manager.h           # FreeRTOS tasks, I2C mutex, command queue
│   ├── wifi_provisioning.h      # STA connection, NVS credentials, provisioning state machine
│   ├── settings.h               # NVS persistence layer (debounced writes)
│   ├── web_server.h             # Web UI HTML + API handlers + captive portal endpoints
│   ├── bot_mode.h               # Bot state machine, personality system, update/render
│   ├── bot_faces.h              # 20 expression definitions + interpolation
│   ├── bot_eyes.h               # Eye/pupil/brow/mouth rendering, look-around, blink
│   ├── bot_sayings.h            # Categorized speech bubble phrase pools
│   ├── bot_overlays.h           # Speech bubbles, time, weather, notification overlays
│   ├── info_mode.h              # Info mode — weather dashboard with mini eyes
│   ├── weather_data.h           # Open-Meteo API client, geocoding, forecast parsing
│   ├── weather_icons.h          # Weather condition icons (44px sprites)
│   ├── wled_display.h           # WLED integration — DDP pixel control, state management
│   ├── wled_font.h              # 5x7 pixel font for WLED text rendering
│   ├── wled_weather_view.h      # Weather card cycling on WLED display
│   ├── touch_control.h          # Touch menu gestures and UI (shared I2C mutex)
│   ├── effects_ambient.h        # 13 ambient effects (resolution-independent hi-res variants)
│   ├── palettes.h               # 15 color palette definitions
│   ├── display_lcd.h            # LCD rendering + GFX initialization
│   ├── partitions.csv           # Custom partition table (+2MB app space on 4MB boards)
│   └── SensorQMI8658.hpp        # IMU driver
├── vizpow/                      # ESP32-S3 version (full-featured, single-threaded)
│   ├── vizpow.ino               # Main sketch — setup(), loop(), shake detection
│   ├── config.h                 # Hardware pins, constants, board selection
│   ├── palettes.h               # 15 color palette definitions
│   ├── effects_motion.h         # 12 motion-reactive effects
│   ├── effects_ambient.h        # 13 ambient effects + 13 hi-res LCD variants
│   ├── effects_emoji.h          # Emoji queue, display, transitions, random fill
│   ├── emoji_sprites.h          # 28 pixel art sprites (palette-indexed compression)
│   ├── display_lcd.h            # LCD rendering (8x8 simulation + hi-res mode)
│   ├── bot_mode.h               # Bot mode state machine, update/render pipeline
│   ├── bot_faces.h              # 20 expression definitions + interpolation
│   ├── bot_eyes.h               # Eye/pupil/brow/mouth rendering, look-around, blink
│   ├── bot_sayings.h            # Categorized speech bubble phrase pools
│   ├── bot_overlays.h           # Speech bubbles, time, weather, notification overlays
│   ├── touch_control.h          # Touch menu gestures and UI
│   ├── web_server.h             # Web UI HTML + API handlers
│   └── SensorQMI8658.hpp        # IMU driver
├── vizpow_8266/                 # ESP8266 port (WiFi + LEDs only)
│   ├── vizpow_8266.ino          # Main sketch — 2 modes, no IMU/LCD/touch
│   ├── config.h                 # ESP8266 pin config (GPIO2)
│   ├── palettes.h               # Same 15 palettes
│   ├── effects_ambient.h        # 13 ambient effects
│   ├── effects_emoji.h          # Emoji queue and display
│   ├── emoji_sprites.h          # 41 sprites (PROGMEM optimized)
│   └── web_server.h             # Web UI (2-mode variant)
├── pix-art-converter/           # Pixel art sprite creation tool
│   └── pix-art.html             # Browser-based 8x8 sprite editor
├── scripts/                     # Helper scripts
│   └── add-icon.js              # Add new icons to sprite library
├── README.md
├── LICENSE
└── .gitignore
```

## Design Patterns

### Shuffle-Bag Randomization

Effects and palettes cycle using Fisher-Yates shuffle bags instead of `random()`. This guarantees every effect/palette gets equal airtime before any repeats.

### Gesture Detection with Hysteresis

Shake detection tracks rising edges (transition from not-shaking to shaking) rather than absolute acceleration values. This prevents a single sustained shake from counting as multiple events.

### Degraded Mode Operation

The boot sequence populates a `SystemStatus` struct. If a subsystem fails (IMU, touch, WiFi), the firmware skips that hardware rather than crashing. All code paths check `sysStatus.*Ready` flags before accessing hardware.

## Roadmap

- [x] Emoji display mode with sprite library
- [x] Shake-to-change-mode gesture control
- [x] Hi-res LCD rendering mode
- [x] Touch menu control
- [x] ESP8266 lightweight port
- [x] Bot companion mode with 20 expressions and 4 personalities
- [x] Ambient overlay — bot face over animated backgrounds
- [x] NTP time sync with web-configurable WiFi
- [x] Live weather overlay via Open-Meteo API
- [x] Dual-core FreeRTOS architecture (WiFi on Core 0, render on Core 1)
- [x] Captive portal with DNS wildcard and platform-specific detection
- [x] WiFi provisioning with NVS credential storage
- [x] Visual boot sequence with LCD diagnostics
- [x] Persistent settings via NVS flash
- [x] I2C mutex and command queue for thread safety
- [x] mDNS (`vizbot.local`) hostname support
- [x] WLED integration — DDP pixel control, speech forwarding, palette sync
- [x] Info mode — weather dashboard with mini eyes and 3-day forecast
- [x] Per-device network identity — unique SSID/mDNS from eFuse MAC
- [x] User-settable device names via web UI
- [x] M5Stack Core S3 support with DisplayProxy wrapper
- [x] Resolution-independent hi-res effects (scales to any LCD size)
- [x] Custom partition table — +2MB app space on 4MB flash boards
- [x] WLED weather view cycling with fade transitions
- [x] WLED palette sync — harmonize bot LCD background with LED matrix
- [ ] Bluetooth Low Energy control
- [ ] Custom effect creator
- [ ] Sound reactivity (external mic)
- [ ] Multiple device sync
- [ ] Enclosure design for wearable medallion
- [ ] Battery level indicator
- [ ] OTA firmware updates

## Contributing

Contributions welcome! Please open an issue or pull request.

## License

MIT License - see [LICENSE](LICENSE) file.

## Acknowledgments

- [FastLED](https://github.com/FastLED/FastLED) — LED animation library
- [SensorLib](https://github.com/lewisxhe/SensorLib) — IMU driver
- [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) — LCD graphics library
- [Waveshare](https://www.waveshare.com/) — Hardware
