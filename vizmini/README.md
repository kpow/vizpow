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
  The 12 LEDs are his body: 1 and 8 hands, 2–3 and 6–7 arms, 4 + 5 head,
  9 and 12 legs, 10 + 11 belly. Most moods light a body part (thinking: head;
  happy: right hand waves; love: belly heartbeat; angry: fists; surprised: a
  flash to every tip; sad: a drip from the head; excited: jazz hands; neutral:
  a breath from the feet up), and ambient adds walk, juggle, charge, clap and
  halo. Every LED stays at least `LED_BODY_FLOOR` lit, so the whole body
  glows. The classic whole-strip motions stay but are rare:
  `LED_CLASSIC_EVERY` (one 10 s mood turn in 6) and `LED_CLASSIC_PCT`
  (20% of ambient auto picks).
- **Button** (optional; GPIO3 → GND, internal pull-up, active-LOW, debounced):
  - **Press** → cycle to the next expression; the ambient light show jumps to a new pattern and palette
  - **Long-press or double-press** → enter **Time / Weather** mode
  - **In info mode:** press → flip Time ↔ Weather · long-press → back to the face
- **Time + Weather** — NTP clock and Open-Meteo weather (free, no API key) as
  two full-screen views that **auto-flip every 30s**. Set your location and
  timezone in `config.h`.
- **WiFi + web control** — joins your network (saved in NVS) and serves a small
  control page at `http://vizmini-xxxx.local`; falls back to a `vizMini-XXXX` hotspot
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
(`http://vizmini-xxxx.local`, the hotspot's four hex digits in lowercase, or
the printed IP).

Each vizMini has a **name** (System card on his page; 1–23 characters, empty =
the default `vizMini-XXXX` from his MAC). It is his hotspot name, his `.local`
address (`Desk Yeti` → `http://desk-yeti.local`) and his name in VizCloud.

Each build also publishes a versioned image to the repo-root `builds/` folder
(`builds/vizmini-c3-v<version>.bin`, newest 3 kept) via `name_firmware.py`.

### Updating over the air (OTA)

After the first USB flash, update without a cable — open
`http://<name>.local/update` (or the **⬆ Firmware update** link on the control
page), pick `builds/vizmini-c3-v<version>.bin`, and flash. Or in one line:

```sh
curl -F "firmware=@builds/vizmini-c3-v<version>.bin" http://vizmini-xxxx.local/update
```

The file name must contain `vizmini-c3` and the image must start with the ESP
magic byte 0xE9 and be built for the ESP32-C3; anything else is refused before
a byte is written.

## Cloud

`cloud_client.h` speaks VizCloud bot protocol v2
(`vizCloud/docs/rebuild/BOT-API-V2.md`) as hardware type `vizmini_c3_oled`.

- **Pairing**: on first boot he registers, gets his own key (stored in NVS)
  and waits as *pending* until someone approves him in the VizCloud admin.
- **Sync** every poll interval (default 60 s, never faster than 20 s): his
  name, firmware, face, lights, signal and memory go up; commands come down
  (`expression`, `say`, `brightness` = LED strip, `reboot`, `led_mode`,
  `led_pattern`, `led_palette`) and settings (`deviceName`, `brightness`,
  `pollIntervalSec`). They run through the same functions as his web page.
- **Cloud firmware updates** download over the pinned HTTPS connection,
  check size and SHA-256, and roll back unless the new firmware syncs within
  10 minutes.
- **Forgotten** (0.8.1): a sync answered `approval: "unknown"` means the admin
  forgot him. He clears his id, key and config revision (NVS `cId`/`cTok`/`cRev`,
  `tId`/`tTok`/`tRev` on a test server), registers again without an id on the
  next call and waits for approval as a new bot. At most one such reset per 5
  minutes; inside that window he keeps polling.
- **Memory**: the calls run in their own task, one network job at a time (never
  beside the weather fetch or a web upload), and wait when the heap is short.
  The System card shows free / lowest memory; the Cloud card shows the cloud
  task's spare stack.

### Testing against a server on your Mac

1. Start the VizCloud server on the Mac (it listens on, say, port 3000).
2. Find the Mac's address: `ipconfig getifaddr en0` (e.g. `192.168.1.20`).
3. On his page, open **Cloud**, enter `http://192.168.1.20:3000`, **Save**.
4. Within a minute the Cloud card says *Waiting for approval*: approve him in
   the admin. Empty the field and **Save** to go back to the built-in server.

Plain `http://` is accepted only for 10.x, 172.16–31.x, 192.168.x and `.local`
hosts; an `https://` test server must chain to the GTS Root R4. A test server
keeps its own key, so testing never disturbs his production pairing.

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
| `weather.h` | Open-Meteo fetch/parse + 1-bit weather icons; the one-network-job lock |
| `device_name.h` | his name: hotspot SSID, `.local` address, VizCloud name |
| `cloud_client.h` | VizCloud protocol v2: register, sync, commands, config, cloud OTA |
| `web_ui.h` | WiFi provisioning, captive portal, auto-reconnect, control page, `/state`, OTA |
| `name_firmware.py` | post-build: publish versioned image to `builds/` |
| `tween.h`, `bot_faces.h`, `bot_eyes.h`, `bot_eye_model.h`, `bot_eye_params.h` | vizbot face engine (copied) |
