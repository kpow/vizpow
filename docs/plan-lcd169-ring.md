# Sprint: 1.69 interior LED ring

A small addressable LED ring inside the 1.69 bot, on **GPIO17**, lighting the inside of
the shell. It should echo what the screen is doing. A 2D effect can't map 1:1 onto a
ring, so the ring copies the screen's **colours** and **type of motion**, not its pixels.

## Hardware

- Data: GPIO17. Free on the 1.69 (not strapping, USB, PSRAM or used by the LCD/touch).
- Power: **5V/VBUS, not 3V3.** The 1.69 LDO already looks marginal under WiFi TX
  (see `lcd169-wifi-tx-power`). 3.3V data into a 5V WS2812 is fine over a short wire.
- Ring: **8 × WS2812B, powered from 5V** (Kevin, 2026-10-09). Colour order GRB assumed.
- Budget: 8 LEDs at full white ≈ 480 mA. Engine default brightness 80/255 ≈ 150 mA max.

## Design

Reuse the base LED engine (`stackchan_leds.h`) that already drives the stack-chan ring
and the Faces strips. It gets a third backend, so modes, the web card and the
`/bot/base_leds/*` endpoints come for free.

- Build flag `-DBOARD_HAS_RING_LEDS` (lcd-169 env) → `RING_LED_PIN`, count, chipset.
- Derived flag `HAS_BASE_LEDS` = any of the three bases; replaces the repeated
  `STACKCHAN || FACES` guards.
- `ring_leds.h`: FastLED on its own controller (the matrix array owns `FastLED[0]`).

### New mode: `screen` (step 2)

Follows the screen. Colours come from the frame actually drawn: the ambient effects
build colour from a rotating hue (`ctx.baseHue`), not `currentPalette`, so the ring
point-samples a 4x4 grid of the effect buffer, sorts the samples by hue into a 16-entry
palette and eases toward it. Motion comes from a per-effect archetype:

| Archetype | Ring motion | Effects |
|---|---|---|
| Flow | palette gradient drifts slowly, soft | Plasma, Perlin, Distorsion, Bumpmap, Hiphotic |
| Spin | gradient rotates fast, sharper | Galaxy, ZVortex |
| Ripple | brightness waves outward from one point | Ripple, Sinusoid |
| March | bands step around the ring | Chevrons, Stripes |
| Sweep | one bright band sweeps back and forth | Scanline |
| Blocks | segments of palette colours swap in steps | Checker, Puzzle, Xorcery |
| Dots | a few dots chase with tails | Snakes |

When the background is not ambient (solid / gradient / starfield), the ring shows the
mood colour of the current expression with a slow breath.

## Plan

1. **Bring-up** — flags, `ring_leds.h`, engine running on the 1.69, web card. Test with
   existing modes (rainbow / chase / solid colours). (~30 min incl. flash)
2. **Screen mode** — archetype table above + mood fallback; default mode on the 1.69. (~2 h)
3. **Controls + persistence** — Light page rows on the 1.69 (Ring brightness slider,
   Ring mode step), web card mode list, save mode/brightness/speed in NVS. (~1 h)
4. **Verify** — WiFi still connects at 15 dBm with the ring lit; no render-loop stutter;
   touch flash reactions. (~30 min)

## Status

- [x] Step 1 code: `BOARD_HAS_RING_LEDS` + `HAS_BASE_LEDS`, `ring_leds.h`, engine + `/bot/base_leds/{set,mode}` + "LED Ring" web card on the
      1.69, `audio` mode idles on boards without a mic. v3.7.6; lcd-169, lcd-13, m5cores3,
      stackchan, faces all build. Branch `feat/lcd169-ring`.
- [x] Step 1 count: 8 × WS2812B. Fire mode's second ignition point wrote past the end of
      an 8-LED buffer; now wraps. v3.7.7 flashed to vizbot-old-dino (10.0.0.142); boot log
      shows `[ring] 8 LEDs on GPIO17`, `/state` reports the ring, rainbow mode set.
- [x] Step 1 eye test: rainbow smooth all the way round, red is red (GRB correct).
- [x] Step 2 code: `screen` mode (index 10) in the engine — screen-sampled palette + 7
      motion archetypes (`SC_SCREEN_MOTION`), mood colour when background isn't ambient.
      Default mode on the 1.69. Mood colour now syncs on every base, not just stack-chan.
      Web mode lists include `screen`. v3.7.8 flashed; lcd-169, stackchan, faces build.
- [x] Step 2 eye test: all 7 motions look right (Kevin).
- [x] Step 3: Settings > Light on the 1.69 has "Ring" (brightness slider) and "Ring mode"
      (stepper) under Screen. Mode/brightness/speed saved in NVS (`ringMode`/`ringBri`/
      `ringSpd`) from touch and from `/bot/base_leds/mode`; `scLeds.init()` moved before
      `loadSettings()` so saved values aren't reset. Verified on device: chase/120/150
      survived a reset, stepper > and < (wraps off -> screen), slider tap sets brightness,
      via /debug/touch + /debug/screen. v3.7.9; stackchan + faces build.
- [x] Step 3 hands-on: slider works (Kevin).
- [x] Step 4 cost: added `ring.updateMaxUs` to `/state` (worst `scLeds.update()` incl. RMT
      push since last read). Screen mode at brightness 255: 441-640 us per frame, ~2% of
      the 33 ms frame budget — no stutter risk. v3.7.10.
- [x] Step 4 WiFi: ring at 255 in screen mode, 60 pings 0% loss (avg 9.8 ms), 0 rejoins,
      heap steady ~181-187 KB; 5/5 resets joined STA at 15 dBm (bootMs 7.7-8.7 s), ring
      settings restored every boot. Ring is on 5V so the LDO isn't carrying it.
- [x] All 5 envs build.
- [x] Tap flash: face tap flashes the ring in the mood colour of the reaction face it
      picks (400 ms), shake = dizzy purple (700 ms), wake = surprised cyan (600 ms) — via
      `scFlashMood()` in `BotMode`, so every base gets it. Engine flash reworked: it now
      crossfades back into the running effect instead of fading to black and popping,
      blends at output only (twinkle/fire state untouched), follows the brightness setting
      (2x effect level) and stays dark in mode off. v3.7.11; 3 injected taps, no reset,
      update still ~0.6 ms.
- [x] Tap flash eye test: feels right (Kevin).
