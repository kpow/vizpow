# vizPow

A motion-reactive display controller platform for wearable/portable/alternate displays. Supports three firmware targets:

- **vizBot** (ESP32-S3) — Animated bot companion with dual-core FreeRTOS architecture, captive portal WiFi provisioning, and 20 facial expressions
- **vizPow** (ESP32-S3) — Full-featured LED controller with IMU, LCD display, touch control, and 4 display modes
- **vizPow 8266** (ESP8266) — Lightweight WiFi-only port with 2 display modes (ambient + emoji)

All platforms drive an 8x8 WS2812B LED matrix (64 LEDs) with a web interface for control from any phone or browser. The ESP32-S3 LCD targets also render to a 240x280 ST7789 display.

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
| **mDNS** | `vizbot.local` | None | None |
| **Persistent Credentials** | NVS flash (verified flag) | Flash (SSID/pass) | None |
| **Server** | WebServer on Core 0 FreeRTOS task | Synchronous WebServer | ESP8266WebServer |
| **Concurrent Clients** | Non-blocking (Core 0 dedicated) | Blocking (~100ms/request) | Blocking |

### NTP and Weather (vizBot)

When connected to a home network via WiFi provisioning, vizBot enables internet features:
- **NTP Time Sync** — automatic clock sync with background retry and reconnect
- **Weather Overlay** — live temperature and icon via Open-Meteo API (configurable lat/lon)

## Features

### vizBot (ESP32-S3-Touch-LCD-1.69)
- **20 Facial Expressions**: Neutral, Happy, Sad, Surprised, Sleepy, Angry, Love, Dizzy, Thinking, Excited, Mischievous, Dead, Skeptical, Worried, Confused, Proud, Shy, Annoyed, Bliss, Focused
- **4 Personalities**: Chill, Hyper, Grumpy, Sleepy — each with distinct idle behavior and speech patterns
- **Ambient Overlay**: Hi-res animated effects (plasma, fire, ocean, aurora, etc.) render behind the bot face
- **Speech Bubbles**: 30+ contextual phrases, reactions, greetings
- **Activity States**: Active -> Idle -> Sleepy -> Sleeping (interaction wakes)
- **Overlays**: Time (NTP-synced), weather (Open-Meteo API), notifications
- **Captive Portal**: Auto-opens control page on any device
- **WiFi Provisioning**: Scan and connect to home networks from the web UI
- **Visual Boot Sequence**: LCD shows each subsystem initializing with pass/fail indicators
- **Persistent Settings**: Brightness, effects, palettes, background style saved to NVS flash
- **Dual-Core Architecture**: WiFi on Core 0, rendering on Core 1 — no frame drops or connection timeouts
- **Touch Menu**: Long-press for settings (effects, palettes, brightness, speed, hi-res toggle)
- **Shake Reactions**: IMU-driven dizzy expression and random utterances

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
[6/9] WiFi AP .......... OK  (vizBot 192.168.4.1)
[7/9] DNS .............. OK  (captive portal)
[8/9] mDNS ............. OK  (vizbot.local)
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

vizBot targets the ESP32-S3-Touch-LCD-1.69 exclusively. No board selection needed.

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
- **Arduino_GFX_Library** — LCD display rendering

Built-in (no install needed):

- **DNSServer** — captive portal DNS
- **ESPmDNS** — `.local` hostname resolution
- **Preferences** — NVS persistent storage
- **WebServer** — HTTP server

#### Upload

1. Connect ESP32-S3 board via USB-C
2. Select Board: `ESP32S3 Dev Module`
3. Enable: `USB CDC On Boot -> Enabled`
4. Select Port
5. Upload `vizbot/vizbot.ino`

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
   - vizBot: `vizBot` / password: `12345678` (captive portal auto-opens the control page)
   - vizPow ESP32: `VizPow` / password: `12345678`
   - vizPow 8266: `VizPow-8266` / password: `12345678`
3. Open browser: `http://192.168.4.1` (or `http://vizbot.local` for vizBot)

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

WiFi credentials are stored separately in the `vizwifi` NVS namespace with a `verified` flag — only auto-connect if previously successful.

## Project Structure

```
vizpow/
├── vizbot/                      # ESP32-S3 bot companion (dual-core architecture)
│   ├── vizbot.ino               # Main sketch — setup(), dual-core task launch
│   ├── config.h                 # Hardware pins, WiFi/mDNS config, board selection
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
│   ├── touch_control.h          # Touch menu gestures and UI (shared I2C mutex)
│   ├── effects_ambient.h        # 13 ambient effects (bot background overlay)
│   ├── palettes.h               # 15 color palette definitions
│   ├── display_lcd.h            # LCD rendering + GFX initialization
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
