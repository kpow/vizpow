# Party mode round 2: ferrofluid pattern library

Status: shipped in v3.9.0. Built in firmware (`vizbot/party_blob.h`): Breathing Crown, Calm Dome, Rosensweig Crown, Carousel, Peak Split, Stadium Wave (at medium height), Spike Merge (new: nine small spikes merge into five, then three tall), Bloom, Feelers and Long Reach, with the scheduler from §4. Not built yet: Shimmer, Pearl Chain, Magnet Approach, Grand Crown.

Kevin's calls after trying them in the lab: medium patterns lead the mix (small ones get lost on the screen), Breathing Crown is about 30 % taller than written here, idle feelers are short only, and patterns may **stack**: each owns its own bank of spike slots, up to three play at once and their spikes add up (`GET /bot/party?stack=off|some|wild`, default some). Calm Dome always plays alone. `GET /bot/party?seq=<name>` plays one on demand.
Builds on `docs/party2-art-direction.md` (AD), `docs/party2-motion-spec.md` (MS, tuned values in §6a) and `vizbot/party_blob.h` as shipped to the test device.

Units: `r` is the tier body radius (S 37, **M 44**, L 54 px). Pixel values are for M on the 1.69. Heights are added to the body radius, as fractions of `r`. Angles are in screen space with y down. `αH` is the direction from home to the screen centre (`alphaHome()`). "Slot k" means the existing spike-fan grid: 9 slots, 18° (6 vertices) apart, slot 4 on the attractor `al`. Times are ms from the sequence start. Tentacle lengths are final lengths, with Kevin's 1.2× reach already applied.

---

## 1. The point

Party mode feels canned for three reasons:
- **One intensity.** Nearly every idle beat is the same move: a feeler group with 60 % of reaches at 1.8–2.4 r. From home, the screen centre is only about 1.05 r past the rim (home (172, 65) to (120, 140) is 91 px, minus r = 44), so a big reach crosses the middle of the screen every time.
- **One shape language.** One limb at a random angle, again and again.
- **No pattern.** The angles are random, so the motion reads as noise, not as a field.

The library fixes this in three ways:
- **An intensity ladder.** Most of the time goes to small and medium sequences that stay in his half of the screen. Big moves become rare punctuation.
- **A magnetic vocabulary.** Crowns, rotating fields, splitting peaks, field-line arcs and bead chains, all **mirror-symmetric about αH**. That symmetry is what makes them read as "a magnet is doing this", not as fidgeting.
- **A scheduler with memory.** No repeats, cool-downs, and a build-up and release, so the minute has a shape.

Most of the new sequences are spike-only. That costs about 0.05 ms against about 0.12 ms per tentacle, so the variety is nearly free.

---

## 2. Art direction guardrails

### 2.1 Zones around the blob (all corners, M)

| Zone | Angle from αH | Clearance past rim | What may go there |
|---|---|---|---|
| **Show arc** | within ±65° | ≥ 1 r | everything in this spec |
| **Shoulder** | 65°–100° | 0.6–1 r | spikes ≤ 0.22 r, hills, no tentacles (existing ±70° cone rule) |
| **Back** | beyond 100° | 21–33 px to the bezel or the r 40 glass arc | nothing new; only the porcupine at 0.4× as today |

The show arc is the brief's 120–150°: here it is 130°. The shoulder rule matches the porcupine's 100° cut in `buildOutline`.

### 2.2 Symmetry rules

1. **The axis is αH**, latched when the sequence starts. Set `alT = αH` and pin `al` to it. While a medium or big sequence runs, freeze the wander target so the axis cannot drift under a symmetric pattern. Small sequences may wander.
2. **Odd counts sit on the axis** (1, 3, 5, 7, 9 spikes), so a spike points at the "magnet". **Even counts straddle it** (a pair at ±θ), and are used only for splits and mirrored tentacle pairs.
3. **Mirror pairs are exact.** They share the same target, the same spring and the same phase, and the sway is mirrored. The jitter on a mirrored pair is one value (0.95–1.05) used on both sides. Today's per-slot `jit` of 0.85–1.15 breaks symmetry, so it is only for the event fan.
4. **Rotation is the one allowed asymmetry.** It appears only in Carousel (equal spacing kept while moving) and the existing shake.
5. **Keep it alive with timing, not shape.** Life comes from tremble, breathing and the eyes. The silhouette itself stays symmetric.

### 2.3 Sizes per tier

| | Small / ambient | Medium | Big / hero |
|---|---|---|---|
| Spike height (max) | 0.20 r (9 px) | 0.38 r (17 px) | 0.55 r (24 px), the current fan cap |
| Spikes at once | ≤ 5 (≤ 7 in a travelling wave, only 2–3 tall at a time) | ≤ 7 | ≤ 9 |
| Spike spacing | ≥ 18° | ≥ 18° (≥ 9° only inside Peak Split's cross-fade) | ≥ 18° |
| Spike width | 5 vertices (`PARTY_SPK_LUT`) | 5; 7 when taller than 0.30 r | 7 when taller than 0.40 r |
| Hills / ripple | ≤ 0.07 r | — | — |
| Tentacles | 0 | ≤ 3, each ≤ 1.0 r, ≤ 2 growing at once | ≤ 2, up to the 2.4 r cap |
| Droplets | 0 | ≤ 3, within 1.2 r of the rim | ≤ 3, anywhere in the show arc |
| Mode kicks | ≤ 0.07 | ≤ 0.12 | existing maxima (0.30 splat) |
| Rim / tip speed | ≤ 1.5 px/frame: grow, don't pop | spikes may pop (≤ 6 px/frame) | as MS (≤ 12 px/frame) |
| How far into the show | ≤ 0.25 r past the rim (11 px): it stays a rim texture | ≤ 1.0 r tentacles, 1.2 r drops: **never past the screen centre** | may cross the centre |
| Duration | 3–7 s | 3–6 s | 2.5–4.5 s, then a Calm Dome |

**Spike reading limits at 3° vertices.** A spike is 5 vertices, about 12° at the base and about 15° to the eye. Sharp spikes need ≥ 18° between centres. At 15° they touch at the base and read as a saw blade. Over a full circle that allows at most 20, and no sequence uses more than 9.

**Smooth texture.** Hills and ripple must have a period of at least 8 vertices (24°, n ≤ 15 per circle). They then sample cleanly at any phase, and **no sequence needs a finer outline**.

**Moving sharp features.** Sharp features never move faster than one vertex per frame (75°/s at 25 fps; the cap is 60°/s). Sharp spikes move only by rotating the vertex grid with them (see §5.1), so their tips never alias between vertices.

### 2.4 Eye legibility

1. The eyes draw last. The floors are unchanged: 0.97 R within ±35° of horizontal, 0.85 R elsewhere. Every new term **adds** radius on the show arc. Nothing new subtracts, except Calm Dome, which only removes wobble.
2. The eyes stay the only bright, round, hard-edged thing. No sheen and no glint goes on spikes or beads (beads follow the droplet rules: no glint at all).
3. **Faces.** Use only existing P1 expressions: NEUTRAL, CHILL, HAPPY, SURPRISED, plus `popS` boosts. Change the expression at most once mid-sequence, plus a HAPPY at the end. Never pair ANGRY or MISCHIEF with a sharp crown, because spikes plus angry eyes read as a devil.
4. **Gaze.** In symmetric patterns, look out along αH ("at the magnet"). This keeps the face symmetric with the pattern. Gaze tracking is only for patterns that travel (Carousel, Stadium Wave).
5. **Tall squash.** Sequences kick mode 2 tall (negative) by at most −0.10, so the side floor never has to fight a squash.
6. **No blink at the peak of a hero.** Blink on the transitions instead (start, snatch, end).

### 2.5 What to avoid

| Avoid | Why |
|---|---|
| 4 or more tentacles in one pattern, or 3 of equal length fanned out | Reads as a hand or a spider. Max 3, and a centre one must be shorter |
| Spikes over 0.55 r, or thin spikes (5-wide over 0.40 r) | Sea urchin, needles. Widen to 7 instead |
| Spikes over 0.22 r in the shoulder, or any spike in the back | They hit the bezel and the r 40 corner, and read as clipped |
| A travelling crown faster than 60°/s | Wagon-wheel strobe at 15–25 fps |
| The same sequence twice in a row, or 3 spike sequences in a row | That is the canned feeling again |
| Perfect loops of more than 3 cycles | A machine, not a liquid |
| Sharp spikes aimed at a finger | AD rule 1.2: touch gets round tips |
| A crown plus the porcupine, or two layouts sharing the spike slots | One owner at a time (§4.4) |
| Tentacles that close a loop with each other (an arch whose tips touch) | A hole in the silhouette reads as a handle. Tips stop ≥ 2 tip widths (12 px) apart |

---

## 3. The library

### 3.1 Overview

| # | Name | Tier | Uses | Weight | Cool-down | Extra cost (avg / peak) |
|---|---|---|---|---|---|---|
| 1 | Breathing Crown | S | spike slots, thresholds | 17 | 12 s | +0.03 / +0.05 ms |
| 2 | Shimmer | S | hills term | 13 | 15 s | +0.02 / +0.02 ms |
| 3 | Stadium Wave | S | spike slots, lean | 12 | 15 s | +0.03 / +0.04 ms |
| 4 | Calm Dome | S | calm gain κ | 8 (+ forced) | 20 s | ≈ 0 |
| 5 | Rosensweig Crown | M | spike slots, thresholds, mound | 9 | 30 s | +0.06 / +0.08 ms |
| 6 | Carousel | M | spike slots, `al` rotation, lean | 7 | 35 s | +0.05 / +0.07 ms |
| 7 | Peak Split | M | spike slots with a custom layout | 6 | 35 s | +0.04 / +0.06 ms |
| 8 | Bloom | M | mirrored tentacle pair, 1 spike | 7 | 30 s | +0.25 / +0.37 ms |
| 9 | Pearl Chain | M | held droplets on an arc | 6 | 35 s | +0.09 / +0.12 ms |
| — | Feelers (existing `feelerGroup`, short only) | M | tentacles | 5 | 20 s | +0.25 / +0.37 ms |
| 10 | Magnet Approach | B | spikes, lean, 2 field-line tentacles, 1 held drop | 4 | 75 s | +0.45 / +0.60 ms |
| 11 | Grand Crown | B | 9 spikes, 2 flung drops | 3 | 60 s | +0.12 / +0.20 ms |
| 12 | Long Reach (existing big feeler) | B | 1 tentacle up to 2.4 r | 3 | 45 s | +0.12 / +0.15 ms |

Weights add up to 100. The tier picker comes first (§4.1), so the effective shares are set there.

---

### 1. Breathing Crown (S)

**What you see:** a small crown of five spikes breathes in and out with him, like a magnet slowly pumping under the dish.

**Silhouette:** a low mound on the axis. On each in-breath, short cones pop out of it (centre first, then outward) and sink back into the mound on the out-breath. The body never looks spiky, only "prickly for a moment".

**Layout:** slots 2–6 (0°, ±18°, ±36°), 5 vertices wide.
- Envelope: 1.0 / 0.85 / 0.6 for |k−4| = 0 / 1 / 2.
- H = 0.26 r.
- Height: `h_k = popped_k · F · H · env_k`.
- Thresholds (§5.2): `F_on = 0.32 + 0.06·|k−4|`, `F_off = F_on − 0.13`.
- Springs: rising 2.0 Hz ζ 0.6 (no overshoot), falling 3.0 Hz ζ 0.5.

| t (ms) | Field / body | Spikes | Eyes |
|---|---|---|---|
| 0–500 | F → 0.15. Mound `g = 0.12·F` on the axis | none (below threshold) | CHILL |
| 500–2500 | Breath 1: `F = 0.15 + 0.55·(½ − ½cos 2π·0.5·t')`, peaking at 0.70 at 1500. Mode 2 +0.03 at the peak | Centre pops about 1000, ±18° about 1120, ±36° about 1240. Peak centre 0.18 r (8 px), outer 0.11 r (5 px). Outer ones sink first on the way down | — |
| 2500–4500 | Breath 2, peak F 0.75 | as above, 0.20 r centre | slow blink (300 ms) on the out-breath |
| 4500–6500 | Breath 3, peak F 0.60 (it fades) | only 3 pop | — |
| 6500–7000 | F → 0 | none | back to the previous face |

- **Symmetry:** mirror, 5 on the axis.
- **Face:** CHILL, gaze on the axis at pad 0.3.
- **Music (CoreS3):** one breath per 2 beats; the bass adds +0.1 to F at the peak.

### 2. Shimmer (S)

**What you see:** the rim facing the show goes faintly corrugated, little hills and valleys swapping places, as if the field were sitting just under the threshold.

**Silhouette:** about 5–6 rounded bumps across the show arc, 3 px tall. They alternate with the bumps between them at under 1 Hz. The shoulder and back stay smooth.

**Term (new, §5.3):** `r += A·win(x)²·[m·max(0, cos nx)² + (1−m)·max(0, −cos nx)²]`, where:
- x = θ − αH, n = 15 (24° period, 8 vertices),
- `win(x) = max(0, (cos x − cos 65°)/(1 − cos 65°))`,
- `m = ½ + ½·sin(2π·0.8·t)`.

It is positive-only, so it never touches the floors. One hill sits on the axis when m = 1, so it is symmetric.

| t (ms) | Body | Eyes |
|---|---|---|
| 0–600 | A 0 → 0.07 r (3 px). Edge-wave gain κ → 0.6, so the texture reads against a quieter rim | unchanged |
| 600–3000 | Hold. The hills swap at 0.8 Hz (rim speed about 0.3 px/frame, under the 0.55 cap) | one slow blink at about 1800 |
| 3000–3600 | A → 0, κ → 1 | — |

- **Symmetry:** mirror about αH.
- **Face:** unchanged. This is the most "background" item.
- **Music:** the swap locks to the beat (one swap per beat), A 0.08 r.

### 3. Stadium Wave (S)

**What you see:** one ripple of short spikes runs across his crown from side to side and back, like a stadium wave.

**Silhouette:** at any instant only 2–3 neighbouring spikes are up, each leaning toward where the wave is going, so the crest visibly travels.

**Layout:** slots 1–7, driven directly (no thresholds).
- Height: `h_k(t) = H·env_k·bump((t − t_k)/300)`, with `bump(u) = ½ + ½cos(πu)` for |u| < 1.
- `t_k = t_pass + j·90` ms, where j is the slot's order in the pass.
- H = 0.20 r. env 1.0, but 0.8 on slots 1 and 7.
- Lean +12° in the travel direction while `h` is rising, 0° while falling (§5.4).

| t (ms) | Spikes | Body | Eyes |
|---|---|---|---|
| 0–200 | none | F 0.2 mound | look to the starting side (the side the gaze is already on) |
| 200–1100 | Pass 1: slot 1 → 7 (the crest crosses 108° in 540 ms, 200°/s of crest travel; each spike only rises and falls in place) | mode 2 +0.02 as the crest crosses the axis | padX/padY follow the crest angle at 0.5 gain (the eye spring lag makes them trail it) |
| 1100–1400 | rest | — | — |
| 1400–2300 | Pass 2: slot 7 → 1 | same | follow back |
| 2300–3400 | 50 %: pass 3, slot 1 → 7, H 0.14 r | — | HAPPY 600 ms at the end |

- **Symmetry:** mirror in time (there and back) rather than in space. Equal spacing is kept.
- **Note:** the crest travels fast, but no spike moves sideways, so nothing strobes.
- **Music:** a pass starts on a downbeat and lasts one bar.

### 4. Calm Dome (S)

**What you see:** everything settles into a smooth, glossy dome, as when a ferrofluid's field is switched off. Then comes one soft jelly wobble.

**Silhouette:** a near-perfect drop (only the mode-2 rest squash of 0.05 and the breath remain), then 2–3 low wobbles.

| t (ms) | Body | Eyes |
|---|---|---|
| 0–600 | κ → 0.12 (edge waves almost off; spring 1.6 Hz ζ 1). Mode springs ζ ×2. F 0, hills 0 | CHILL |
| 600–2100 (+0–1000 at random) | Hold the dome. The sheen crescent reads clean | one long blink (320 ms) mid-hold |
| end | Mode 2 +0.07 and mode 4 +0.03 (axis 0). κ → 1 over 1 s (spring 1.0 Hz ζ 1). Mode ζ restored | NEUTRAL |

- **Symmetry:** full (a dome).
- **Role:** the palate cleanser. Forced after every big sequence (hold 1.0 s). Run it at 50 % before a roam. It only starts when no tentacles are out.
- **Cost:** ≈ 0, or slightly negative.

### 5. Rosensweig Crown (M)

**What you see:** the field comes on: one spike, then three, then five, then seven pop out of a mound and stand there trembling. Then all of them drop back at once and the drop slumps.

**Silhouette:** a symmetric crown across 108° of the show arc. The centre is tallest (16 px), falling to 0.22 r at the edges, with clear gaps between the cones.

**Layout:** slots 1–7.
- env 1 / 0.9 / 0.75 / 0.6 for |k−4| = 0..3.
- H = 0.38 r. The centre spike is 7 wide; the others are 5 wide.
- Thresholds: `F_on = 0.30 + 0.12·|k−4|`.
- Rising spring 3.2 Hz ζ 0.45 (about 20 % overshoot: a real pop). Falling 4.5 Hz ζ 0.38 with today's −0.05 r dimple.

| t (ms) | Field / body | Spikes | Eyes |
|---|---|---|---|
| 0 | F target ramps 0 → 0.95 linearly over 1600 ms. Mound `g = 0.12·F` | — | look along αH, pad 0.5. Pupils small |
| ~500 | — | centre pops | — |
| ~700 / 900 / 1100 | — | ±18°, then ±36°, then ±54° pop (centre-out nucleation, about 200 ms apart) | popS +0.03 at 1100 ("ooh") |
| 1600–2800 | Hold. Tremble `h·(1 + 0.06·sin(2π·2.2t + 1.3·|k−4|))`, phase shared by mirror pairs | full crown: 0.36 / 0.32 / 0.27 / 0.22 r | — |
| 2800 | F → 0 (F spring 6 Hz) | they drop outer-first (higher F_off), a ripple back in to the centre, with dimples | blink |
| 2850 | Mode 2 +0.08, mode 3 +0.03 at αH (the slump) | — | HAPPY 900 ms |
| 2850–4000 | wobble settles | — | — |

- **Symmetry:** mirror, odd count on the axis.
- **Variant (30 %), "nine":** slots 0–8, with slots 0 and 8 in the shoulder at 0.4× height.
- **Music:** F rides `beatSm`, and nucleation advances one ring per beat.

### 6. Carousel (M)

**What you see:** a ring of spikes marches around his crown, leaning as it goes, as if a magnet were circling under the dish. It reverses once, then stops.

**Silhouette:** about 4 spikes visible, 36° apart. They rise as they enter the show arc, are tallest on the axis and sink as they leave, all tilted the same way.

**Mechanism (§5.5):** light every other slot (36° spacing). Rotate the whole vertex grid by moving `al` at ω. The slot heights come from an envelope fixed in screen angle: `h_k = H·win(θ_k − αH)^0.7`, with `win` as in Shimmer and H = 0.32 r. Each time `al` advances one slot (18°), subtract 18° from `al` and shift the slot springs one index. The geometry is identical, so this is seamless and can run forever. Lean +14° toward the direction of travel.

| t (ms) | Field / motion | Eyes |
|---|---|---|
| 0–400 | F 0 → 0.8. ω ramps to +40°/s (1.6°/frame at 25 fps: under 1 vertex/frame) | look along αH |
| 400–3400 | Rotate at +40°/s (120°). A new spike enters every 0.9 s | slow pursuit: the gaze pans ±0.4 in the travel direction |
| 3400–3800 | ω → 0 → −50°/s. The lean flips over 200 ms. Mode 3 +0.03 at αH (the slosh) | blink on the reversal |
| 3800–5600 | Rotate at −50°/s | pan back |
| 5600–6200 | ω → 0, F → 0 (the spikes sink where they are). `al` springs back to αH | HAPPY 600 ms |

- **Symmetry:** rotational (equal spacing), the one sanctioned asymmetry.
- **Bulge:** `bulgeA` stays on αH, not `al`, while this runs (today the bulge follows `al` when F > 0.05).
- **Music:** steps 18° per beat (a ratchet) instead of a smooth ω.

### 7. Peak Split (M)

**What you see:** one tall peak rises on his crown, splits into two, then into three smaller ones, then melts. This is the classic split of a ferrofluid peak as the field weakens.

**Silhouette:**
- First: one broad cone 20 px tall, with the body pulled slightly tall toward it.
- Then: a fork of two peaks 18° apart. The pair stays ≤ 0.32 r so it reads as peaks, not horns.
- Then: three short cones.

**Layout (custom offsets, §5.1), 6 slots:**
- A: offset 0, 7 wide.
- B: offsets −3 and +3 (±9°), 5 wide.
- C: offsets −6, 0 and +6 (±18°), 5 wide.

Heights add, so a cross-fade from A to B shows the peak broadening and forking. No finer outline is needed.

| t (ms) | Spikes | Body | Eyes |
|---|---|---|---|
| 0–600 | A → 0.46 r (rising spring 2.4 Hz ζ 0.55) | mound g 0.10. Mode 2 −0.05 (tall) | look up the axis, pad 0.6 |
| 600–1100 | hold, tremble ±4 % | — | — |
| 1100–1450 | A → 0.14 r while B → 0.32 r (each pair the same) | mode 3 +0.03 at αH (shudder) | SURPRISED 500 ms, popS +0.02 |
| 1450–2000 | hold B | — | — |
| 2000–2350 | B → 0, A → 0. C → 0.22 r (centre 0.18) | mode 3 +0.02 | — |
| 2350–2900 | hold C | — | — |
| 2900 | all drop (falling spring) | mode 2 +0.06 | HAPPY 800 ms |

- **Symmetry:** mirror, 1 → 2 → 3.
- **Forked-tip rule:** AD bans forked tips on reachers. These are field spikes. They separate at the base (≥ 7 px apart at the tips), so they read as two peaks, not a claw.

### 8. Bloom (M)

**What you see:** two plump tentacles open in a mirror pair like petals, with a small spike standing between them. They boop together and fold back in.

**Silhouette:** a "V" of two round-tipped arms, 37 px each, 64° apart, curled gently outward, with a 10 px cone on the axis between them. It reads as a flower or a drop with arms raised, not a hand: only 2 arms, and the spike is clearly a different kind of thing.

**Tentacles:**
- kind `PT_SEQ`, mirrored pair (§5.6), `dir = αH ± 32°`, L 0.85 r.
- curl +0.45 rad (outward), grow 1.3 Hz ζ 0.55, both spawned in the same frame.
- Sway 0.12 rad at 0.4 Hz, mirrored, same phase. Boops 2.

| t (ms) | Tentacles | Spikes / body | Eyes |
|---|---|---|---|
| 0 | — | mode 2 −0.04 (gather) | look along αH, pad 0.4. popS +0.02 |
| 100 | the pair spawns | launch kicks (2 × +0.06, along each root) | — |
| ~450 | they arrive (λ > 0.9) | — | — |
| 500 | — | slot 4 pops to 0.24 r (the stamen) | HAPPY |
| 700, 1300 | both bulbs boop in unison (+0.4) | — | — |
| 1300–2300 | hold, mirrored sway | stamen trembles ±5 % | — |
| 2300 | both retract (2.4 Hz), curl → 2.2 outward (petals closing) | stamen drops at 2400 | — |
| ~2700 | absorbed | mode 2 +0.08 | blink, HAPPY 900 ms |

**Variants:**
- **Triple (25 %):** a centre tentacle 0.55 r with curl 0, spawned 150 ms after the pair, in place of the spike. It is shorter than the pair by rule.
- **Arch (25 %):** curl −0.6 rad (inward, a C facing the axis), L 0.95 r. The tips head toward a point 1.7 r out on the axis: two field lines between poles. Guard: if the tip gap drops below 12 px, both λ targets drop to 0.9 (the tips never touch).

### 9. Pearl Chain (M)

**What you see:** a string of beads draws out of his side along a curved field line, hangs there swaying, then the beads roll home one by one.

**Silhouette:** 3 black beads, shrinking outward (5.5 → 4.8 → 4.0 px radius), on a gentle C that curves toward the axis. Show is visible between them (gaps ≥ 7 px, which keeps the AD one-tip-width rule). It is a chain, not a tentacle.

**Path:** the root is `θr = αH ± 20°` (random side), with pose `ψ(s) = θr + Φ·s²` and `Φ = −0.5 rad·side`.
- Beads are held at arc distances 0.32 / 0.72 / 1.10 r past the rim (14 / 32 / 48 px).
- At L, add a 4th bead at 1.45 r.
- Held droplets are new (§5.7).

| t (ms) | Beads | Body | Eyes |
|---|---|---|---|
| 0 | — | droplet-neck mound at θr | look along the arc to its far end |
| 150 / 450 / 750 | beads spawn at the rim (far slot first) and slide out to their slots (held spring 1.8 Hz ζ 0.7). Each one leaves through the existing bridge, which looks like a pinch | mode 3 +0.02 at θr on each pinch | — |
| 1100–2600 | hold. Φ sways ±0.12 rad at 0.4 Hz, so the chain swings as one | — | gaze drifts along the chain |
| 2600 / 2850 / 3100 | nearest bead first: switch to return (phase 1, 1.4 Hz) → bridge → absorb | mode 3 +0.04 at θr on each absorb (existing) | follow each bead home |
| ~3800 | done | — | blink, HAPPY 600 ms |

- **Symmetry:** one chain on one side, a C curving toward the axis. 40 % of the time a mirrored chain on the other side, 2 beads each, if the pool allows (6 drops).
- **Needs:** 3 free droplet slots, so it is skipped while a shake splash is out.

### 10. Magnet Approach (B, hero)

**What you see:** an invisible magnet comes in from the show. His crown leans toward it and two tentacles curve out along field lines to meet it. Then it is snatched away: everything snaps back with a splat, and one droplet is left clinging where the magnet was.

**Silhouette:** at the peak, a fan of 7 spikes converging on a point 1.9 r out on the axis (outer ones leaning in up to 25°). Two arms arc in from ±50° toward that point, tips 12+ px apart. The body is stretched toward it. Bilateral, very "iron filings".

**Pieces:**
- **Virtual magnet M** on the axis. Its distance from the body centre d(t) eases out from 3.0 r to 1.9 r over 0–1500 ms.
- **Field strength** `F = 0.95·clamp((3.2r − d)/1.2r, 0, 1)`.
- **Spikes:** slots 1–7, H 0.50 r, env as in Rosensweig, the centre 3 are 7 wide. Thresholds `0.30 + 0.10·|k−4|`. Lean `λ_k = clamp(0.6·wrap(angle(M − base_k) − θ_k), ±25°)`.
- **Tentacles:** a field-line pair (§5.6) rooted at αH ± 50°. Curl `Φ = clamp(3·wrap(β − θr), ±1.2 rad)`, with β the chord angle to the stop point (M ± 0.15 r across the axis). L ≈ 1.2 r. Grow 1.2 Hz ζ 0.6.

| t (ms) | Field / spikes | Tentacles / drops | Body | Eyes |
|---|---|---|---|---|
| 0–900 | M at 3.0 → 2.3 r. Mound, then centre-out pops, leaning on M | — | bulge `g` toward M → 0.12 | look at M, pad 0.4 → 0.8 ("leaning in"). Pupils small |
| 900–1500 | M → 1.9 r. Full fan | field-line pair grows, slow and drawn | mode 2 −0.06 along αH (stretch toward M) at 300 and 900 | — |
| 1500–2700 | Hold. Quiver 2.2 Hz ±6 % (mirror phase) | mirrored sway 0.08 rad | — | — |
| 2700 | **Snatch:** F → 0 (6 Hz). The spikes snap with dimples | the centre spike flings a drop toward M (160 px/s), which is **held** at M for 500 ms. Both tentacles retract fast | mode 2 +0.22, mode 4 +0.06, E +0.3 | forced blink → SURPRISED 600 ms |
| 2700–4200 | — | the drop flows home about 3200–3800 and is absorbed | 3–4 bounces | HAPPY 1 s |
| 4200 | — | — | → Calm Dome (hold 1 s) | — |

**Rules:** ≤ 2 tentacles; the fan plus reachers is allowed (as in the beat grab). Tentacle area is about 0.25 × body, under the 0.6 cap.

### 11. Grand Crown (B)

**What you see:** the field slams on. Nine tall spikes burst across his whole crown at once, two droplets fly off the sides in a mirror pair, then it all drops with a big jelly splat.

**Silhouette:** the full Rosensweig hedgehog on the show arc: 24 px centre cones, shoulder cones at 0.4×, everything symmetric. It lasts under a second, so it reads as a burst, not a sea urchin.

**Layout:** slots 0–8, H 0.55 r.
- env 0.4 / 0.7 / 0.85 / 0.95 / 1 / … (slots 0 and 8 sit in the shoulder).
- 7 wide where h > 0.40 r.
- Thresholds `0.20 + 0.06·|k−4|`.
- Rising spring 4 Hz ζ 0.4.

| t (ms) | Field / spikes | Drops | Body | Eyes |
|---|---|---|---|---|
| 0–150 | — | — | mode 2 −0.10 (crouch, anticipation) | forced blink |
| 150 | F steps to 1 (6 Hz ζ 0.7). The nucleation sweep runs centre-out in about 80 ms | — | — | SURPRISED, popS +0.04 |
| 200 | — | — | mode 2 +0.16. E 0.6 | — |
| 300 | slots 2 and 6 drop to 0.3 h for 150 ms (the tips let go), then regrow | a mirror pair is flung from those tips along their axes, 180 px/s, r 0.09 r | — | — |
| 300–1000 | hold, tremble 2.5 Hz ±5 % (mirror phase) | the drops bounce off the glass | — | — |
| 1000 | F → 0. Snap back with dimples | they return about 1200–1900 with bridges | mode 2 +0.20, mode 4 +0.05, 3–4 bounces | HAPPY 1.2 s |
| ~2400 | — | absorb kicks (mode 3) | → Calm Dome (hold 1 s) | — |

### 12. Long Reach (B, existing)

**What you see:** one tentacle stretches far into the show toward the brightest spot. This is `feeler(now, true)`: 1.9–2.4 r, boops, a slow drain.

It is unchanged except that it now arrives through the scheduler (weight 3, ≥ 45 s apart) instead of as 60 % of every feeler group. It is the one item that crosses the screen centre on its own.

---

## 4. Scheduler

### 4.1 Picking

The scheduler runs only when he is free (no run, morph, poke, tap or shake in the last 4 s, and no finger down). The existing `nextFeeler` timer is retired and the feeler group becomes the "Feelers" entry.

1. **Rest.** After a sequence ends, rest before the next pick: small 2–4 s, medium 3–6 s, big 6–10 s (the Calm Dome counts as part of the big). During a rest only breath, waves, eyes, wander, fidgets and the 1.69 pulse run.
2. **Pick a tier.** Base odds: small 0.58, medium 0.36, big 0.06. Ladder modifiers:
   - after 2 smalls in a row, medium ×1.6;
   - big only if a medium ran in the last 30 s and no big ran in the last 60 s (build-up, then release);
   - after a big, the next two picks are small (the forced Calm Dome counts as the first).
3. **Pick an entry** in that tier by weight (§3.1). An entry is eligible only if:
   - it is not the last entry,
   - its cool-down is over,
   - the tier budget allows it (below),
   - its resources are free (tentacle slots under `partyTentCap`, droplet slots),
   - and it is not the third spike-family item in a row. The spike family is entries 1, 3, 5, 6, 7 and 11.
4. **Bright-spot bias.** If `haveBright`, Long Reach and Pearl Chain get weight ×1.5 and aim at it (the chain's side is the one nearer the spot).
5. **Budget per rolling 60 s:**
   - big ≤ 1;
   - medium ≤ 4;
   - tentacle-using items (Bloom, Feelers, Magnet Approach, Long Reach, plus flicks) ≤ 3.

   This keeps AD bench criterion 8 (calm idle) honest: spike sequences don't count as reaches.

**Expected minute:** about 40 % rest, 35 % small, 20 % medium, ≤ 5 % big, which is about 4 smalls, 2–3 mediums and a big every 1–2 minutes.

### 4.2 Lifecycle

- `startSeq(id)` latches αH and the side.
  - It claims what it needs: spike owner, tentacle slots (kind `PT_SEQ`, so `spawnTent` never steals them), droplet slots, and the wander freeze for M and B.
  - It sets `seqT0`.
- `stepSeq(now, dt)` runs each frame, right after `runEvents`. It writes targets as analytic functions of `t = now − seqT0`: F, per-slot `hT` and lean, hills A, κ, M. Tentacles and drops spawn on stage edges. It uses `later` only for one-shots (boops, blinks), so the 20-slot event queue doesn't fill.
- `endSeq()` releases everything gently.
- `abortSeq(fast)` is called by a poke, tap, shake, roam start, exit, or hero request:
  - spike targets → 0 on the falling spring;
  - `PT_SEQ` tentacles `retract(fast)`;
  - held drops → return phase;
  - κ and hills spring back;
  - then the rest timer restarts at 4 s.
- **Hard cap:** 7 s per sequence. Beyond that, `endSeq`.

### 4.3 Chaining with the existing behaviours

| Existing | With the library |
|---|---|
| Feeler groups (5–10 s timer) | Retired as a timer. Becomes the "Feelers" entry (short reaches only, 0.8–1.3 r; see question 1) |
| 1.69 self-pulse (5–12 s) | Keeps its timer. **With no sequence running:** as today, but the flick chance drops from 0.5 to 0.25. **While a sequence owns the spikes:** a *throb* instead: +0.06 r on every live slot target for 250 ms, plus the lub-dub mode-2 kicks at half strength. No fan, no flick |
| Fidgets (shiver, scoot) | Only during rests. A scoot during a small sequence is fine; during M or B, skip it |
| Roam (25–60 s) | When due: wait for the current sequence (≤ 6 s), then 50 % a Calm Dome (hold 0.8 s) as the inhale, then the roam. No pick within 3 s of a due roam, and none for 3 s after landing (the landing burst owns the spikes) |
| Tap, poke, shake | Abort the sequence (§4.2). The event runs as today |
| Beat on CoreS3 | **Music set:** only entries 1, 3, 5, 6 and 11, beat-locked as noted in each entry. Tentacles belong to `beatGrab`, so Bloom, Feelers, Magnet Approach and Long Reach are off while `heard`. While a crown owns the spikes, the beat does a throb (as the pulse does) instead of `field()`. Grand Crown only on bass > 0.85, ≤ 1 per 30 s |
| `/bot/party?hero=` | Add `?seq=1..12` to play one on demand (aborting any running sequence), for tuning on the device |

### 4.4 Hand-over rules for the spike slots

There is one owner token, `spkOwner` ∈ {NONE, EVENT, SEQ, SHAKE}.

1. **Priority:** SHAKE > EVENT (tap, landing, enter, exit) > SEQ > EVENT (pulse, beat). The lower-priority pulse and beat become throbs instead of taking over.
2. **Layout changes only when drained.** All slot heights must be under 1 px, or the slots are force-drained at 6 Hz for at most 200 ms. Every crown here uses the standard 18° grid except Peak Split. So a tap during a crown can simply retarget the same slots (the fan converges on the finger), with no visible jump.
3. **Two crowns never touch.** Between two spike-family sequences there are ≥ 1.5 s with no spikes up (the rest covers this). The same layout is never re-armed while its springs are still falling.
4. **`al` and `bulgeA`** belong to the owner. When a sequence ends, `alT` returns to αH and the bulge returns to the gaze.
5. **Tentacles:** a sequence's tentacles are `PT_SEQ`. `spawnTent`'s reuse rule (oldest feeler or streamer) never takes them. Feelers don't spawn while a tentacle sequence is running.

---

## 5. New primitives

All of these are per frame on the 1.69. "Cost" is CPU on top of today. Pixel cost (rim RMW plus fill) is folded into the per-sequence numbers in §3.1.

### 5.1 Spike slots (the generalised fan)

```
struct PartySlot { int8_t off; uint8_t wide; float hT, lean, Fon, Foff; bool popped; BotSpring h; };
PartySlot slot[12]; uint8_t nSlot;   // default layout: 9 slots, off = (k-4)*6, wide = 0
```

- **Default layout:** the event fan. Its target is today's formula (`F·0.55R·gauss·jit`), so the shipped behaviour is unchanged.
- **Sequences:** may install another layout (offsets in vertices relative to vertex 0 = `al`, width 5 or 7) and write `hT` directly.
- **LUTs:**
  - `LUT5 = {1, .41, .09}` (today's);
  - `LUT7 = {1, .53, .22, .05}`, from `(1 − |d|/4)^2.2`.
- **Plugs in:**
  - `step()`: replace the spike loop's target line with `slot[k].hT` (the default layout keeps the Gaussian).
  - `buildOutline()`: the fan loop iterates over `slot[]`, using `off` and the LUT.
  - Raise `PARTY_SPIKES` from 9 to 12.
- **Cost:** ≤ 12 × 7 adds, no trig: about 2 µs. Memory +200 B.

### 5.2 Field threshold with hysteresis (Rosensweig pop)

`popped_k = F ≥ Fon_k || (popped_k && F > Foff_k)`, `hT_k = popped_k ? F·H·env_k : 0`, with the mound `g = max(gG, 0.12·F)` on `al`.

- Thresholds that rise with |k−4| give centre-out nucleation and outer-first collapse for free.
- **Plugs in:** the per-slot target in `step()`.
- **Cost:** about 0.

### 5.3 Hills term (Shimmer)

Per vertex i, with x = θ_i − αH:
- `cx = c·cH + s·sH` (cos x, from the vertex's own c and s);
- `win = max(0, (cx − 0.4226)/0.5774)`;
- `C = cos(n·x)` by rotation recurrence: start at `cos/sin(n·(a0 − αH))`, step by `cos/sin(n·3°)`, exactly as the edge waves do;
- `r += A·win²·(m·max(0,C)² + (1−m)·max(0,−C)²)`.

**Plugs in:**
- `prepShape()`: A, m, n and the start and step constants.
- `buildOutline()`: in the per-vertex loop, after `radiusCore`, before mounds. Skip it when A < 0.2 px.

**Cost:** about 12 flops × 120 vertices: about 8 µs.

### 5.4 Lean (tangential offset)

Add `float partyTT[PARTY_N]`, zeroed each frame (480 B memset).
- A slot of height h and lean λ adds, per vertex d of its LUT:
  - `RR += h·LUT[d]·(cos λ)`;
  - `TT += h·LUT[d]·sin λ`.
- In `placeOutline()`: `VX = cx + (RR+grow)·c − TT·s`, `VY = cy + (RR+grow)·s + TT·c`.

The fill is non-zero winding, so a leaned tip that passes its neighbour vertices still fills correctly. The grown rim is radial, so it thins by `cos λ` on the overhang side (at most 10 % at 25°). That is acceptable; recheck it in the lab on galaxy.

**Cost:** 240 FMAs for 2 `placeOutline` calls, plus the slots: about 5 µs. Magnet Approach adds about 9 `atan2f` for the lean toward M: about 3 µs.

### 5.5 Rotating grid (Carousel)

- Drive `al.x = al0 + ω·t` directly (not through the spring) while the Carousel runs.
- When `al.x − al0 ≥ 18°`:
  - subtract 18° from `al0`;
  - shift `slot[k]` one index (the heights, springs and popped flags move with their screen angles);
  - zero the slot that comes in.
- The edge waves and modes are already evaluated in absolute angle (`ws`/`mcs` start at `harm·a0`), so the body does not rotate with the grid.
- The only artefact is that the 35° floor step jitters by up to 3°, which already happens whenever `al` moves today.

**Cost:** about 0.

### 5.6 Mirror pair and field-line tentacles

**Mirror pair:** `spawnPair(opt, β)` spawns two tentacles in the same frame:
- `dir = αH ± β`, same L, springs and `swPh`;
- `mirror = true`, which makes `stepTent` use `swA·side` in ψ. Sway is not mirrored today, so two "symmetric" tentacles sway the same way and the symmetry breaks.
- kind `PT_SEQ`.

**Field line:** an untracked tentacle with `dir = θr` (leaving along the normal) and `curl = Φ·side`, where `Φ = clamp(3·wrap(β − θr), −1.2, 1.2)`.
- The pose `ψ(s) = θr + Φs²` has a mean tangent of `θr + Φ/3`, so the chord points at the target.
- 1.2 rad stays inside AD's 70° bend limit.
- L = chord length − stop distance (arc ÷ chord ≤ 1.06 at these curls, so this is close enough).

**Plugs in:** `PartyTentOpt` (+ `mirror`), `stepTent`'s ψ line, `fire()`/`spawnTent`. No new geometry.

**Cost:** that of the tentacles themselves (MS §1.3).

### 5.7 Held droplets and the virtual magnet

**Held drop:** `PartyDrop` phase 3 = held at (hx, hy) on the `rx`/`ry` springs (1.8 Hz ζ 0.7).
- The sequence rewrites the target each frame (beads on the swaying arc, the drop at M).
- The bridge to the rim works as today while `gap < 0.3 r`.
- On release, phase 1 (return) runs as today.

**Virtual magnet M:** a point owned by the sequence. It feeds the lean (§5.4), `bulgeA`, the tentacle curls and the held-drop target. It is never drawn.

**Spike fling:** `spawnDrop` at slot k's tip point, along its leaned direction. On the same frame, snap that slot's spring to 0.3·h, so the tip visibly lets go.

**Cost:** as droplets (about 0.03 ms each with the rim). The magnet is about 0.

### 5.8 Calm gain κ

`BotSpring kappa` (default 1).
- In `prepShape`: `A = (1 + 2E)·R·κ·…`.
- During the Calm Dome, the mode-spring ζ is doubled and restored at the end, and E's target is multiplied by κ.

**Cost:** 0.

### 5.9 Sequence runner and owner token

- `struct PartySeq { uint8_t id, stage, side; unsigned long t0; float axis, a, b; }`, plus `spkOwner`, `lastSeq`, `nextOk[12]`, and a 60 s ring of start times per tier (3 × 4 bytes).
- `stepSeq()` is a switch on `id` holding closed-form envelopes.
- **Cost:** about 0.01 ms.
- **Memory:** about 150 B.

**Total budget.**
- Small sequences add about 0.03 ms.
- Spike mediums add about 0.06 ms.
- Tentacle mediums add as much as today's feeler group.
- The worst big (Magnet Approach) adds about 0.6 ms. With the scheduler banning overlap with feelers and flicks, it stays inside today's 9 ms worst case.

---

## 6. Top picks for the lab

1. **Spike slots + thresholds → Rosensweig Crown.** This is the most recognisable ferrofluid image and the base for six entries. If it reads at 20 fps on plasma and on galaxy, the library works. About 2 h in `lab.part` (the `buildBody` spike loop already mirrors the firmware).
2. **Breathing Crown.** Same code, small tier, and it will fill the most screen time. It proves the "small and alive, not canned" claim.
3. **Calm Dome.** About 20 lines. It provides the contrast that makes every other entry read as an event, and it is the scheduler's closer.
4. **Bloom.** Kevin asked for "medium" tentacle moments. This is the symmetric one, and it forces the mirrored-sway fix that every pair needs.
5. **Carousel.** The rotating magnet. It is a different kind of motion (travel, not pulse), it is nearly free through the `al` rotation, and it is the riskiest for strobing, so it is worth checking early at 15 / 20 / 25 fps.

Then build Magnet Approach on top of 1 and 4 (adding lean and the held drop) as the new hero.

**Lab additions:**
- a "Library" button row (one per entry);
- an "Auto" toggle that runs §4;
- a 60 s strip that colours time by tier (rest, S, M, B), so Kevin can judge the mix as well as each move.

---

## 7. Open questions for Kevin

1. **Your 60 % big reaches.** With Long Reach and Magnet Approach in the library, can idle feelers drop to short reaches only (0.8–1.3 r)? Big reaches would then come about once every 1–2 minutes through the scheduler, instead of in 60 % of feeler groups.
2. **Tempo.** Is about 40 % stillness right, with a sequence starting every 6–10 s: roughly 4 small and 2–3 medium a minute, and a big every 1–2 minutes? Or do you want it busier?
3. **CoreS3 with music.** Should the crowns lock to the beat (breathing, stadium, carousel ratchet) and replace the generic fan pump, with the tentacle sequences paused while `beatGrab` drums?
