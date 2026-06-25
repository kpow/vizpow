# vizMini 🐾

A pocket-sized, standalone **vizbot** for a tiny yeti body powered by an
**ESP32-C3 Super Mini** with a **1.3" monochrome I2C OLED** and a **TTP223
touch pad**. It keeps the soul of the full vizbot — the expressive procedural
face — on a one-color 128×64 screen.

## What it does (v1)

- **Expressive face** — all 25 vizbot expressions (happy, sad, dizzy, love,
  glitch, sassy…) with blinking, idle eye look-around, and smooth tweened
  transitions between moods.
- **Touch interactions** (GPIO7)
  - **Tap** → *boop!* startle reaction (quick surprised flash)
  - **Double-tap** → cycle to the next expression
  - **Long-press** → go to sleep (drowsy closed eyes + drifting `Zzz`)
  - **Any touch while asleep** → wake up
- **WiFi + web control** — joins your network (credentials saved in NVS) and
  serves a small control page; falls back to its own `vizMini-XXXX` Wi-Fi
  hotspot with a captive setup portal if it can't connect.
- **Ambiance** — a light falling-snow effect (toggleable), fitting for a yeti.

Deferred to v2: cloud/personality sync, OTA, sound.

## Hardware wiring

| Signal | C3 Super Mini pin | Notes |
|--------|-------------------|-------|
| OLED SDA | GPIO8 | strapping pin — don't hold low at boot |
| OLED SCL | GPIO9 | BOOT strapping pin — don't hold low at boot |
| OLED VCC / GND | 3V3 / GND | |
| Touch OUT | GPIO7 | TTP223 digital output (active-HIGH default) |
| Touch VCC / GND | 3V3 / GND | |

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
firmware and any future expression added there drops straight in.

### Tuning the face on hardware

The exact framing needs a real panel. Adjust these in `config.h`:

- `FACE_SCALE` — overall face size (default `0.45`)
- `FACE_ORIGIN_X` / `FACE_ORIGIN_Y` — where the face centers on the 128×64 panel

If the image is shifted ~2px or scrambled, your panel is the other controller —
flip `OLED_DRIVER_SH1106` in `config.h` (SH1106 ↔ SSD1306). If nothing draws at
all, confirm the I2C pins; software I2C (`OLED_USE_SW_I2C 1`) is used by default
so the SDA/SCL pins are explicit regardless of board variant.

## Files

| File | Role |
|------|------|
| `vizmini.ino` | setup/loop, touch gestures, sleep logic, overlays, control hooks |
| `config.h` | pins, OLED driver, face transform, behavior tunables |
| `gfx_mono.h` | `GfxDevice` 1-bit adapter + the U8g2 display object |
| `touch_input.h` | TTP223 debounce + tap / double-tap / long-press |
| `web_ui.h` | WiFi provisioning, captive portal, control page, `/state` |
| `tween.h`, `bot_faces.h`, `bot_eyes.h` | **verbatim** vizbot face engine |
