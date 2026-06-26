# vizMini 🐾

A pocket-sized, standalone **vizbot** for a tiny yeti body powered by an
**ESP32-C3 Super Mini** with a **1.3" monochrome I2C OLED**, a **pushbutton**,
and the board's **onboard WS2812 NeoPixel**. It keeps the soul of the
full vizbot — the expressive procedural face — on a one-color 128×64 screen.

## What it does

- **Expressive face** — all 25 vizbot expressions (happy, sad, dizzy, love,
  glitch, sassy…) with blinking, idle eye look-around, smooth tweened
  transitions, and a light falling-snow ambiance.
- **Button** (GPIO7 → GND, internal pull-up, active-LOW, debounced):
  - **Press** → cycle to the next expression
  - **Long-press or double-press** → enter **Time / Weather** mode
  - **In info mode:** press → flip Time ↔ Weather · long-press → back to the face
  - **Any press while asleep** → wake up
- **Time + Weather** — NTP clock and Open-Meteo weather (free, no API key) as
  two full-screen views that **auto-flip every 30s**. Set your location and
  timezone in `config.h`.
- **WiFi + web control** — joins your network (saved in NVS) and serves a small
  control page at `http://vizmini.local`; falls back to a `vizMini-XXXX` hotspot
  with a captive setup portal, and **auto-reconnects** in the background
  (AP↔STA self-healing, no reboot needed).
- **NeoPixel spark** — the onboard WS2812 (GPIO8) glows a slow rainbow through
  the skin: ~30s bursts with short dark gaps (on more than off), full
  brightness, ~12s smooth color drift. Fires automatically and via the web
  **Spark** button. All timing/brightness tunable via `SPARK_*` in `config.h`.
- **Auto-sleep** — drowsy closed eyes + drifting `Zzz` after 2 min idle (or the
  web Sleep button); any touch wakes it.

Not included: cloud/personality sync, sound.

## Hardware wiring

| Signal | C3 Super Mini pin | Notes |
|--------|-------------------|-------|
| Button | GPIO7 ↔ GND | momentary pushbutton; internal pull-up, active-LOW (no VCC) |
| NeoPixel | GPIO8 | onboard WS2812 (hardwired) |
| OLED SCL | GPIO9 | BOOT strapping pin — don't hold low at boot |
| OLED SDA | **GPIO10** | moved off GPIO8 so the NeoPixel can use it |
| OLED VCC / GND | 3V3 / GND | |

> A plain pushbutton replaced a TTP223 capacitive pad, which false-triggered
> from board/WiFi/NeoPixel supply & EMI noise. A mechanical switch is immune to
> it; firmware debounces in `touch_input.h`.

> **Note:** the OLED's SDA lives on **GPIO10**, not GPIO8 — GPIO8 carries the
> onboard NeoPixel. (The C3 SuperMini's u.FL external-antenna connector is
> disconnected by default and needs a board mod to use; this build runs on the
> onboard antenna.)

## Build & flash

```sh
cd vizmini
pio run -e c3-oled            # compile
pio run -e c3-oled -t upload  # flash over USB
pio device monitor            # serial log shows the IP / AP name
```

First boot has no saved Wi-Fi, so it starts the **vizMini-XXXX** hotspot. Join
it, the captive page pops up, enter your SSID/password, and it reboots onto your
network. The boot splash and serial log show the address to reach it
(`http://vizmini.local` or the printed IP).

## How the port works

The face is **not** reimplemented — `tween.h`, `bot_faces.h`, and `bot_eyes.h`
are copied **verbatim** from `../vizbot`. The renderer draws through an object
`gfx` of type `GfxDevice` using six primitives (`fillEllipse`, `fillCircle`,
`drawLine`, `fillRect`, `fillTriangle`, `drawFastVLine`). `gfx_mono.h` supplies
a `GfxDevice` that maps those onto a 1-bit U8g2 buffer:

- **Color → on/off.** Eye-white (non-zero RGB565) lights a pixel; background /
  pupil (`0x0000`) clears it, so black pupils "cut into" the white eyes.
- **Design space → screen.** The renderer works in its native 240×280 space
  centered on (120, 118). Every coordinate/radius is mapped with
  `screen = ORIGIN + (design − center) × FACE_SCALE`.

Because the renderer is untouched, the face stays in lock-step with the main
firmware and any future expression added there drops straight in. It's a
**single-core** cooperative loop (web + render interleaved) suited to the C3.

### Tuning on hardware

Adjust these in `config.h`:

- `FACE_SCALE`, `FACE_ORIGIN_X/Y` — face size and centering on the 128×64 panel
- `OLED_DRIVER_SH1106` — flip to `0` for SSD1306 if the image is shifted/garbled
- `SPARK_*` — NeoPixel burst length, gap, brightness, and color-drift speed
- `WEATHER_LAT/LON`, `TZ_POSIX`, `CLOCK_24H`, `INFO_AUTO_SWITCH_MS` — clock/weather

If nothing draws at all, confirm the I2C pins; software I2C
(`OLED_USE_SW_I2C 1`) is the default so SDA/SCL are explicit regardless of board
variant.

## Files

| File | Role |
|------|------|
| `vizmini.ino` | setup/loop, touch gestures, sleep, NeoPixel spark, info mode, overlays |
| `config.h` | pins, OLED driver, face transform, spark + weather tunables |
| `gfx_mono.h` | `GfxDevice` 1-bit adapter + the U8g2 display object |
| `touch_input.h` | pushbutton debounce/noise-reject: press + long-press |
| `weather.h` | Open-Meteo fetch/parse + 1-bit weather icons |
| `web_ui.h` | WiFi provisioning, captive portal, auto-reconnect, control page, `/state` |
| `tween.h`, `bot_faces.h`, `bot_eyes.h` | **verbatim** vizbot face engine |
