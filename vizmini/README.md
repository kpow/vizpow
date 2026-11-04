# vizMini 🐾

A pocket-sized, standalone **vizbot** for a tiny yeti body powered by an
**ESP32-C3 Super Mini** with a **1.3" monochrome I2C OLED**, an optional
**pushbutton** and an **8-LED WS2812B strip** glowing through a translucent
body. It keeps the soul of the full vizbot — the expressive procedural face —
on a one-color 128×64 screen. The v2 plan and status live in `PLAN-v2.md`.

## What it does

- **Expressive face** — vizbot's parametric eye model (the eye lab's 25 faces,
  springs, blinks, glances, fidgets), mouths from the expression tween, and a
  light falling-snow ambiance.
- **Face motion** (`face_motion.h`) — rare whole-face moves: tilt, wobble,
  pendulum, drift, spin, double spin (then dizzy), upside-down flip. Off /
  rare (default, every 2–6 min) / often, plus test buttons on the web page.
- **Ferro mode** (`ferro_blob.h`) — the screen turns white and he becomes a
  black ferrofluid drop: roaming, symmetric magnetic spike patterns on a
  turning axis, tentacles with round bulbs. The face's black morphs into the
  drop and back. Off / auto (default: a 60 s visit every 8–15 min) / always.
  Specs: `docs/vizmini-ferro-motion-spec.md`, `docs/vizmini-ferro-art-direction.md`.
- **Inside lights** (`strip_leds.h`) — mood (colour + motion per expression,
  dims with blinks), ambient show, or off; a Spark button lays a rainbow
  burst over either. Brightness and mode are saved per bot.
- **Button** (optional; GPIO3 → GND, internal pull-up, active-LOW, debounced):
  - **Press** → cycle to the next expression
  - **Long-press or double-press** → enter **Time / Weather** mode
  - **In info mode:** press → flip Time ↔ Weather · long-press → back to the face
- **Time + Weather** — NTP clock and Open-Meteo weather (free, no API key) as
  two full-screen views that **auto-flip every 30s**. Set your location and
  timezone in `config.h`.
- **WiFi + web control** — joins your network (saved in NVS) and serves a small
  control page at `http://vizmini.local`; falls back to a `vizMini-XXXX` hotspot
  with a captive setup portal, and **auto-reconnects** in the background
  (AP↔STA self-healing, no reboot needed).
- **Web OTA update** — flash new firmware over WiFi from the `/update` page (or
  the **⬆ Firmware update** link on the control page). No cable after the first
  install; it writes to the spare app slot, so an interrupted upload won't brick it.

Not included: cloud/personality sync, sound.

## Hardware wiring

| Signal | C3 Super Mini pin | Notes |
|--------|-------------------|-------|
| LED strip DIN | **GPIO4** | 8× WS2812B; power from 5V / GND, capped at `LED_MAX_MA` |
| Button (optional) | GPIO3 ↔ GND | momentary pushbutton; internal pull-up, active-LOW (no VCC) |
| Onboard NeoPixel | GPIO8 | left dark: the strip needs both C3 RMT channels |
| OLED SCL | GPIO9 | BOOT strapping pin — don't hold low at boot |
| OLED SDA | **GPIO10** | kept off GPIO8 (the onboard NeoPixel) |
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

Each build also publishes a versioned image to the repo-root `builds/` folder
(`builds/vizmini-c3-v<version>.bin`, newest 3 kept) via `name_firmware.py`.

### Updating over the air (OTA)

After the first USB flash, update without a cable — open
`http://vizmini.local/update` (or the **⬆ Firmware update** link on the control
page), pick `builds/vizmini-c3-v<version>.bin`, and flash. Or in one line:

```sh
curl -F "firmware=@builds/vizmini-c3-v<version>.bin" http://vizmini.local/update
```

## How the port works

The face is **not** reimplemented — `tween.h`, `bot_faces.h`, `bot_eyes.h`,
`bot_eye_model.h` and `bot_eye_params.h` are copied from `../vizbot` (only
`bot_faces.h` drops its `bot_sounds.h` include). The renderer draws through an
object `gfx` of type `GfxDevice`; `gfx_mono.h` maps those calls onto a 1-bit
U8g2 buffer:

- **Color → on/off.** Eye-white (non-zero RGB565) lights a pixel; background /
  pupil (`0x0000`) clears it, so black pupils "cut into" the white eyes.
- **Screen space.** Coordinates are OLED pixels. `renderMiniFace()` draws the
  face centred on (`FACE_ORIGIN_X`, `FACE_ORIGIN_Y`) at `FACE_SCALE` the way
  vizbot's `renderBotFaceAt()` shrinks it, so the eye model rasterises one
  span per real OLED column.

**No FPU:** the C3 emulates floats in software (~0.4 µs an op), so the
rotation blit and the ferro outline, spikes and fill are fixed point.

Because the renderer is untouched, the face stays in lock-step with the main
firmware and any future expression added there drops straight in. It's a
**single-core** cooperative loop (web + render interleaved) suited to the C3.

### Tuning on hardware

Adjust these in `config.h`:

- `FACE_SCALE`, `FACE_ORIGIN_X/Y` — face size and centering on the 128×64 panel
- `OLED_DRIVER_SH1106` — flip to `0` for SSD1306 if the image is shifted/garbled
- `LED_*`, `MOTION_*`, `FERRO_*` — inside lights, face motion, ferro mode
- `WEATHER_LAT/LON`, `TZ_POSIX`, `CLOCK_24H`, `INFO_AUTO_SWITCH_MS` — clock/weather

The OLED runs on hardware I2C at 400 kHz (`OLED_I2C_HZ`, ~33 ms a frame).
Software I2C (`OLED_USE_SW_I2C 1`) still works but takes ~520 ms a frame.

`GET /state` reports `faceUs`, `ferroUs`/`ferroMaxUs`, `sendUs` and `frameMs`;
`GET /debug/screen` returns the raw 1024-byte frame buffer (8 pages × 128
columns, LSB = top pixel) for checking the screen without looking at it.

## Files

| File | Role |
|------|------|
| `vizmini.ino` | setup/loop, touch gestures, info mode, ferro visits, overlays |
| `config.h` | pins, OLED driver, face placement, lights / motion / ferro / weather tunables |
| `strip_leds.h` | 8-LED strip: mood table, ambient show, spark |
| `face_motion.h` | whole-face rotation moves (fixed-point rotated blit) |
| `ferro_blob.h` | ferro mode: drop, spikes, tentacles, morph |
| `gfx_mono.h` | `GfxDevice` 1-bit adapter + the U8g2 display object |
| `touch_input.h` | pushbutton debounce/noise-reject: press + long-press |
| `weather.h` | Open-Meteo fetch/parse + 1-bit weather icons |
| `web_ui.h` | WiFi provisioning, captive portal, auto-reconnect, control page, `/state`, OTA |
| `name_firmware.py` | post-build: publish versioned image to `builds/` |
| `tween.h`, `bot_faces.h`, `bot_eyes.h`, `bot_eye_model.h`, `bot_eye_params.h` | vizbot face engine (copied) |
