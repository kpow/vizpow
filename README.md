# vizPow

A motion-reactive display controller platform for wearable/portable/alternate displays. Supports two platforms:

- **ESP32-S3** — Full-featured with IMU, LCD display, touch control, and 3 display modes
- **ESP8266** — Lightweight WiFi-only port with 2 display modes (ambient + emoji)

Both platforms drive an 8x8 WS2812B LED matrix (64 LEDs) with a web interface for control from any phone or browser.

## Features

### ESP32-S3 Version
- **3 Display Modes**: Motion-reactive, ambient, and emoji
- **38 LED Effects**: 12 motion-reactive + 13 ambient + 13 hi-res LCD effects
- **41 Emoji Sprites**: Built-in pixel art icons with auto-cycling and fade transitions
- **Shake to Change Mode**: Shake 3 times to cycle through modes
- **Hi-Res LCD Rendering**: Full 240x280 resolution effects on the touch LCD
- **Touch Menu**: Long-press touch menu for settings, effects, palettes, brightness, speed
- **15 High-Contrast Palettes**: Custom gradients designed for small LED displays
- **Motion Control**: Accelerometer and gyroscope-driven animations with tunable sensitivity
- **Web Interface**: Control via WiFi AP from any phone/browser

### ESP8266 Version
- **2 Display Modes**: Ambient and emoji (no IMU/motion mode)
- **13 Ambient Effects** + **41 Emoji Sprites**
- **Web Interface**: Same control UI as ESP32 (minus motion tab)
- **Lightweight**: Runs on ESP8266 with PROGMEM-optimized sprite storage

## Hardware

### ESP32-S3-Matrix (TARGET_LED)

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

### ESP32-S3-Touch-LCD-1.69 (TARGET_LCD)

- **Board**: [Waveshare ESP32-S3-Touch-LCD-1.69](https://www.waveshare.com/esp32-s3-touch-lcd-1.69.htm)
- **MCU**: ESP32-S3 (dual-core 240MHz, WiFi, BLE)
- **Displays**: 8x8 WS2812B LED Matrix + 240x280 ST7789 LCD
- **Touch**: CST816S capacitive touch
- **Sensors**: QMI8658 6-axis IMU
- **Features**: Hi-res LCD rendering, touch menu control

### ESP8266 (NodeMCU/Wemos D1 Mini)

- **MCU**: ESP8266 (160MHz, WiFi)
- **Display**: 8x8 WS2812B LED Matrix (64 LEDs)
- **No IMU, LCD, or touch**

| Function | GPIO |
|----------|------|
| LED Data | 2 |

## Board Configuration

The ESP32-S3 version supports two board targets via `config.h`:

```cpp
// Uncomment ONE of these:
// #define TARGET_LED    // Waveshare ESP32-S3-Matrix (LED only)
#define TARGET_LCD       // ESP32-S3-Touch-LCD-1.69 (LCD + Touch)
```

- **TARGET_LED**: LED matrix only, no LCD or touch
- **TARGET_LCD**: Enables LCD display (`display_lcd.h`), touch menu (`touch_control.h`), and hi-res effects (`HIRES_ENABLED`)

## Installation

### ESP32-S3

#### Prerequisites

1. [Arduino IDE](https://www.arduino.cc/en/software) (2.0+ recommended)
2. ESP32 Board Support:
   - Arduino IDE -> Settings -> Additional Board Manager URLs
   - Add: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools -> Board -> Boards Manager -> Install "esp32 by Espressif Systems"

#### Libraries Required

Install via Arduino Library Manager:

- **FastLED** - LED control
- **SensorLib** by Lewis He - QMI8658 IMU driver

#### Upload

1. Connect ESP32-S3 board via USB-C
2. Select Board: `ESP32S3 Dev Module`
3. Enable: `USB CDC On Boot -> Enabled`
4. Select Port
5. Upload `vizpow/vizpow.ino`

### ESP8266

#### Prerequisites

1. [Arduino IDE](https://www.arduino.cc/en/software) (2.0+ recommended)
2. ESP8266 Board Support:
   - Arduino IDE -> Settings -> Additional Board Manager URLs
   - Add: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
   - Tools -> Board -> Boards Manager -> Install "esp8266 by ESP8266 Community"

#### Libraries Required

- **FastLED** - LED control

#### Upload

1. Connect ESP8266 board via USB
2. Select Board: `NodeMCU 1.0 (ESP-12E Module)` (or your board variant)
3. Select Port
4. Upload `vizpow_8266/vizpow_8266.ino`

## Usage

### Connecting

1. Power on the device — a sparkle intro animation plays on startup
2. On your phone, connect to WiFi:
   - ESP32: `VizPow` / password: `12345678`
   - ESP8266: `VizPow-8266` / password: `12345678`
3. Open browser: `http://192.168.4.1`

### Shake to Change Mode (ESP32 only)

Shake the device 3 times within 1.5 seconds to cycle through modes:

**Motion** -> **Ambient** -> **Emoji** -> **Motion** ...

- A brief white flash confirms the mode change
- 2-second cooldown prevents accidental re-triggers
- When entering emoji mode with no queue, 8 random sprites are auto-loaded

### Touch Menu (TARGET_LCD only)

Long-press the touch screen to open the settings menu:

- **Page 1**: Effect prev/next, palette cycling, mode switch, auto-cycle toggle
- **Page 2**: Brightness up/down, speed up/down, hi-res mode toggle
- Menu auto-hides after 8 seconds of inactivity

### Web Interface

- **Mode tabs**: Switch between modes (Motion/Ambient/Emoji on ESP32, Ambient/Emoji on ESP8266)
- **Effect buttons**: Select current effect
- **Palette buttons**: Choose color scheme
- **Emoji picker**: Add sprites to the emoji queue
- **Brightness slider**: 1-50
- **Speed slider**: 5-100ms delay
- **Auto Cycle toggle**: Automatically rotate effects and palettes

## Effects

### Motion Effects (ESP32 only — use accelerometer/gyroscope)

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

### Ambient Effects (both platforms)

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

### Hi-Res LCD Effects (TARGET_LCD only)

When hi-res mode is enabled, ambient effects render at full 240x280 LCD resolution instead of the 8x8 LED grid. All 13 ambient effects have hi-res variants:

Plasma, Rainbow, Fire, Ocean, Sparkle, Matrix, Lava, Aurora, Confetti, Comet, Galaxy, Heart, Donut

### Emoji Mode

Display pixel art sprites from a built-in library of 41 icons:

Heart, Star, Smiley, Check, X, Question, Exclaim, Sun, Moon, Cloud, Rain, Lightning, Fire, Snow, Tree, Coin, Key, Gem, Potion, Sword, Shield, ArrowUp, ArrowDown, ArrowLeft, ArrowRight, Skull, Ghost, Alien, Pacman, PacGhost, ShyGuy, Music, WiFi, Rainbow, Mushroom, Skelly, Chicken, Invader, Dragon, TwinkleHeart, Popsicle

- **Queue system**: Add up to 16 sprites to a cycling queue
- **Fade transitions**: Smooth crossfade between emojis (configurable timing)
- **Auto-cycle**: Automatically cycle through the queue
- **Random fill**: Shake into emoji mode to get 8 random unique sprites (ESP32)

## Project Structure

```
vizpow/
├── vizpow/                      # ESP32-S3 version (full-featured)
│   ├── vizpow.ino               # Main sketch — setup(), loop(), globals, shake detection
│   ├── config.h                 # Hardware pins, constants, XY mapping, board selection
│   ├── palettes.h               # 15 color palette definitions
│   ├── effects_motion.h         # 12 motion-reactive effects
│   ├── effects_ambient.h        # 13 ambient effects + 13 hi-res LCD variants
│   ├── effects_emoji.h          # Emoji queue, display, transitions, random fill
│   ├── emoji_sprites.h          # 41 pixel art sprite definitions
│   ├── display_lcd.h            # LCD rendering (8x8 simulation + hi-res mode)
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

## API Endpoints

| Endpoint | Description |
|----------|-------------|
| `/` | Web interface |
| `/state` | Current state (JSON) |
| `/mode?v=0\|1\|2` | Set mode (ESP32: motion/ambient/emoji, ESP8266: 0-1 only) |
| `/effect?v=N` | Set effect index |
| `/palette?v=N` | Set palette index |
| `/brightness?v=N` | Set brightness (1-50) |
| `/speed?v=N` | Set speed delay (5-100ms) |
| `/autocycle?v=0\|1` | Toggle auto-cycle |
| `/emoji/add?v=N` | Add sprite N to emoji queue |
| `/emoji/clear` | Clear emoji queue |
| `/emoji/settings?cycle=MS&fade=MS&auto=0\|1` | Configure emoji playback |

## Roadmap

- [x] Emoji display mode with sprite library
- [x] Shake-to-change-mode gesture control
- [x] Hi-res LCD rendering mode
- [x] Touch menu control
- [x] ESP8266 lightweight port
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

- [FastLED](https://github.com/FastLED/FastLED) - LED animation library
- [SensorLib](https://github.com/lewisxhe/SensorLib) - IMU driver
- [Waveshare](https://www.waveshare.com/) - Hardware
