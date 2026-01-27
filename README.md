# ESP32-S3 LED Matrix Controller

A motion-reactive LED matrix controller designed for wearable displays. Built for the Waveshare ESP32-S3-Matrix board with onboard 8x8 WS2812B LED matrix and QMI8658 6-axis IMU.

## Features

- **3 Display Modes**: Motion-reactive, ambient, and emoji
- **28 LED Effects**: 12 motion-reactive + 16 ambient effects
- **Emoji Mode**: 32 built-in pixel art sprites with auto-cycling and fade transitions
- **Shake to Change Mode**: Shake the device 3 times to cycle between modes
- **15 High-Contrast Palettes**: Custom gradients designed for small LED displays
- **Motion Control**: Accelerometer and gyroscope-driven animations with tunable sensitivity
- **Web Interface**: Control via WiFi AP from any phone/browser
- **Adjustable Settings**: Brightness, speed, auto-cycle mode

## Hardware

- **Board**: [Waveshare ESP32-S3-Matrix](https://www.waveshare.com/esp32-s3-matrix.htm)
- **MCU**: ESP32-S3FH4R2 (dual-core 240MHz, WiFi, BLE)
- **Display**: 8x8 WS2812B LED Matrix (64 LEDs)
- **Sensors**: QMI8658 6-axis IMU (accelerometer + gyroscope)
- **Power**: USB-C (battery charging circuit onboard)

### Pin Configuration

| Function | GPIO |
|----------|------|
| LED Data | 14 |
| I2C SDA | 11 |
| I2C SCL | 12 |
| IMU INT | 10 |

## Installation

### Prerequisites

1. [Arduino IDE](https://www.arduino.cc/en/software) (2.0+ recommended)
2. ESP32 Board Support:
   - Arduino IDE → Settings → Additional Board Manager URLs
   - Add: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools → Board → Boards Manager → Install "esp32 by Espressif Systems"

### Libraries Required

Install via Arduino Library Manager (Sketch → Include Library → Manage Libraries):

- **FastLED** - LED control
- **SensorLib** by Lewis He - QMI8658 IMU driver

### Upload

1. Connect ESP32-S3-Matrix via USB-C
2. Select Board: `ESP32S3 Dev Module`
3. Enable: `USB CDC On Boot → Enabled`
4. Select Port
5. Upload `src/led_matrix.ino`

## Usage

### Connecting

1. Power on the device — a sparkle intro animation plays on startup
2. On your phone, connect to WiFi network: `LED-Matrix`
3. Password: `12345678`
4. Open browser and go to: `http://192.168.4.1`

### Shake to Change Mode

Shake the device 3 times within 1.5 seconds to cycle through modes:

**Motion** → **Ambient** → **Emoji** → **Motion** ...

- A brief white flash confirms the mode change
- 2-second cooldown prevents accidental re-triggers
- When entering emoji mode with no queue, 8 random sprites are auto-loaded

### Web Interface

- **Mode tabs**: Switch between Motion, Ambient, and Emoji modes
- **Effect buttons**: Select current effect
- **Palette buttons**: Choose color scheme
- **Emoji picker**: Add sprites to the emoji queue (emoji mode)
- **Brightness slider**: 1-50
- **Speed slider**: 5-100ms delay
- **Auto Cycle toggle**: Automatically rotate effects and palettes

## Effects

### Motion Effects (use accelerometer/gyroscope)

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

### Ambient Effects (no motion input)

| Effect | Description |
|--------|-------------|
| Plasma | Classic color blend |
| Rainbow | Diagonal rainbow wave |
| Fire | Rising flames |
| Ocean | Perlin noise water |
| Sparkle | Random twinkles |
| Wave | Bouncing sine wave |
| Spiral | Rotating dot with trails |
| Breathe | Pulsing color fade |
| Matrix | Falling rain drops |
| Lava | Flowing lava lamp |
| Aurora | Northern lights |
| Confetti | Colorful bursts |
| Comet | Traveling dot |
| Galaxy | Spinning galaxy |
| Checkerboard | Shifting checkerboard |
| Diamond Pulse | Expanding diamond |

### Emoji Mode

Display pixel art sprites from a built-in library of 32 icons:

Heart, Star, Smiley, Check, X, Question, Exclaim, Sun, Moon, Cloud, Rain, Lightning, Fire, Snow, Tree, Coin, Key, Gem, Potion, Sword, Shield, Arrow Up/Down/Left/Right, Skull, Ghost, Alien, Pacman, Music, WiFi, Rainbow

- **Queue system**: Add up to 16 sprites to a cycling queue
- **Fade transitions**: Smooth crossfade between emojis (configurable timing)
- **Auto-cycle**: Automatically cycle through the queue
- **Random fill**: Shake into emoji mode to get 8 random unique sprites

## Project Structure

```
esp32-led-matrix/
├── led_matrix/
│   ├── led_matrix.ino       # Main sketch — setup(), loop(), globals, shake detection
│   ├── config.h             # Hardware pins, constants, XY mapping, shake settings
│   ├── palettes.h           # 15 color palette definitions
│   ├── effects_motion.h     # 12 motion-reactive effects
│   ├── effects_ambient.h    # 16 ambient effects
│   ├── effects_emoji.h      # Emoji queue, display, transitions, random fill
│   ├── emoji_sprites.h      # 32 pixel art sprite definitions
│   ├── web_server.h         # Web UI HTML + API handlers
│   └── SensorQMI8658.hpp    # IMU driver
├── README.md
├── LICENSE
└── .gitignore
```

## API Endpoints

| Endpoint | Description |
|----------|-------------|
| `/` | Web interface |
| `/state` | Current state (JSON) |
| `/mode?v=0\|1\|2` | Set mode (motion/ambient/emoji) |
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
