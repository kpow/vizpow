# Party mode round 2: tentacles and jiggle (motion spec)

Status: proposal for Kevin. Nothing in the repo was changed. It builds on v3.8.0 (`origin/main`):
`vizbot/party_blob.h`, `docs/plan-party-mode.md`, `docs/party-mode-motion-spec.md` (called **R1** below), and
`docs/party-mode-layout-spec.md`. Where this spec changes an R1 number, the new number is given with the old
one in brackets.

Units: `r` = the tier's body radius (S 37 / **M 44** / L 54 px). Pixel values are for M on the 1.69 unless stated.
Angles are in screen space with y down. All motion runs on `BotSpring` (8 ms sub-steps, dt clamped to 50 ms).
Kicks are normalised with `botSpringPeak` as in R1, so "kick 0.1" means the value peaks at 0.1.

---

## 0. The calls in one table

| # | Call |
|---|---|
| 1 | **Keep the polar body. Add tentacles, spikes and droplets as separate polygons.** They join the body by draw order (all rims first, then all black fills). There is no SDF and no grid. |
| 2 | Fake the smooth-min look in only three places: the tentacle root (a polar mound plus a flared root), pinch-off (a neck spring), and re-merge (a bridge tube plus a mound). |
| 3 | **Tentacles:** a pool of 5. Each is a 10-node spring chain drawn as a tapered tube with a **round bulb tip**. Lengths run 0.6–2.4 r, capped at 110 px. They reach toward the tap, the run target, a bright spot in the show, or the beat. |
| 4 | **Spikes stay sharp and short; tentacles are round and long.** Spikes are field responses. They pop above a threshold, with hysteresis, and lean along field lines toward the attractor point. |
| 5 | **Jiggle:** surface modes n = 2–5 (cos and sin each) on underdamped springs (ζ 0.22–0.32). The modes are kicked by pokes, landings, tentacle recoil, droplets and stopping. Area is preserved with an `R_eff` correction plus mass borrowed into the tentacles. |
| 6 | **Eyes are never covered and never shrink below k 0.30.** The side floor becomes dynamic (eye half-width + 3 px), the top and bottom floor drops to 0.72 r, and the eyes scale down by up to 9 % on tall squash. |
| 7 | Cost: about **+0.6 ms on average and +1.0 ms at worst** on top of the shipped 2.97 / 3.97 ms. The budget is 4.0 ms average and 5.5 ms maximum. Tier C (the shipped look plus jiggle) costs the same as today. |
| 8 | The blob is not what sets the frame rate (see §1.4). Reaching 30 fps for real means changing the main loop, which is a separate decision for Kevin. |

---

## 1. Representation

### 1.1 Options weighed

| Option | Verdict | Why |
|---|---|---|
| **A. 2D SDF/metaball field, coarse grid + marching squares** | Rejected | The reach area is about 180×180 px. A 4 px grid has 2,500 nodes. After per-cell culling that is about 15 k capsule-distance + smooth-min evaluations at roughly 60 cycles each: **about 3.8 ms on its own**, plus marching squares. Tips and droplets under 4 px fall between grid nodes and pop in and out each frame (R1 pitfall 1: boil). A 2 px grid is about 15 ms. It also throws away the vertex-aligned spike tips from R1 §1.1. |
| A′. SDF per pixel or per-row root solve | Rejected | About 32 k px × 5 primitives = 25+ ms. |
| B. Bigger polar r(θ) | Rejected | r(θ) has one value per angle, so it can't curl, overhang or reach past about 1.5 r without long thin spokes. |
| **C. Polar body + separate tentacle, spike and droplet polygons, joined by draw order** | **Chosen** | The cost scales with edges and perimeter, not area. Tips are exact to 1/16 px, so a 2.5 px bulb stays stable. Curls and overhangs are free (a tentacle is any polyline). It reuses the shipped `partyScan`/`partySpans` rasteriser as it is, keeps `PARTY_MAX_X 16` because each shape is filled alone, and keeps every R1 term. |

SDF gives you necks, merges and fillets automatically. Those show up in only three moments here: root, pinch-off and re-merge. §2.6 builds each one explicitly, which is cheaper and easier to control.

### 1.2 Draw order (the union trick)

The shipped renderer draws the rim and the body in one pass for one shape. If two shapes overlap that way, the second shape's rim would land on the first shape's black pixels and leave a face-coloured line inside the blob. The fix is to change the order:

```
effect → shadow (body only)
→ RIM rings of all small shapes (tentacles, spike polys, droplets, bridges): grown 1.5 px minus their own inner spans, RMW
→ body: rim ring + black interior in one pass (unchanged code)
→ black interiors of all small shapes (memset spans)
→ body sheen → tentacle sheen lines → droplet glints → eyes (always last)
```

- A rim pixel later covered by any interior turns black, so the union comes out correct.
- Where two rings overlap outside the shapes, the pixel gets the rim operation twice: `max(bg/4, faceFloor)`. That only happens in the crease where two shapes meet, and it reads as a small contact shadow. Keep it.
- Each small shape goes through `partyScan` 3 times (the grown rim, its inner spans, then the interior fill). Change `partyScan` so it clears only the rows of `partyRowN` that the shape touched, instead of `memset(…, LCD_HEIGHT)`.

### 1.3 Cost estimate, ESP32-S3 at 240 MHz, 1.69, size M

Shipped and measured (`/state` → `party.render`): **2.97 ms average, 3.97 ms maximum** (CoreS3 4.3 ms average). The docs don't record the per-stage split, so read `stagesUs` before the port. The additions below are estimates. The worst case has 5 tentacles, 9 spike polygons, 4 droplets, 2 bridges and porcupine together.

| Stage (`stagesUs` index) | Added, typical (3 tentacles, 1 droplet) | Added, worst |
|---|---|---|
| 0 step: 8 mode springs, 5×10×2 node springs at about 5 sub-steps, length pass, droplets | +0.08 ms | +0.15 ms |
| 1 outline: mode terms by rotation (2 recurrences per mode), tentacle/spike/droplet vertices (about 26 + 45 + 48) | +0.04 ms | +0.06 ms |
| 3 rims + fills of small shapes: about 60–100 rows × 3 scans per tentacle, ring about 300 px RMW per tentacle | +0.35 ms | +0.65 ms |
| 4 tentacle sheen lines, glints | +0.03 ms | +0.05 ms |
| bright-spot sampling (12 px every 4th frame) | ~0 | ~0 |
| **Total** | **+0.5 to 0.6 ms → about 3.5 ms average** | **+0.9 to 1.0 ms → about 5.0 ms maximum** |

**Budget (1.69):** 4.0 ms average and 5.5 ms maximum. That is 1/8 of a 33 ms frame, so the blob never sets the frame time. CoreS3: 5.5 ms average.

**Memory:** about +5 KB static. Tentacle nodes are 5 × 10 × 2 `BotSpring` × 20 B = 4 KB. Polygons, droplets and modes add about 1 KB. The row tables don't grow (still 2 × 280 × 16). No heap.

**Tiers.** Pick the tier per board at compile time. A runtime guard then caps live tentacles at 3 while the maximum over a 2 s window is above 5.5 ms, and lifts the cap after 10 s under 4.0 ms.

| Tier | Contents | Expected |
|---|---|---|
| **A** (1.69 default) | 5 tentacles × 10 nodes, spike polygons, 4 droplets + 2 satellites, modes n = 2–5, tentacle sheen | about 3.5 / 5.0 ms |
| **B** (CoreS3 and Stackchan to start, since they measure 4.3 ms today; promote if they fit) | 3 tentacles × 8 nodes, R1 polar spikes (with the threshold and hysteresis from §2.5), 2 droplets, no satellites, modes n = 2–4, no tentacle sheen | about +0.35 ms |
| **C** (floor = v3.8.0) | No tentacles or droplets. Modes n = 2–4 on the shipped body | about +0.05 ms |

### 1.4 Frame rate (needs Kevin's call, out of scope for the motion work)

The loop is `runBotMode()` → `pushSprite` → `delay(33)` (`vizbot.ino:740`). Pushing 240×280×2 B at 40 MHz SPI (`display_lcd.h:211`) takes about 27 ms if `pushSprite` blocks. The real loop is therefore about 33 + 27 + effect + 3 ms, which is **about 13–17 fps**. The eye-art doc says about 20. The blob layer is 5–8 % of that time. 30 fps needs:

1. a `loopUs` stat in `/state` (no loop timing is measured today),
2. deadline pacing (`delay(max(1, 33 − elapsed))`),
3. then, optionally, an 80 MHz SPI trial on the 1.69, or an async DMA push from a double-buffered canvas in PSRAM.

The motion below is built to read at 15–20 fps:
- No mode is faster than 4.5 Hz.
- Fast reaches smear (§2.3).
- Physics is in real time, so it improves automatically if the loop gets faster.

---

## 2. Tentacles

### 2.1 Geometry

| Item | Value (M px) |
|---|---|
| Pool | 5 slots (tier B: 3). **At most 2 moving fast at once.** At rest: 0 or 1. |
| Nodes | 10 (9 segments, ℓ = L/9). Node 0 sits at 0.8·r(θr), inside the body. Node 1 is on the rim. |
| Root half-width w0 | reach/grab 0.20 r (8.8). Feeler/streamer 0.16 r (7). Flick 0.14 r (6.2). |
| Width profile | `w(s) = w0·(1 − 0.72 s)^1.2 + flare`, with `flare = 0.5·w0·(1 − s/0.2)²` for s < 0.2 (root fillet) |
| Tip | A neck at s = 0.9 with half-width `wn` (spring, normally `0.75·b`), then a **round bulb** of radius `b = max(2.5 px, 0.085 r)·bulbScale` (3.7 px). Drawn as a 7-vertex arc. |
| Polygon | Left offsets for nodes 1…8 (normals by central difference), the bulb arc, then right offsets back: **26 vertices**. |
| Root mound (fake smooth-min) | Add `mR·R·max(0, cos(θ − θr))^8` to the body's r(θ). That is about 23° half-width at half height, or 18 px of arc, which matches 2·w0. `mR = 0.12·λ` (λ = length fraction). The tube side has to meet the mound flank at **≥ 150°** so the crease doesn't show at 1 px. The prototype checks this (§6). |
| Bend limit | ≤ 35° per joint and ≥ ℓ ≥ w at that joint, so the inner side never folds. |
| Sheen (tier A) | One 1 px line on the upper-left side of the tube where w ≥ 4 px, in the body's sheen colour. Plus a 1 px glint on bulbs where b ≥ 4. |
| Length | Feeler 0.8–1.3 r. Tap reach up to **2.4 r (106)**. Grab 1.6–2.0 r. Crawl latch 1.4–1.8 r. Streamer 0.6–1.2 r. Flick 0.7–1.0 r. Hello 1.2 r. Exit reach 1.8 r. **Hard cap 110 px** (this limits L to 2.04 r). |
| Root angle θr | Within **±70° of αH** (the inward cone). Roots stay ≥ 28° apart. Only streamers during a run may root at the back. |
| On screen | Clamp each node to the screen inset by 3 px and to outside the 1.69 corner arcs (r_c 40). A clamped tip bunches up (the bulb grows ×1.2) instead of being cut off. Overlays still draw on top. |

### 2.2 Dynamics

The pose is an analytic curve. The nodes chase it on springs whose frequency drops toward the tip, which gives follow-through and a wave that travels outward without a Verlet solver.

```
tangent angle along the tentacle, s in [0,1]:
ψ(s) = ψ0 + Φ·s² + A_sw·s·sin(2π·f_sw·t − 2.5·s + φ)
pose point P_i = root + ∫ λ·Lt·(cos ψ, sin ψ) ds, sampled at s_i = i/9
node i (≥ 2) = a 2D BotSpring toward P_i with f_i = 3.2·(1 − 0.5·i/9) Hz and ζ_i = 0.6 − 0.25·i/9
then one length pass: clamp each segment to [0.85ℓ, 1.15ℓ] from the root outward
nodes 0 and 1 ride the body (the body centre plus the rim radius at θr)
```

| Spring | f (Hz) | ζ | Notes |
|---|---|---|---|
| Length λ, grow (feeler, tap) | 1.6 | 0.5 | 90 % in about 280 ms, 16 % overshoot |
| Length λ, grab (poke dash) | 3.0 | 0.6 | about 140 ms |
| Length λ, retract | 2.4 | 0.75 | about 350 ms |
| Length λ, snap-back after pinch-off | 3.5 | 0.5 | |
| Curl Φ | 1.2 | 0.5 | Reaching 0. Retract target **+2.2 rad** (it rolls up into the body). Hello: ±1.0 rad, 1.4 Hz × 3 cycles |
| Aim ψ0 | 1.4 | 0.8 | Toward the target direction, 70 % target / 30 % rim normal |
| Root angle θr | 0.8 | 0.9 | Re-roots only when the target moves more than 25° |
| Bulb scale | 2.0 | 0.4 | Boop kick +0.4. Smear: the bulb stretches along the tip velocity up to ×2 when moving more than 8 px per frame (area kept) |
| Neck wn (pinch) | 6.0 | 1.0 | Target 0 when pinching, about 80 ms |
| Sway | — | — | Feeler A_sw 0.35 rad at 0.45 Hz. Dancer (beat) 0.5 rad, kicked per beat. Streamer 0 (inertia does it) |

Streamers (trailing, during runs) use soft nodes: f_i = 1.4 → 0.8 Hz and ζ 0.3. They lag and whip.

Body–tentacle reaction:
- Launch kicks mode 2 along the tentacle axis by +0.06 (the body elongates toward it).
- Retract absorb kicks mode 2 by −0.04 and mode 3 by +0.03 at θr.
- Live tentacles borrow mass (§3.3).

### 2.3 Targets (what a tentacle reaches for)

| Source | When | Target and length | Tentacles |
|---|---|---|---|
| **Tap point** | A tap outside the blob | Tip IK to the finger: `Lt = min(dist − 6, 2.4 r, 110)`. Beyond reach: full length, the bulb stretches ×1.4 toward the finger, and the body leans (bulge 0.15) | 1 (the same slot re-targets on repeat taps; 3 taps in 2 s add 2 flankers at ±30°, 1.2 r) |
| **Run direction** | Poke dash, roam | A point on the run path, latched in world space (§4) | 1 grab or 2 crawl latches, plus 2 streamers on a dash |
| **Bright spot** | Idle feeler | Every 1.5 s, sample 12 canvas pixels on two arcs (1.6 r and 2.2 r) across the inward cone. Take luma `(R·2 + G·5 + B)/8`, low-passed over 1 s. Aim at the brightest point if it beats the mean by more than 25 %, otherwise pick a random point in the cone | 1 feeler |
| **Beat** (CoreS3 mic) | `audioSpectrum` rising edge | Two dancers at αH ± 35°, 0.9 r. Each beat kicks sway, alternating sign | 2 |
| **Pulse** (1.69) | Every 5–12 s | Flankers at αH ± 35° on 40 % of pulses | 2 flicks |
| **Gaze** | The eye saccades more than 0.2 R (R1's gaze reach) | 25 % of those fire a feeler (1.0 r) along the gaze, instead of R1's 0.25 fan | 1 |
| **Screen centre** | Exit | 1.8 r toward (W/2, H/2), latched | 2 |

### 2.4 Lifecycle per slot

`IDLE → GROW → HOLD (track / sway / latch) → RETRACT (curl) → IDLE`, with an optional `PINCH` from HOLD or RETRACT.

- **Feeler:** 2.5–5 s hold. Lands on a bright spot with 2 bulb boops, 0.6 s apart.
- **Tap:** reaches the finger at about 330 ms, boop at 360 ms, holds 500 ms with sway, retracts over about 900 ms (curl to 2.2 rad while λ → 0).
- **Retract absorb:** at λ < 0.08 the slot frees and the mound decays (the mound's λ term).

### 2.5 Spikes (R1 fan upgraded)

| Change | Value |
|---|---|
| Spikes become separate 5-vertex polygons | Base on the rim at r(θk) − 1 px, base half-width 0.10 r (4.4). Flank points at 40 % height, inset 25 %, so the flanks are concave. Then the tip. |
| **Field lines** | Tip direction = normalise(lerp(radial, toward the attractor point Pa, 0.6)). Pa is the tap point, or a virtual point at 2.5 r along α for pulses and beats. The fan **converges** on the finger, the way ferrofluid reaches for a magnet. |
| **Threshold (Rosensweig)** | Below F_on = 0.35 only the mound grows (`g = 0.12·F`). Above it the spikes **pop**. They stay until F < F_off = 0.22 (hysteresis). |
| Height | `F·0.55 r·gauss·jitter` [was 0.42 r], so 24 px at most |
| Rising spring | 3.2 Hz, ζ 0.45 [was 2.6 / 0.55]: a snappier pop with about 20 % overshoot |
| Falling spring | Unchanged (4.5 Hz, ζ 0.38, dimple floor −0.05 r) |
| Porcupine (shake) | Stays polar (R1). Height 0.30 r [was 0.22] |
| Tier B | Polar spikes as R1, with the threshold and the new height |

### 2.6 Pinch-off, droplets, re-merge

| Item | Value |
|---|---|
| Triggers | (1) A flick: tip speed > 6 r/s while retracting with λ·Lt > 1 r. (2) Scripted: dash landing (50 % per streamer), shake (3–5 drops), strong-bass flick on CoreS3, 1 in 4 pulse flicks on the 1.69. **Rate cap: 1 per 1.5 s, except shake.** |
| Pinch | The neck spring wn → 0 (about 80 ms) while the bulb swells ×1.15. When wn < 0.8 px, spawn a droplet at the bulb centre with the tip velocity × 1.1. The rest of the tentacle snaps back (λ at 3.5 Hz) and has **no bulb** (a short sharp thread for about 150 ms, which is real ferrofluid behaviour) |
| Satellite | 40 % chance: a 2 px drop at the neck point, at 50 % of the velocity. It returns first. |
| Droplet radius | 0.07–0.11 r (3.1–4.8). Never below 2.5 px, except a satellite (2 px for ≤ 400 ms) |
| Pool | 4 droplets + 2 satellites (tier B: 2 + 0) |
| Flight | Ballistic 0.25–0.45 s, drag `v *= e^(−3·dt)`, no gravity. Bounces off the screen inset (restitution 0.4): a "boing" off the bezel |
| Return | Then a 2D spring toward the nearest rim point: **1.4 Hz, ζ 0.55**. Lifetime cap 2.0 s, then a forced return at 2.5 Hz |
| Shape | An ellipse stretched along the velocity, `1 + 0.35·min(1, |v|/(4 r/s))`, area kept. 12 vertices |
| **Bridge** (fake smooth-min) | When the gap to the rim is < 0.3 r: a tapered tube from the drop to the rim, mid half-width `0.6·rd·sqrt(1 − gap/0.3 r)`, plus a body mound toward the drop of `0.10·(1 − gap/0.3 r)` |
| Absorb | At gap < 1 px the drop moves into the mound and shrinks to 0 over 120 ms (2.6 Hz, ζ 1). The mound gains the drop's area, then decays. Kick mode 3 by +0.04 at that angle and E by +0.15 |

---

## 3. Jiggle (soft body)

### 3.1 Surface modes

The body radius gets `R_eff·Σ_{n=2..5} (c_n·cos nθ + s_n·sin nθ)`. Each component is a `BotSpring`. Compute it per vertex by rotation recurrence, the same way the shipped edge waves do (2 extra rotations per mode). The frequency ratios are Rayleigh's droplet ratios, compressed so nothing goes over 4.5 Hz (pitfall 3: temporal aliasing at 15–20 fps).

| Mode | f (Hz) | ζ | Clamp | Settles (5 %) |
|---|---|---|---|---|
| n = 2 (replaces J and St) | 2.0 | 0.22 | ±0.28 | about 1.1 s |
| n = 3 | 3.0 | 0.25 | ±0.14 | about 0.65 s |
| n = 4 | 3.8 | 0.28 | ±0.09 | about 0.45 s |
| n = 5 (tier A) | 4.5 | 0.32 | ±0.06 | about 0.35 s |

**Mode 2 target:**
```
(c2*, s2*) = (J0 + landSplat, 0) + S·(cos 2φv, sin 2φv)
J0 = 0.05 (rest drop),  S = 0.18·clamp(|v| / (2.5 r/s), 0, 1)  [was 0.10, on a separate 4 Hz ζ 0.8 spring]
landSplat = −L/r · 0.9 while the lift L < 0 (R1)
```
Because mode 2 is underdamped, a run that stops overshoots into a squash along the travel axis and wobbles out. That is the jello stop, and it comes for free. Keep R1's freeze of φv below 0.2 r/s.

### 3.2 Kicks (normalised peaks)

| Event | n = 2 | n = 3 | n = 4 | n = 5 | Phase |
|---|---|---|---|---|---|
| Poke at θD | −0.10 | −0.07 | −0.05 | −0.03 | `(cos nθD, sin nθD)`: a dent at the finger. Plus the local dent D −0.22 r [was −0.18] |
| Landing (dash) | +0.22 | — | +0.06 | — | screen axis (wide) |
| Landing (roam, enter) | +0.12 / +0.16 | — | +0.03 | — | screen axis |
| Take-off | −0.14 | — | — | — | screen axis (tall) |
| Tentacle launch | +0.06 | — | — | — | along the tentacle |
| Tentacle absorb | −0.04 | +0.03 | — | — | at θr |
| Droplet absorb | — | +0.04 | — | — | at the drop's angle |
| Beat (CoreS3) | +0.04 + 0.08·bass | — | — | — | screen axis (a squash pump) |
| 1.69 pulse ("lub-dub") | +0.10, then +0.06 after 180 ms | — | — | — | screen axis |
| Shake | 0.18 | 0.12 | 0.08 | 0.05 | random phase per mode |
| Settle twitch (idle) | 0.025 on n = 2 or 3, every 4–9 s | | | | random |

### 3.3 Volume

- `R_eff = R·B / sqrt(1 + ½·Σ(c_n² + s_n²)) · sqrt(1 − m)`
- `m = min(0.08, 0.3·(A_tentacles + A_spikes) / (π R²))`. These areas come from the polygons built this frame (shoelace formula, about 0.01 ms). He "pours himself into the reach": the body shrinks by up to 4 % in radius.
- The floors in §5 still apply after this.

### 3.4 Idle and secondary motion

| Term | New [R1] |
|---|---|
| Breath | ±0.05 r at 0.21 Hz [±0.04] |
| Edge waves a3 / a4 / a5 | 0.065 / 0.045 / 0.027 r [0.045 / 0.030 / 0.018]. Rim speed is still ≤ 13 px/s |
| Agitation gain | `A = 1 + 2.0·E` [1.5·E] |
| Eye offset spring | **2.0 Hz, ζ 0.38** [2.4 / 0.5]: the eyes slosh more and settle about 0.5 s after the body. Clamp unchanged (inner radius − 3 px) |
| Hop lift | Unchanged spring. The tap hop drops to 0.12 r [0.22], because the tentacle now does the reaching |
| Dent D | 3.2 Hz, ζ 0.28 [0.32] |

---

## 4. Beat sheet

Times are from the trigger. "Body" covers the modes, hop and spikes. "Eyes" covers `botEyes` (padX/padY = look, `popS`, blinks, existing expressions).

### Idle (the stillness rule still holds: at rest only breath, the waves, the eyes, and at most 1 feeler)
| t | Body | Tentacles | Eyes |
|---|---|---|---|
| always | Breath, edge waves, settle twitch every 4–9 s | 0 | Normal saccades and blinks |
| every 6–14 s (55 %) | Mound grows at θr | Feeler grows 0.8–1.3 r toward the bright spot (1.6 Hz), sways 0.45 Hz, holds 2.5–5 s. On a bright spot: 2 boops | Look along the feeler 60 % of the time. Pupils "small" |
| retract | Absorb kick | Curl to 2.2 rad, λ → 0 over about 350 ms | Back to idle |
| fidgets (12–25 s) | R1 probe, shiver and scoot stay. Probe is now a feeler at 1.3 r | | as R1 |

### Pulse (1.69, every 5–12 s) and beat (CoreS3)
| t | Body | Tentacles | Eyes |
|---|---|---|---|
| 0 | 1.69: lub-dub mode-2 kicks (0 and 180 ms). Spike fan F 0.5 → pops (above threshold), converging on a virtual Pa | 40 %: 2 flicks at αH ± 35°, 0.9 r, out and back in 500 ms. 1 in 4 of those throws a droplet that arcs out about 1.2 r and returns | none |
| beat | CoreS3: smoothed F pump (R1) + mode-2 squash pump per beat | 2 dancers live while music is heard (they retract after 3 s of silence). Sway kick ±0.5 rad, alternating per beat | Pop on bass > 0.7 (R1, ≥ 600 ms apart) |
| bass > 0.75 | Hop 0.06 r (R1) | One dancer flicks and pinches off a droplet (≤ 1 per 2 s) | Glance at the droplet for 400 ms |

### Tap (outside the blob)
| t | Body | Tentacles | Eyes |
|---|---|---|---|
| 0 | Mound toward the tap. Mode-2 tall kick −0.06 | Slot picks θr toward the tap | Saccade to the tap (padX/padY, 600 ms+) |
| 80 ms | Small hop 0.12 r. Spike fan converges on the finger (F 1.0, 500 ms) | Reach toward the finger, 1.6 Hz, λ·Lt = min(dist − 6, 2.4 r) | Existing `onTap()` expression and pop |
| ~330 ms | Launch reaction kick | The tip arrives. **Boop** at 360 ms (bulb +0.4) | Hold the look |
| 360–860 ms | | Hold with sway 0.25 rad, the tip tracks a finger that moves | |
| ~900 ms | Absorb kick on return | Retract: curl 2.2 rad, 2.4 Hz | Look relaxes |

### Shake
| t | Body | Tentacles | Eyes |
|---|---|---|---|
| 0 | Porcupine 0.30 r for 450 ms. Random-phase kicks on all modes. R1 side jitter 5.7 Hz × e^(−5t) | All live tentacles whip back (3.5 Hz). **Splash: 3–5 droplets** fly out across the inward cone at 150–260 px/s | Forced blink, DIZZY |
| 0.45–2.0 s | Ringing modes die out | The droplets bounce off the bezel and come back one at a time with bridges, each absorb kicking mode 3 | DIZZY until the R1 timeout |

### Poke-and-run (a dash to another random corner)
| t | Body | Tentacles | Eyes |
|---|---|---|---|
| 0 | Dent D −0.22 r + mode kicks at θD. Crouch (mode 2 +0.18) | All live tentacles retract instantly (startle) | Forced blink → SURPRISED |
| 120 ms | | A **grab** tentacle shoots toward the destination (3.0 Hz) to 1.8 r. Its tip latches in world space at path p = 0.5 | Look at the destination |
| 260 ms | Launch on the shipped path (Q at 1.5 Hz, ζ 0.85, bows 45 % toward the centre). Take-off kick −0.14. Stretch up to 0.18 | 2 **streamers** (0.6–1.2 r) root at the back and lag behind. The grab shortens as he passes its tip and is absorbed by p = 0.6 | Look along the path |
| ~0.95 s (Q > 0.97) | Landing: mode 2 +0.22 and mode 4 +0.06. Spike fan F 0.7 toward αH. The underdamped mode 2 rings for about 1 s | The streamers whip forward past the body. Each has a 50 % chance to pinch off a droplet that lands 0.5–1 r ahead, and it is absorbed within 400 ms | Blink |
| +0.5 s after landing | | | Face returns (R1) |

### Roam (his own idea, every 25–60 s)
| t | Body | Tentacles | Eyes |
|---|---|---|---|
| 0–700 ms | Mound toward the destination | A **crawl latch** reaches (1.6 Hz) to 1.6 r along the path and plants (the bulb flattens to 0.8) | 0.7 s glance at where he's going (R1) |
| 700 ms | Travel on Q (0.95 Hz, ζ 0.9). The body flows toward the latch (bulge 0.15 along the path, stretch) | Latch 1 shortens as he passes and is absorbed | Look along the path. The face doesn't change |
| path > 3 r (the 1.69's long edge is 148 px = 3.4 r) | Q target 0.5 → hold 150 ms → 1.0 (**a two-step crawl**, about +0.6 s) | Latch 2 reaches from the front rim at p ≈ 0.45 (hand over hand) | |
| landing | Mode 2 +0.12. No droplets | All absorbed | Blink on landing |

### Enter party mode
| t | Body | Tentacles | Eyes |
|---|---|---|---|
| 0 | R1 swap under a blink. The stroke ellipse flies on P (1.4 Hz, ζ 0.82) | 2 streamers trail the flight (they appear once P > 0.3) | Blink |
| landing (P > 0.97) | Mode 2 +0.16, mode 4 +0.03. **Splash crown:** spike polygons pop across the whole inward cone (F 0.8, 300 ms) | 2–3 droplets bounce out about 1 r and are absorbed by about 900 ms | Blink |
| +400 ms | | **Hello:** one tentacle at 1.2 r curls ±1.0 rad at 1.4 Hz for 3 cycles, then retracts | HAPPY for 1.5 s, looking at the waving tentacle |

### Exit party mode
| t | Body | Tentacles | Eyes |
|---|---|---|---|
| 0–300 ms | Crouch (mode 2 +0.10, R1) | All idle tentacles retract. Droplets are forced home (2.5 Hz). **2 reach** tentacles grow 1.8 r toward the screen centre and latch | Look at the centre (R1) |
| 300 ms | Launch (R1 kick and lift). The body is "pulled" by the latches | The latches shorten as he flies and are fully absorbed by P < 0.5 | |
| P < 0.5 | All modes target 0 (stiffen to ζ 0.6) so the drop is a clean ellipse before the swap | none | |
| P < 0.03 | Swap back under a blink (R1) | none | Blink |

---

## 5. How far to exaggerate (eye legibility, not creepy)

### 5.1 Hard limits

| Limit | Value | Reason |
|---|---|---|
| Eyes draw last and nothing ever covers them | always | Legibility. Tentacles that cross the body are black on black anyway |
| Side floor (within ±35° of horizontal) | `max(0.80 r, eyeHalfWidth(k_eff) + 3 px)`, applied with a smooth max (k = 2 px) so the sides never look sliced | Replaces the fixed 0.97 r, so a tall squash is possible |
| Top/bottom floor | **0.72 r** [0.85] | The content reaches only 1.46 R = 25 px (0.57 r) up |
| Eye scale on a tall squash | `k_eff = k·(1 − 0.35·max(0, −c2_axis))`, ≥ 0.30. At M that is down to −9 % (0.36 → 0.33) for ≤ 300 ms | The eyes squish with the body. The size S floor never scales down, so S gets smaller tall squashes |
| Eye offset | Dynamic clamp (R1). No fixed 0.08 r | |
| Total coverage (body + tentacles + drops) | ≤ 18 % of the screen at M (the body is 9 %) | The show stays the star |
| Live tentacles | ≤ 5, **≤ 2 fast at once**, ≤ 3 for more than 1.5 s | Many waving limbs read as spider/horror |
| Tentacle length | ≤ 2.4 r and ≤ 110 px | |
| Tip speed | Smear above 8 px per frame. No speed cap, but the reach spring sets about 350 px/s at most | |

### 5.2 Playful, not creepy: rules

1. **Round tips on tentacles** (a bulb ≥ 2.5 px). Sharp points only on short spikes (≤ 0.55 r). No hooks, suckers, claws or needles.
2. **Chunky:** root half-width ≥ 0.14 r, and the taper ends in a bulb, never a hair. Thin, long and many is the horror recipe.
3. **Slow at rest:** idle sway ≤ 0.45 Hz with 0 or 1 feelers. No constant writhing. Stillness makes the reaches special.
4. **Purpose:** every tentacle reaches *for* something (a finger, a light, the beat, where he's going), and the eyes look at it. Purposeful reads curious. Aimless reads eerie.
5. **No radial symmetry of limbs.** Tentacles cluster in the inward cone (±70°). A ring of legs is a spider. The porcupine is the only all-round effect, and it lasts 450 ms.
6. **The eyes stay kind.** No new angry or scary expressions are triggered by tentacle events. Pair the reaches with the existing curious, happy and surprised faces.
7. **Liquid, not flesh.** The sheen on tentacles and the droplets that merge back make it read as fluid. Never draw segments, joints or texture.

---

## 6. Prototype first

**The smallest proof:** one HTML canvas at **240×280 device pixels**, scaled ×3 with `image-rendering: pixelated`, rasterised by the shipped algorithm (ported, not canvas paths). It needs:

1. A JS `BotSpring` (8 ms sub-steps), the shipped 120-vertex polar body, and `partyScan`/`partySpans` ported as they are (x in 1/16 px, `ceil(x − 0.5)` rules) into an `ImageData`.
2. Background: a moving plasma, plus a toggle to black/galaxy (to test the rim on dark scenes).
3. **One tentacle slot** (§2.1 and §2.2) with the root mound + flare, the bulb and the bend limit. Click the show to reach, release to retract with a curl.
4. **The §1.2 draw order** with the rim, so you can see whether the union and the crease read as one liquid.
5. Modes n = 2–4 (§3). Click the body to poke-kick them.
6. Key **F** flicks: pinch-off → droplet → return → bridge → absorb.
7. A frame-rate toggle of 15 / 20 / 30 / 60 fps (with the physics in real time).
8. Sliders for flare, mound height, mound power and bulb size.
9. A "crease" debug colour for pixels that get the double rim.
10. Per-frame counters: crossings, rim RMW pixels and fill pixels, turned into µs with constants calibrated so the shipped body alone reads 2.97 ms.
11. Eyes: reuse the bundled `eyelab/shared` from the R1 prototype artifact (RMM4CvsGzmAg6Fujt3HJqW) at k 0.36. If that takes more than 30 min, draw two placeholder shapes of the right size.

**Pass criteria (Kevin judges):**
1. No visible seam at the root, on plasma and on galaxy.
2. A 2.4 r reach with a curl-back reads smooth at 15 and 20 fps.
3. Pinch-off and re-merge read as liquid.
4. The eyes stay readable through a maximum poke jiggle.
5. The counters say ≤ +1 ms over the shipped model.

**Time:** about 3–4 h. **Then:** port the beat sheet into the R1 party prototype for Kevin to tune (about 3 h). Then the firmware port behind a compile-time tier, with `stagesUs` read on old-dino.

---

## 6a. Tuned defaults (Kevin, 2026-10-26, Tentacle Lab)

These multiply the spec values above and are the starting point for the firmware port.

| Knob | Value | Meaning |
|---|---|---|
| Reach | 1.20× | tentacle length |
| Jiggle | 1.70× | surface-mode kick amplitude |
| Chunkiness | 1.30× | tentacle root/body width |
| Tip bulb | 1.30× | bulb radius at the tip |
| Big reaches | 60% | share of random reaches that go big |

Eye safety zone: the art director's (body floor 0.85 R toward the eyes, 0.97 R on the sides). The motion designer's variant is dropped.

## 7. Open questions for Kevin

1. Is the frame-rate work (§1.4: loop pacing, SPI clock, DMA) in scope? The motion is built to read at 15–20 fps either way.
2. Should the tentacles be allowed to draw under the clock pill in top corners? Overlays draw on top, and this spec assumes yes.
3. P2: tapping a tentacle (rather than the body) gives a "tickle": a fast recoil, a mode 3 ripple and HAPPY. Build it?
