# vizBot party mode: ferrofluid blob motion spec

> **Superseded in part (Kevin, 2026-10-25):** there's no mouth in party mode, and corner is not a setting: he picks a random corner at the start and runs to a new one each time he's poked. Where this spec differs, `docs/plan-party-mode.md` wins.

Audience: whoever implements party mode in `vizbot/`. Scope: the corner character (blob body, rim, sheen, shadow, eyes inside) and how it moves. The full-screen ambient effect underneath is unchanged.

Sources checked:
- `vizbot/bot_eye_model.h`: `BotSpring` (semi-implicit Euler, 8 ms sub-steps, dt clamped to 50 ms), `botSpringPeak()` for normalised kicks, `render(cx, cy, k, fc, stroke, now, drawBrows)` takes **int16** centre, `padX/padY` pursuit input, public `gxOut/gyOut`, `popS` kick.
- `vizbot/bot_mode.h:784-922`: frame order, the album-art corner face (`ART_FACE_R 48`, `ART_FACE_SCALE 0.36`, top right), `renderBotFaceAt`, ambient bg = style 4.
- `vizbot/effects_ambient.h:296`: hi-res effects are 8x8 px blocks. `fillCtxAudio` gets audio only on `TARGET_CORES3`.
- `vizbot/audio_spectrum.h` (CoreS3 only): `beatEnv` (1.0 on beat, decays ×0.92 per tick), `bass`, `mid`, `rms`. **The 1.69 has no beat source**, so it gets a synthetic pulse (§2.6).
- `vizbot/display_lcd.h:459`: the canvas is an `LGFX_Sprite`, and `getBuffer()` gives the raw buffer. Pixels are stored **byte-swapped** RGB565 (`web_server.h:2321`). The canvas can fall back to 8-bit (RGB332).
- `docs/eye-art-direction.md`: flat, bold, springs not frame counts, swaps under a blink, no rotation, gradients or alpha.
- void (`PLAYER99-void/void`, `app/static/index.html`, MIT, fetched from GitHub; no local clone found). Ideas taken from it:
  - `jelly` squash spring at 2.2 Hz, ζ 0.45, drawn as `sx = 1+sq, sy = 1-1.05·sq`.
  - A wobble spring driven by *velocities* (`-tilt.v·1.6 - gx.v·1.2`).
  - Breathing at `sin(1.3t)·0.004`.
  - A shake that decays `exp(-5t)`, with sideways offset `sin(36t)` (5.7 Hz).
  - Mood kicks (`jelly.kick(.15…1.1)`).
  - Pupils that move more than the body (`lx·1.25`).
  - void's body itself "stays put: no bobbing or hopping". We add hops, because ours is a liquid.

---

## 0. Size and placement

| Symbol | Value | Notes |
|---|---|---|
| `U` | `min(LCD_W, LCD_H) / 240` | = 1.0 on all three targets (1.69 240x280, CoreS3/Stackchan 320x240) |
| `R0` | `40·U` px | blob base radius in party mode (blob ≈ 80 px across, a third of the short side) |
| `kP` | `0.28` | eye scale in party mode: eye R = 13.4 px, eye pair half-width = `2.04·48·k` = 27.4 px |
| `R(k)` | `(91.7·k + 14.3)·U` | blob radius as a function of eye scale: 40 at kP, 106 at k = 1. Used by the enter/exit morph. |
| margin `m` | `0.40·R0` = 16 px | from the screen edges to the blob's rest circle. It covers wander (9 px), wobble peak (4 px) and the rim. |
| Home, top-right (default) | `(W − m − R0, m + R0)` | 1.69: (184, 56). CoreS3: (264, 56). |
| Home, top-left | `(m + R0, m + R0)` | |
| Home, bottom corners | `y = H − m − R0 − 0.25·R0` | extra 10 px so the shadow fits below the blob |
| Rest squash `J0` | `+0.05` | at rest the blob is 5 % wider than tall: a drop sitting on a surface |
| Eye base offset | `(0, −0.10·R0)` | eyes sit high in the drop and the mass sits low. It reads cuter and leaves room for brows. |
| Home attractor angle `αH` | `atan2(Hc.y − C.y, Hc.x − C.x)`, where Hc is the screen centre | Spikes point **into the show** by default, never at the screen edge. Top-right on the 1.69: ≈ 127° (down-left). |

Eyes are drawn with `botEyes.render(ex, ey, kP, botFaceColor, /*stroke*/false, now, /*brows*/true)`. The blob body is already black, so no stroke is needed. Brows stay on: they are drawn in the face colour, so they read on black. **No mouth** in party mode: at R 13 the lids carry the expression (scorecard criterion 1 already requires that). Call the eye model directly, not `renderBotFaceAt`.

---

## 1. Shape technique: polar radius function, filled as horizontal spans

**Chosen:** a star-shaped polygon of N = 120 vertices around the blob centre. Its radius is `r(θ,t)`, and it is filled with an even-odd scanline fill (`drawFastHLine`, which writes contiguous memory in the sprite).

**Rejected: 2D metaballs on a small grid.** That needs about 8000 field evaluations per frame for an 80 px blob, plus thresholding. It gives soft blobby merges but no sharp ferrofluid spikes, and no direct control over where a spike points. The polar form gives:
- exact control of every lobe and spike,
- squash and stretch as one cos 2θ term,
- area kept roughly constant (all the deforming terms are zero-mean harmonics),
- a cost under 0.5 ms.

### 1.1 The formula

Sample angles are **aligned to the attractor**: `θi = α + i·3°`, with i = 0…119. Every spike tip then lands exactly on a vertex, so tips never flicker in height as α moves (pitfall 6).

```
r(θ) = R · B(t) · [ 1 + J·cos2θ + S·cos2(θ − φv) ]          body, breath, squash, stretch
     + R · A(t) · Σ_{n=3,4,5} a_n · m_n(t) · sin(nθ + 2π·f_n·t + φ_n)   edge wobble
     + R · g · max(0, cos(θ − αg))³                          bulge toward gaze or attractor
     + Σ_{k=0..8} h_k · spk(θ − θ_k)                         ferrofluid spike fan
     + hP · Σ_{j=0..11} spk(θ − α − j·30°)                   porcupine ring (shake)
     + D · spkW(θ − θD)                                      finger dent (poke), D ≤ 0

B(t)   = 1 + 0.04·sin(2π·0.21·t)                             breath, ±1.6 px
A(t)   = 1 + 1.5·E                                           agitation multiplies wobble
m_n(t) = 0.6 + 0.4·sin(2π·μ_n·t + ψ_n)                       slow amplitude drift per harmonic
spk(d) = (1 − |d|/w)^2.2  for |d| < w, else 0                w = 9° = 3 samples: sharp tip, concave flanks
spkW(d)= cos²(d·π/(2·0.6))  for |d| < 0.6 rad               wide, soft dent
θ_k    = α + (k − 4)·18°                                     6 samples apart, tips on vertices
```

| Term | Values |
|---|---|
| a_3, a_4, a_5 | 0.045, 0.030, 0.018 × R (1.8 / 1.2 / 0.7 px; the peak sum ≈ 3.7 px) |
| f_3, f_4, f_5 | +0.13, −0.19, +0.29 Hz. Incommensurate, and n=4 runs the other way, so the pattern never visibly repeats. |
| μ_3, μ_4, μ_5 | 0.050, 0.071, 0.037 Hz |
| φ_n, ψ_n | random at party-mode entry |
| J | squash spring (cos 2θ term). + = wider and shorter. Rest target J0 = 0.05. |
| S | velocity stretch along the direction of travel φv, 0…0.10 |
| g | bulge, 0…0.10 |
| h_k | spike heights, 0…0.42·R (17 px), with a floor of −0.05·R (a 2 px dimple after snap-back) |
| hP | porcupine height, 0…0.22·R |
| D | dent depth, 0…−0.18·R |

**Planted bottom:** draw the body at `Cy_draw = Cy − lift + J·R`. Squash then keeps the bottom on the "floor" instead of shrinking about the centre.

### 1.2 Building the polygon (per frame, static buffers)

```cpp
#define BLOB_N 120
static float blobVX[BLOB_N], blobVY[BLOB_N];       // vertices, screen space
static float blobCos3[BLOB_N], blobSin3[BLOB_N];   // cos/sin(i·3°), built once
// ca = cos α, sa = sin α: rotate the LUT once instead of 120 cosf calls
for i: c = ca*blobCos3[i] - sa*blobSin3[i]; s = sa*blobCos3[i] + ca*blobSin3[i];
       // cos2θ = 2c²−1; sin(nθ+φ) via sinf (360 calls ≈ 0.15 ms), or a 256-entry LUT
       r = ...;  blobVX[i] = Cx + r*c;  blobVY[i] = CyDraw + r*s;
```

- Spike terms add `h_k·spkLUT[|i − idx_k|]` for |i − idx_k| < 3, where `spkLUT = {1, 0.41, 0.09}` (that is, `(1 − j/3)^2.2`).
- Porcupine slots are at i = 0, 10, 20…

### 1.3 Filling

For each row y from ⌊minY⌋ to ⌈maxY⌉, at yc = y + 0.5:
1. Collect the x crossings of the 120 edges (at most 12; spikes make concave rows).
2. Insertion-sort them.
3. Fill the pairs from `ceil(xa − 0.5)` to `floor(xb − 0.5)`.

The same routine (with a per-span callback) draws the rim, the body and the sheen. It costs about 110 rows × 120 edge tests ≈ 0.2 ms. A row with an odd crossing count drops its last crossing.

### 1.4 Flat look: rim, sheen, highlight (max two extra colours)

Draw order, all into the canvas: **effect → shadow → rim → body → sheen crescent → specular dot → eyes.**

1. **Rim (separation on any background).** Fill the polygon at `r + 1.5 px` with a per-pixel op, then the body at `r` in pure black on top.
   - The op is per channel: `out = max(bg × 0.5, rimFloor)`, where `rimFloor = botFaceColor × 0.22`.
   - On a bright plasma, `bg × 0.5` wins. That gives a 1–2 px half-tone step between bright and black, which reads as a soft edge.
   - On black space (galaxy, starfield), `rimFloor` wins. That gives a faint face-coloured outline, so the black body never vanishes.
   - It costs one read-modify-write per pixel of the outer polygon (≈ 5000 px, ≈ 0.3 ms). Optimise later by skipping the inner span.
2. **Sheen crescent (the ferrofluid gloss).** A band just inside the upper-left rim, θ from 200° to 250° (screen y down, so 225° is up-left). Its outer edge is at `r − 2`, inner edge at `r − 2 − t(u)`, with `t(u) = 0.08·R·sin(π·u)` (3.2 px thick in the middle, tapering to 0).
   - Build it as a 2×12-vertex polygon from the body's own r(θ), so it rides every wobble and squash.
   - Colour: the **reflected show**. Sample one canvas pixel at the screen centre each frame. Low-pass it in RGB888 with τ 0.3 s, then scale it to 45 % brightness. Clamp the result to at least `botFaceColor × 0.18`.
   - The blob then glints the plasma's colours: a liquid mirror, still one flat colour per frame.
3. **Specular dot (optional, P2).** A 3×2 px light grey (`0xBDF7`) mark at the crescent midpoint. Hide it while `|S| > 0.05` or `E > 0.6`, so it doesn't flicker in fast motion.

Not used: radial gradients, per-pixel lighting, normals.

---

## 2. Motion layers

All motion uses `BotSpring` stepped with real dt (≤ 50 ms clamp, 8 ms sub-steps). Kicks are normalised the way the eye model does it: `spring.v += peak / botSpringPeak(f, ζ)`. A "peak 0.1" kick therefore peaks at 0.1 whatever the tuning. Compute the peaks once at init.

Layering rule: **at rest only three things move: breath, slow wobble, and the eyes.** Spikes and hops are events, and must stay rare enough to feel like magic.

### 2.1 Idle breathing
- `B(t)`: ±0.04·R at 0.21 Hz (a 4.8 s cycle).
- Eyes ride it: eye y offset `−0.3·(B − 1)·R`. They rise about 0.5 px as the body swells, which reads as the drop inhaling.

### 2.2 Edge wobble
- Three travelling harmonics (§1.1). Rim phase speed stays ≤ 11 px/s (0.55 px/frame at 20 fps).
- Agitation `E` (0…1) scales the wobble by up to 2.5×. Its target is:
  ```
  E* = clamp(0.25·beatEnvSmoothed + |v|/(3R per s) + shakeT + 0.6·tapT, 0, 1)
  ```
  E is a spring at 1.2 Hz, ζ 1.0.
- Wobble is therefore calm when still and sloshy after any motion. This is void's "wobble driven by velocity" idea, applied to the whole rim.

### 2.3 Wander drift in the corner
- Every **3.5–8 s**, pick a new offset uniformly inside an ellipse of ±0.22·R0 × ±0.16·R0 (±9 × ±6 px) around home.
- Position springs X, Y: **0.55 Hz, ζ 0.75**. That gives a slow glide with about 3 % overshoot.
- If the new offset is more than 0.15·R0 from the current one, 50 % of the time it travels by a **scoot hop**: a lift kick of peak 0.06·R0.
- Wander is paused while the enter/exit flight runs, and during a tap reaction (1.2 s).

### 2.4 Squash and stretch
- **Jelly J** (cos 2θ): spring **2.2 Hz, ζ 0.45** (void's tuning), target J0 = 0.05.
- **Hop lift L** (px, + = up): spring **1.9 Hz, ζ 0.42**, target 0, driven only by kicks.
  - Its undershoot (about 23 %) is the landing. While L < 0, add `−L/R · 0.9` to J's target, so the drop flattens into the floor and wobbles back.
  - One spring therefore gives take-off, air time and a splat.
- **Velocity stretch S**: target `0.10·clamp(|v|/(2.5·R per s), 0, 1)`, where v is the total centre velocity including the lift velocity. φv = atan2(vy, vx). Spring **4 Hz, ζ 0.8**, so S never jumps.
  - Freeze φv when |v| < 0.2·R/s, so the stretch axis doesn't spin at rest.
- **Take-off anticipation** (taps, enter, exit): a J kick of −0.10 (tall) at the same instant as the lift kick.

### 2.5 Ferrofluid spikes (the signature)

Rosensweig spikes rise in a fan toward an **attractor** and snap back by surface tension.

- **Attractor angle α**: an angle spring **1.2 Hz, ζ 0.9**. Step it on the shortest wrapped delta. α is also the sample-grid origin.
  - Allowed directions are the inward half-plane: within ±80° of αH. Spikes never point at the screen edge they would clip on.
- **Field F** (0…1): spring **3 Hz, ζ 0.8** toward the event's strength. It is held for the event's hold time, then targets 0.
- **Spike k target**:
  ```
  h_k* = F · 0.42·R · exp(−((k − 4)/2.2)²) · jitter_k
  ```
  - `jitter_k` is drawn from 0.85–1.15 once per event, so the fan isn't a perfect comb.
  - **Stagger:** spike k's target is delayed by `|k − 4| · 35 ms`. The centre rises first and the fan opens outward.
- **Spike springs**:
  - Rising (target > x): **2.6 Hz, ζ 0.55**. A quick pop with a small overshoot.
  - Falling: **4.5 Hz, ζ 0.38**. A snap-back that overshoots into a dimple, clamped at h ≥ −0.05·R.
  - Switch f and ζ per step by comparing the target with x.
- **Base swell**: while F > 0, the bulge target is `g = max(gGaze, 0.10·F)` aimed at α. The spikes grow out of a mound, not a flat rim.

**Attractor events:**

| Event | Direction | F | Hold | Extras |
|---|---|---|---|---|
| Beat (CoreS3 / Stackchan, `audioSpectrum.alive`) | αH | 0.3 + 0.5·bass | 120 ms | Detect a rising edge (`beatEnv > 0.9` with previous < 0.5). Feed F from a smoothed `beatEnv` (spring 3 Hz, ζ 0.7), so fast tempos *pump* instead of strobing. If bass > 0.6: lift kick 0.06·R0, at most once per 450 ms. |
| Synthetic pulse (no mic: 1.69) | αH ± 40° random | 0.5 | 0.5–0.9 s | every 5–12 s while in party mode |
| Tap on screen (outside the blob) | toward the touch point, clamped to the inward half-plane | 1.0 | 500 ms | lift kick 0.22·R0, J kick −0.10, E += 0.6 |
| Gaze reach | eye gaze direction | 0.25 | 400 ms | 40 % chance on a saccade larger than 0.2 R. He "reaches" for what he looks at. |
| Landing after enter | αH | 0.7 | 300 ms | blink |

**Porcupine (IMU shake):** hP target 0.22·R for 450 ms. Spring rises at 5 Hz, ζ 0.6, and snaps back at 4.5 Hz, ζ 0.38. It uses 12 slots every 30°, but the 3 slots pointing within ±40° of the nearest screen edge get 0.4×.

### 2.6 Event reactions

| Event | Body | Eyes |
|---|---|---|
| **Tap** (screen, outside blob) | Hop (lift peak 0.22·R0 ≈ 9 px), spike fan at the tap point, E += 0.6. Wander paused 1.2 s. | Existing `onTap()` expression change and pop. `padX/padY` look toward the tap for 600 ms. |
| **Poke** (touch inside the blob) | Dent D kick to −0.18·R at the touch angle. Spring **3.2 Hz, ζ 0.32**, which gives a wobbly rebound. Plus a J kick of +0.16 and E = 1. No spikes (a finger pushes in, the field pulls out). | Forced blink, then SURPRISED or HAPPY through the existing path |
| **Shake** (IMU, `onShake`) | Porcupine. Plus a sideways jitter `x += 0.12·R·shakeAmp·sin(2π·5.7·t)` with `shakeAmp` set to 1 and decaying `exp(−5t)` (void). E = 1. | Existing DIZZY |
| **Strong beat** | Small hop, spike pump | `popS` kick peak 0.03 if bass > 0.7, at most once per 600 ms. No blink on beat. |
| **Expression change** | J kick of `0.08·pop`, where pop is the face's existing per-expression value | The eye model's own mood pop |

**Idle fidgets** (party mode only), one every **12–25 s**, never the same one twice in a row:
1. **Probe:** one spike (slot 4) at a random inward α rises slowly (field spring 1.2 Hz) to 0.25·R. It holds 0.8 s and snaps back. The eyes look along it.
2. **Shiver:** add harmonic n=7 at 1.1 Hz, amplitude 0.02·R, under a 600 ms sin² envelope. This is the only time n > 5 is allowed.
3. **Scoot:** a wander retarget with a forced scoot hop and an eye glance in the travel direction.
4. **Puddle peek (P2):** J → 0.35 for 1.6 s, so the body becomes a puddle. The eyes slide up 0.25·R and are drawn **with** `stroke = true` (a 1 px black stroke at k 0.28), so their tops can poke above the puddle and still read. This is the one case where eyes may leave the body.

### 2.7 Entering and exiting party mode

One progress spring **P** (0 → 1): **1.4 Hz, ζ 0.82**, with about 1.5 % overshoot, which is the "landing". The k and position values below are functions of P. Nothing is timed by frames.

**Enter:**
1. **t = 0: blink swap.** Start a blink (`BW_SWAP` path). On the shut frame, switch to the party render: the mouth disappears and the ambient effect starts. The blob's start shape depends on the background:
   - **From the ambient background (with stroke):** start the blob at `R = 78.5`, `J = 0.31`. That is a wide ellipse that matches the old 5 px stroke around both eyes (half-width 103, half-height 54 at k = 1). The swap is near-invisible.
   - **From a black background:** start the blob at `R = hypot(W, H)/2 + 10`, so it covers the whole screen. The screen is still black with eyes, so the swap is invisible. As the blob shrinks, the show is revealed like an **iris opening**.
2. **0–~900 ms: fly.** Target P = 1.
   - Eye scale: `k = lerp(1, kP, smooth(min(1, 1.3·P)))`. The shrink leads the travel by about 25 %.
   - Radius: `R = lerp(R_start, R(k), smooth(min(1, 2·P)))`.
   - Centre: a quadratic Bézier from the face centre to home, with control point `mid + (0, −0.25·|home − face|)`. It is a hop-arc into the corner. Clamp P to ≤ 1.03 for the curve.
   - E is held at 0.8 during the flight, so it sloshes. The velocity stretch S applies automatically.
3. **Landing** (first time P > 0.97): J kick +0.14, spike fan F 0.7 toward αH for 300 ms, a blink. Wander starts 1 s later.

**Exit** (reverse, about 1.1 s):
1. **Anticipation, 200 ms.** The eyes look toward the screen centre (`padX/padY`). A spike fan of F 0.6 rises at αH, so he "reaches" for the middle. J kick +0.10 (crouch).
2. **Launch.** J kick −0.12, lift kick 0.10·R0, P target 0 (same spring). The scale and position curves run in reverse.
3. **End state.** The blob deflates to the stroke ellipse (R 78.5, J 0.31), or grows to cover the screen if the bot returns to a black background (an iris closing).
4. **Swap back.** At P < 0.03, blink. On the shut frame, swap back to the normal face render and the previous background style.

---

## 3. Eyes inside the blob

- **Position:** `E = C + O`, where O is a **2D spring at 2.4 Hz, ζ 0.5** (secondary motion). It chases this target in screen space, so the eyes lag the body on hops and scoots, overshoot, and settle about 0.3 s later:
  ```
  O* = (0, −0.10·R)                          base: eyes sit high
     + (0.20·R·gxOut/0.35, 0.15·R·gyOut/0.25)  look with the whole face, void's "pupils move more than the ball"
     + (0, 0.6·J·R)                         ride the squash: the top comes down, so the eyes drop
     + (0, −0.3·(B − 1)·R)                  breath
  ```
- **Clamp:** the eye-pair box (half-width `2.04·48·k`, half-height `0.92·48·k + 0.3·48·k` for the brows) must stay inside the body's **inner** radius minus 3 px. Clamp O elliptically, so the eyes press against the wall and squish the spring instead of crossing it.
- **Body follows eyes:** the gaze bulge `gGaze = 0.07·min(1, hypot(gxOut/0.35, gyOut/0.25))`, aimed at `αg = atan2(gyOut, gxOut)`. Bulge spring **1.5 Hz, ζ 0.9**.
  - The drop leans toward where he looks, and that bulge also widens the clamp in that direction. "Edge-riding" is therefore the body flowing around the eyes, not the eyes escaping.
- **Gaze source:** in party mode, set `botEyes.idle = true` and let the eye model run its normal saccades, blinks and fidgets. Override with `padX/padY` only during attractor events (the eyes look at the spike target) and during the exit anticipation.
- **Blinks:** the eye model's own (2.6–6.8 s, 18 % doubles). Forced blinks on: landing, a poke, both swap points, and the start of the porcupine.
- **Small-size legibility:** at R 13.4, pupils are ≥ 2 px (dot 0.08 R ≈ 1 px). Use `small` (0.18) as the minimum for idle "change of look" in party mode by remapping `PUP_DOT → PUP_SMALL`. Brows stay on (≈ 2 px thick). Overlays (X, spiral, glitch) still work at this size: the lab's chip scale is ×0.25, smaller than this.
- **Integer centre:** `render()` takes int16. Round the body centre **once** with 0.6 px hysteresis (only move the drawn integer when the float is more than 0.6 px away). Use that same integer for the body polygon origin, the shadow and the eyes. The eye offset O is rounded with its own hysteresis.

---

## 4. Shadow

A soft **floor shadow** directly under the drop. It grounds the character in the corner and shows the hop height.

- **Floor line:** `yF = Cy_rest + (1 − J0)·R`, where `Cy_rest` is the centre *without* lift. It follows the wander (x and y) but **not** the lift.
- **Ellipse:** centre `(Cx + 0.08·R, yF + 0.06·R)`. Light comes from the upper left, matching the sheen. Half-axes: `a = 0.92·R·(1 − 0.45·hN)`, `b = 0.20·R·(1 − 0.45·hN)`, where `hN = clamp(L / (0.25·R), 0, 1)`. It shrinks as the drop leaves the ground.
- **Tones** (two tones darken the effect; no flat colour):
  - Outer ellipse: 75 % brightness, `p − ((p >> 2) & 0x39E7)`.
  - Core ellipse (0.70·a, 0.60·b): 50 %, `(p >> 1) & 0x7BEF`.
  - The core edge gets a 1 px checkerboard seam (alternate pixels 50/75 %), with the dither **anchored to the screen** (`(x + y) & 1`), so it doesn't crawl as the shadow moves.
  - When hN > 0.6, the core also uses 75 %: a lifted drop gives a fainter shadow.
- **Implementation:**
  - One horizontal span per row (an ellipse is convex), on the raw sprite buffer: `uint16_t *row = buf + y*W`.
  - The sprite is **byte-swapped**: `v = __builtin_bswap16(px); v = op(v); px = __builtin_bswap16(v)`.
  - Each pixel is touched once (core and outer are computed per row as nested spans, not two passes).
  - About 1000 px, ≈ 0.05 ms.
  - **8-bit canvas fallback:** skip the darkening and draw the ellipse flat with `0x0000` at a 50 % checkerboard.
- The drop covers the top half of the ellipse, so what shows is a crescent of shade below it. On a black background it is simply invisible, which is correct.

---

## 5. Timing table

### Springs (all `BotSpring`)

| Spring | f (Hz) | ζ | Target / kick |
|---|---|---|---|
| Wander X, Y | 0.55 | 0.75 | home + random offset |
| Jelly J (squash) | 2.2 | 0.45 | J0 0.05 (+ landing splat) |
| Hop lift L | 1.9 | 0.42 | 0; kicks: tap 0.22·R0, scoot 0.06·R0, beat 0.06·R0, exit 0.10·R0 |
| Velocity stretch S | 4.0 | 0.8 | 0.10·clamp(\|v\|/2.5R) |
| Attractor angle α | 1.2 | 0.9 | event direction (wrapped) |
| Field F | 3.0 (probe: 1.2) | 0.8 | event strength, then 0 |
| Spike h_k rising | 2.6 | 0.55 | `F·0.42R·gauss·jitter`, 35 ms stagger per step from centre |
| Spike h_k falling | 4.5 | 0.38 | 0, floor −0.05·R |
| Porcupine hP | 5.0 up / 4.5 down | 0.6 / 0.38 | 0.22·R for 450 ms |
| Dent D | 3.2 | 0.32 | kick −0.18·R |
| Bulge g | 1.5 | 0.9 | max(gaze 0.07, 0.10·F) |
| Agitation E | 1.2 | 1.0 | E* (§2.2) |
| Beat envelope smooth | 3.0 | 0.7 | `audioSpectrum.beatEnv` |
| Eye offset O (x, y) | 2.4 | 0.5 | O* (§3) |
| Enter/exit P | 1.4 | 0.82 | 1 / 0 |
| Eye model (unchanged) | saccade 6 / pursuit 2 / pupil 3 / shape 0.75/T | 1 / 0.9 / 0.6 / 0.85 | existing |

### Oscillators

| Oscillator | Hz | Amplitude |
|---|---|---|
| Breath B | 0.21 | ±0.04·R (1.6 px) |
| Wobble n=3 | +0.13 | 0.045·R × m₃ × A |
| Wobble n=4 | −0.19 | 0.030·R × m₄ × A |
| Wobble n=5 | +0.29 | 0.018·R × m₅ × A |
| Wobble amplitude drift m₃, m₄, m₅ | 0.050, 0.071, 0.037 | 0.6 ± 0.4 |
| Shiver n=7 (fidget only) | 1.1 | 0.02·R, 600 ms sin² envelope |
| Shake jitter x | 5.7 | 0.12·R · e^(−5t) |
| Sheen colour low-pass | τ 0.3 s | — |

### Random intervals and one-shots

| What | Range |
|---|---|
| Wander retarget | 3.5–8 s (scoot hop on 50 % of moves larger than 0.15·R0) |
| Party fidget | 12–25 s, no repeats |
| Synthetic pulse (no mic) | every 5–12 s, hold 0.5–0.9 s, ±40° around αH |
| Spike jitter per event | 0.85–1.15 |
| Gaze-reach spikes | 40 % of saccades larger than 0.2 R |
| Beat hop / eye pop rate limit | ≥ 450 ms / ≥ 600 ms |
| Tap hold / poke / shake | 500 ms / dent spring / 450 ms porcupine, jitter e^(−5t) |
| Blinks | eye model: 2.6–6.8 s, 18 % double |
| Enter / exit | ≈ 0.9 s / ≈ 1.1 s (spring-driven, not timed) |

### Cost estimate (S3 at 240 MHz; measure with `botFaceRenderStats`)

| Step | Estimate |
|---|---|
| 120 vertices (one rotation, 360 `sinf`) | ~0.15 ms |
| Shadow (≈ 1000 px read-modify-write) | ~0.05 ms |
| Rim op (≈ 5000 px read-modify-write) + body fill (`drawFastHLine`) | ~0.45 ms |
| Sheen crescent + dot | ~0.05 ms |
| Eyes at k 0.28 | ~0.25 ms |
| **Total** | **≈ 1 ms** (budget 3 ms) |

No heap: about 2 KB of static arrays (vertices, LUTs, about 20 springs).

---

## 6. Pitfalls (low fps, no antialiasing)

1. **Sub-pixel wobble boils.** An amplitude under about 1 px just toggles edge pixels on and off at random. Every persistent term must be ≥ 1.5 px at R0 (it is, except a_5 = 0.7 px, which only adds to its neighbours) or off. Don't add "a touch" of anything.
2. **High harmonics look like noise.** At R 40 px and 20 fps, n ≥ 6 becomes per-vertex stair-stepping and crawling. Cap persistent wobble at n = 5. n = 7 is only for the brief shiver.
3. **Fast features alias in time.** Anything moving along the rim faster than about 1 px/frame strobes. Keep the rim phase speed ≤ 15 px/s (≤ 0.75 px/frame) and the harmonic frequencies ≤ 0.3 Hz.
4. **Uniform wobble looks like a screensaver.** Constant amplitude and evenly spaced frequencies read as a lava lamp, not a creature. Use incommensurate frequencies, counter-rotation, the slow amplitude drift m_n, and agitation tied to real motion. Stillness is part of the character, and contrast is what makes spikes feel special.
5. **Centre rounding jitter.** The eyes take int coordinates. If the body uses a float centre, the eyes shimmer ±1 px inside it. A spring settling near x.5 flips a pixel every frame. Round once with 0.6 px hysteresis and share that integer across body, shadow and eyes.
6. **Spike tips missing samples.** On a fixed angle grid, a tip between vertices gets chopped, so spike height flickers as α moves. Align the grid to α so every slot is a vertex (§1.1).
7. **Multi-span rows.** Spikes make rows with 4–12 crossings. Use an even-odd fill with sorted crossings, not "leftmost to rightmost" (that would fill between spikes).
8. **Clipping on screen edges.** A spike or porcupine slot cut off by the bezel looks like a bug. Keep attractors in the inward half-plane, reduce the porcupine slots near edges, and keep m = 0.4·R0.
9. **Beats strobing.** At 120 BPM, raw `beatEnv` would flash full spikes twice a second. Drive F through the smoothed envelope and a 120 ms hold, and keep beat F ≤ 0.8 so taps (F 1.0) still read as bigger.
10. **Black on black.** Galaxy and starfield are mostly black, and a plain black blob disappears there. The rim's `max(bg/2, face × 0.22)` fixes this without making a hard outline on bright backgrounds. The sheen also carries colour.
11. **Byte order and depth.** The sprite stores byte-swapped RGB565. Darkening the raw value without swapping corrupts the colours (green bleeds into red and blue). On the 8-bit fallback, take the flat checkerboard path.
12. **Dither crawl.** Anchor the shadow's checker to screen (x, y), never to the shadow's own coordinates.
13. **Frame hitches.** Never kick per frame or scale kicks by dt. Kick once per event, and let the springs integrate with the clamped dt. A 200 ms WiFi stall must not launch the blob.
14. **Stretch axis spinning.** atan2 of a near-zero velocity is noise. Freeze φv when |v| < 0.2·R/s, and let S's spring carry the last axis down to zero.
15. **Snap-back dimples.** The falling spike spring at ζ 0.38 overshoots about 27 % of the spike height. Without the −0.05·R floor, a 17 px spike leaves a 5 px crater.
