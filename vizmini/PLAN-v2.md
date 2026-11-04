# vizMini v2 — lights, new eyes, lots of rotation

Started 2026-11-03. New translucent printed bodies, 2–3 more vizMinis. The
inside lights are a primary feature; the face should move far more than it
does today.

Kevin's calls (2026-11-03):
- LEDs: 8× WS2812B 5V strip inside the body. Two modes, switchable from the
  web page: **mood** (colour + motion follow the face, default) and
  **ambient** (independent light show). Plus off.
- Rotation: all of it, "and more" — mood head tilts, full spins as
  reactions, a constant slow drift, upside-down flip gags.
- Eyes: adapt as much of the eye lab work (25-face parametric eye model,
  `vizbot/bot_eye_model.h` + `bot_eye_params.h`) as the 128×64 mono OLED allows.
- No button on these builds (GPIO3 floats high on its pull-up = never pressed).

## Wiring

| Part | Pin |
|---|---|
| LED strip DIN | GPIO4 |
| LED strip 5V / GND | 5V / GND |
| OLED SCL / SDA | GPIO9 / GPIO10 |
| OLED VCC / GND | 3V3 / GND |
| Onboard NeoPixel | GPIO8, left dark (the strip needs both C3 RMT channels) |

Strip power is capped in firmware at `LED_MAX_MA` (450 mA) so USB never browns out.

## Stages (one at a time: build → flash → Kevin checks → next)

1. **LED strip** (code done, fw 0.7.0.2; waiting on Kevin's wiring to test) — `strip_leds.h`: FastLED on GPIO4 (strip only), mood
   table (hue + motion per expression, blink dims it), ambient show, spark
   burst, `/bot/leds` + web card, NVS-saved mode/brightness.
2. **New eyes** (done, fw 0.7.0.4, all 25 captured via GET /debug/screen) — port `bot_eye_model.h` + generated params; mono render
   (lit eye, black pupil cut-outs) through the vertical-span path; brows and
   mouths re-fit to 128×64: FACE_SCALE 0.41 at (64, 28) fits every face.
3. **Rotation engine** (done, fw 0.7.0.8: `face_motion.h`; rare by default = a move every 2–6 min, off/rare/often + test buttons on the web page) — render the face into an offscreen 1-bit buffer,
   rotated blit into the U8g2 buffer at any angle and scale (shrinks to fit
   a 64 px circle when spinning). Spring-driven angle; per-mood tilt, idle
   drift, spin reactions, flip gag.
   Found on the way: software I2C made every frame take ~520 ms (2 fps);
   hardware I2C at 400 kHz is ~33 ms (~24 fps). Rotation blit is 16.16
   fixed point (the C3 has no FPU; a float loop cost ~28 ms).
4. **Lights ⇄ motion** — LED patterns follow the rotation angle (the lit
   spot travels with the spin), sweeps on blinks and face changes.
5. **Fleet** — per-unit names/hostnames for the 2–3 new bots.
6. **Ferro mode** (specs written 2026-11-03) — the
   screen goes white and he becomes a black ferrofluid blob. Motion:
   docs/vizmini-ferro-motion-spec.md. Look: docs/vizmini-ferro-art-direction.md.
   Where they differ, the art direction's §11 resolutions win (tall spikes
   ≥ 30° apart, glint at r − 4.5, filings as 2 px dashes, ≤ 3 free droplets,
   wall contact 2 px short). All per-vertex and per-pixel work is Q16.16
   fixed point with a sin table (no FPU).
   Kevin's calls (2026-11-03): **auto visits** (~60 s of ferro every 8–15 min,
   plus a web on/off), **floor stance + climbs**, **rare peek eyes** (2×3 px,
   <10 % of the time), **open window** in front of the OLED (art minimums as
   written, glint on).
   Round 2 (Kevin 2026-11-03: not locked to the floor, symmetric magnetic
   spikes, tentacles like the 1.69/Stackchan, more dynamic) DONE fw 0.7.2.3:
   floats + roams (glide/dart/hop, bounces), 24-slot spike ring on a turning
   magnet axis with 11 patterns (2 stack), reach/feeler/flick/arm tentacles
   with bulbs, union fill. Cost avg 2.5-4 ms, max ~10 ms with 3-5 tentacles
   (frames 40-50 ms). Floor/ceiling/wall stances from the spec are parked.
   Morph + auto visits DONE fw 0.7.3.1: enter = the face's black shrinks
   from a 76 px drop (iris opening, eyes shut at P 0.45, landing splash,
   hello tentacle); exit = gather, swell back, eyes reopen, white corners last;
   brightness follows P. Ferro setting off / auto (default) / always in NVS
   `ferro`; auto = 60 s visit every 8-15 min; web Visit (60s) button.
   Still open: peek eyes, LED pairing, stances, Show.
   Build phases: F1 (DONE fw 0.7.1.1: ferro_blob.h, 0.7 ms avg / 3.7 ms max
   per frame, frame 41 ms, panel 99) body on the floor + jiggle + fill + glint + web toggle
   (measure ms) → F2 spikes/tentacles/droplets + party patterns → F3 new
   wide-screen patterns + scheduler + stacking + show → F4 enter/exit morph,
   peek eyes, auto visits, LED pairing.
7. **Info screens = Ink Time** (Kevin's pick from the concept pitch, built
   2026-11-03, merged in PR #124): ink_time.h clock, weather, transitions
   and boot intro. **On hold:** LED pairing for the ink screens until Kevin
   sees vizMini in the printed translucent case.
