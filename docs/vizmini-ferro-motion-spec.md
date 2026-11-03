# vizMini ferro mode: motion spec

Status: proposal for Kevin. No firmware written. Scope: how the black ferrofluid blob moves on
vizMini's white 128x64 1-bit screen, when it moves, how he gets in and out of ferro mode, and
what it costs on the ESP32-C3. The look (silhouette, edge, glint, white-screen power and wear)
is the art director's separate spec; the few places motion depends on it are marked **[look]**.

Sources: `docs/plan-party-mode.md`, `docs/party-mode-motion-spec.md` (**R1**),
`docs/party2-motion-spec.md` (**R2**, tuned values §6a), `docs/party2-pattern-library.md` (**PL**),
`vizbot/party_blob.h` (shipped party mode), `vizmini/PLAN-v2.md`, `vizmini/face_motion.h`,
`vizmini/vizmini.ino`, `vizmini/config.h`.

Units: `r` = body radius (M = 15 px). Angles in screen space, y down, so "up" is −90°.
`αH` = the stance axis: the direction pointing away from the surface he sits on (floor: −90°).
All motion runs on springs stepped in real time (R1's `BotSpring` maths, fixed 1/128 s sub-steps).
Kicks are peak-normalised as in R1 ("kick 0.1" peaks at 0.1).

---

## 0. The calls in one table

| # | Call |
|---|---|
| 1 | **He sits on a surface, not in a corner.** Default stance: a sessile drop on the floor (bottom edge), r 15 px, free to crawl the full 88 px of floor. He also climbs the walls and hangs from the ceiling. Every pattern is written about `αH`, so party patterns port as-is and point "up" on the floor, "down" from the ceiling. |
| 2 | **No eyes in ferro mode.** The eyes appear only during the enter/exit morph, where the face's black background *is* the ferrofluid and swallows them. Mood reads through temperament (which patterns, how springy) and the LED strip. |
| 3 | **Medium leads, big is frequent, stacking is on by default.** About 25 % rest (party: 40 %), a big move every ~25 s, two patterns stacked about twice a minute. |
| 4 | **New wide-screen vocabulary:** Crawl, Taffy (edge-to-edge stretch), Twins (split into two and rejoin), Climb, Hang + Drip, Tarzan (swing across on a tentacle), Lap (around the screen edges), Magnet Sweep, Hop Train, Rain, Lava Lamp, Peekaboo. |
| 5 | **The rotation engine is off in ferro mode** (`apply()` never runs). Its moves and web buttons are re-routed to physical equivalents: spin = Lap, flip = Hang, pendulum = Tarzan, tilt = lean, wobble = jiggle. The motion level (off/rare/often) still sets how often those stance moves happen. |
| 6 | **Everything per-vertex, per-node and per-pixel is fixed point with a 256-entry sin table.** Estimated 0.5 ms typical, 1.2 ms worst (about 2.5 ms with a 2x safety margin) against the 8 ms budget. The same scale written in floats like `party_blob.h` would cost about 7–9 ms. |

---

## 1. Scale and placement

### 1.1 Body size

| Size (web `?size=`) | r | Rest width × height (J0 0.12) | Screen area |
|---|---|---|---|
| S | 12 px | 27 × 21 | 5.5 % |
| **M (default)** | **15 px** | **34 × 26** | **8.6 %** (party M is 9 % of the 1.69) |
| L | 18 px | 40 × 32 | 12 % |

Why 15: it is the largest drop that leaves 37 px of air above it on the floor for spikes and
reaches, and a 34 px body still crawls a useful 88 px. Spike and tentacle sizes below scale with
`r`, so S and L keep the same proportions.

### 1.2 Stances (where he sits)

He always sits on one surface. The surface sets `αH`, the clamp line, and the rest squash.

| Stance | Surface (clamp line) | `αH` | Centre at rest | Centre range | Share of time |
|---|---|---|---|---|---|
| **Floor** (default) | y = 62 | −90° (up) | y = 62 − r(1−J0) + 2 = **50.8** | x 20–108 | ~75 % |
| Ceiling (hang) | y = 1 | +90° (down) | y = 12.2 | x 20–108 | ~10 % |
| Left / right wall | x = 1 / x = 126 | 0° / 180° | x = 12.2 / 114.8 | y 17–47 | ~10 % |
| Float (mid-air) | none | −90° | (64, 32) | ±10 px | ~5 %, only inside Lava Lamp, Twins, Tarzan |

- **Planted contact.** The centre sits 2 px "into" the surface and any vertex past the clamp line
  is moved onto it. That gives a flat, wetted contact patch about 14 px wide that breathes and
  wobbles with the body. The 1–2 px white gap between the patch and the bezel stops it reading as
  cropped. **[look]** whether a floor line is drawn; this spec assumes not.
- **Rest squash J0** (mode 2 along the surface): floor 0.12, wall 0.12, ceiling 0.04 plus a 0.10
  bulge toward `αH` (a pendant drop: flat on top, round belly hanging), float 0.05.
- **Show arc.** Spikes and reaches go within ±70° of `αH`. Shoulder 70–95°: spikes ≤ 0.30 r,
  no tentacle roots (except Taffy and Tarzan, which root at the shoulder on purpose). Past 95°
  is the surface: nothing.
- **Free roaming, not a home.** On the floor he crawls to a new x every 4–9 s (§2.1). He has no
  fixed home; x 64 is only where the enter morph lands him.

### 1.3 Pixel rules for 1 bit at 24 fps

| Rule | Value |
|---|---|
| Smallest persistent amplitude | 1.5 px, or off (R1 pitfall 1: sub-pixel terms boil) |
| Smallest spike | 3 px tall, or treat as 0 |
| Smallest round tip | bulb radius 3 px (a 2 px disc reads as a square) |
| Slow rim travel | ≤ 0.75 px/frame (18 px/s) |
| Sharp-feature travel | ≤ 1 vertex/frame (5.6°/frame = 135°/s); Carousel uses ≤ 60°/s |
| Fastest surface mode | 3.8 Hz (no aliasing at 24 fps) |
| Spike spacing | ≥ 3 vertices (16.9°) between centres; peaks ≥ 7 px apart at the tips |
| Moving fast | anything faster than 6 px/frame stretches along its velocity (smear) instead of jumping |

---

## 2. Motion vocabulary

### 2.1 Base layer (always on, under every pattern)

| Term | vizMini value | Party value |
|---|---|---|
| Breath B | ±0.08 r (1.2 px) at 0.21 Hz. The bottom is planted, so the top rises ±2.4 px | ±0.05 r |
| Edge wave n = 3 | 0.12 r (1.8 px), +0.13 Hz, amplitude drift 0.7 ± 0.3 at 0.050 Hz | 0.065 r |
| Edge wave n = 4 | 0.10 r (1.5 px), −0.19 Hz, drift 0.7 ± 0.3 at 0.071 Hz | 0.045 r |
| Edge wave n = 5 | **dropped** (0.4 px at r 15, pure boil) | 0.027 r |
| Surface modes n = 2 / 3 / 4 | 2.0 / 3.0 / 3.8 Hz, ζ 0.22 / 0.25 / 0.28, clamps ±0.35 / ±0.16 / ±0.10 | same f, ζ; n = 2 clamp was ±0.28 |
| Kick gain | ×1.7 (Kevin's Jiggle). Mode 3 and 4 kicks are floored at 0.07 (1 px) | ×1.7 |
| Mode n = 5 | dropped (same reason as the edge wave) | 4.5 Hz |
| Agitation E | 1.2 Hz ζ 1.0, wobble gain A = 1 + 2E | same |
| Settle twitch | 0.05 on mode 2 or 3, every **2–5 s** | every 4–9 s |
| Pulse ("lub-dub") | mode 2 +0.10, then +0.06 after 180 ms, every **4–8 s**. With no pattern up: a spike fan F 0.5 at `αH` ± 30°. With a pattern up: a throb (+0.06 r on every live slot for 250 ms) | 5–12 s |
| Hop lift L | 1.9 Hz ζ 0.42 (undershoot = the landing splat, R1) | same |
| Velocity stretch | mode 2 along travel, 0.20·clamp(\|v\| / 2.5 r/s), axis frozen below 0.2 r/s | 0.18 |
| Lean | bulge 0.12 toward the direction of travel | — |
| **Crawl** (replaces corner wander and roam) | New x every 4–9 s. Under 12 px: glide (x spring 0.55 Hz ζ 0.75). 12–40 px: 50 % glide, 50 % scoot hop (lift 0.25 r = 3.8 px). Over 40 px: crawl latch (R2 roam: a tentacle reaches 1.6 r along the floor, plants, he flows to it on Q 0.95 Hz ζ 0.9; two latches if over 45 px) | corner box ±13 px, roam 25–60 s |
| Filings (P2, replaces snow when snow is on) | 10 black specks drift down at 4–8 px/s. Within 12 px of the rim they accelerate toward it (spring 2 Hz ζ 0.7) and vanish into a mode 3 kick of 0.04 at that angle | — |

### 2.2 Party patterns that survive (adapted)

Spike heights in vizMini are about 1.45× party's fractions of `r`, because 0.38 r on a 15 px
drop is only 6 px and Kevin found small patterns get lost.

| Tier | Spike height cap | Spikes | Tentacles | How far out |
|---|---|---|---|---|
| S | 0.30 r (4.5 px) | ≤ 5 | 0 | ≤ 0.4 r past the rim |
| M | 0.65 r (10 px) | ≤ 7 | ≤ 3, each ≤ 2.2 r (33 px) | to mid-screen |
| B | 0.90 r (13.5 px) | ≤ 9 | ≤ 2 big (≤ 60 px) | edge to edge |

Spike widths: 5-wide (`{1, .41, .09}`, ±11°) by default, 7-wide (`{1, .53, .22, .05}`, ±17°) when
taller than 0.5 r. Slot k sits at vertex offset (k − 4)·3 from `αH`, so 9 slots span ±67.5°.

| Party pattern | vizMini | Changes for 128x64 |
|---|---|---|
| Breathing Crown | **S** | 5 spikes, H 0.45 r centre (6.8 px), 3 breaths over 6 s. Thresholds as PL. |
| Calm Dome | S, solo | The palate cleanser. Hold **0.8–1.5 s** (party 1.5–2.5 s): vizMini should never look switched off. |
| Rosensweig Crown | **M** | 7 spikes centre-out, centre 0.65 r (10 px), outer 0.35 r. "Nine" variant 30 %: shoulders at 0.4×. |
| Carousel | M | On the floor the ring rises at the left shoulder and sinks at the right: a magnet circling under the dish. Spikes 6 vertices (34°) apart, ω 40°/s then −50°/s (1.7–2.1°/frame). |
| Peak Split | M | A 0.8 r (12 px) 7-wide peak → a pair at ±3 vertices (tips 7.4 px apart) → three at 0, ±5. |
| Stadium Wave | M | 7 slots, H 0.5 r, crest 90 ms per slot, there and back. |
| Spike Merge | M | 9 small (0.25 r) → 5 → 3 tall (0.7 r). Uses the full ±67° arc. |
| Bloom | M | Mirrored pair at `αH` ± 35°, L 1.6 r (24 px), stamen 0.45 r. Triple and Arch variants as PL. |
| Feelers | M | 1–2 short reaches, 0.8–1.4 r. No bright spot to sample: they aim at "interesting" points: the nearer wall, a ceiling corner, a live droplet or filing. |
| Long Reach → **Wall Reach** | B | One tentacle 2.2–4 r (33–60 px) to a wall or the ceiling. The bulb **touches the glass**: it flattens to 0.75 along the edge normal and widens to 1.25, boops twice, then drains back. |
| Grand Crown | B | 9 spikes, centre 0.9 r (13.5 px), shoulders 0.5×, two droplets flung in a mirror pair. |
| Pearl Chain (not built in party) | M | 3 beads, r 3.5 / 3 / 2.5 px, held on a sideways arc along the wide screen, gaps ≥ 4 px. Mirrored second chain 40 %. |
| Magnet Approach | B | The magnet comes **down from the ceiling** on the axis (from y 0 to 1.9 r above the rim). Spikes converge upward, two field-line arms arc up. Snatch, splat, one drop left hanging, then flows home. |
| Probe / Shiver / Scoot fidgets | S | Probe = a short feeler. Shiver uses n = 5 at 1.5 px for 600 ms (n = 7 can't resolve at r 15). Scoot is now part of Crawl. |
| Porcupine (shake) → **Bristle** | event | No IMU. 7 spikes at 0.45 r every 5 vertices (28°) across the upper half, 450 ms. Fired by ANGRY / SHOCKED mood changes and the web Boop. |
| Poke-and-run → **Dash** | B | The web **Boop** button: dent, flinch, then a dash to the far half of the floor (grab tentacle + 2 streamers, R2 timings, Q 1.5 Hz ζ 0.85), landing splat + droplets. |
| Shimmer | **dropped** | Its hills would be 1 px on r 15. |
| Tap reach, gaze reach, beat grab, code mode, voice crown, album art | dropped | No touch, no eyes, no mic. |

### 2.3 New patterns for a wide, short screen

Times in ms from the start. "LED" names a cue from §3.4.

**Taffy (B, ~4.5 s, edge to edge).** Floor stance; crawls to x 52–76 first if needed.

| t | Body | Tentacles | LED |
|---|---|---|---|
| 0–300 | Crouch: mode 2 +0.15 | — | dim to 60 % |
| 300 | Take-off kick mode 2 −0.10 | A mirrored pair roots at `αH` ± 80° (shoulders) and shoots **flat along the floor** to both walls (grab spring 3.0 Hz ζ 0.6, ~150 ms), L = distance to wall − 3 px (≈ 43 px each) | ends of the strip light |
| 450 | Pulled wide: mode 2 → +0.35 (clamp). Mass borrow 8 %: arms widen ×1.3 | Bulbs splat on the glass (flatten 0.75, widen 1.25). The whole shape spans ~120 px | — |
| 450–2500 | Trampoline: lift kicks 0.20 r at 900 and 1500 | Arms sag and sway 0.15 rad at 1.6 Hz, mirrored, same phase | flash per bounce |
| 1200 | Stacks a Rosensweig Crown on top (forced layer) | — | swell |
| 2500 | **Snap:** arms release and retract (3.5 Hz). Recoil kick mode 2 −0.25 (tall), E +0.6 | 50 %: each wall bulb pinches off a droplet that rolls home along the floor | white flash 120 ms |
| 2500–4500 | Mode 2 rings out (~1.5 s) | Droplets absorbed with mode 3 kicks | back to mood |

**Twins (B, ~7 s, split into two and rejoin).** Needs a second body (§5).

| t | Body | Patterns | LED |
|---|---|---|---|
| 0–600 | Peanut: mode 2 → +0.35 and mode 4 → −0.20 (the waist pinches to 0.45 R, lobes at ±9 px) | — | hue splits across the strip |
| 600 | **Split:** swap to two bodies, each r/√2 = 10.6 px, at the lobe centres, joined by a neck tube (half-width spring 4 px → 0, 6 Hz ζ 1, ~80 ms) | — | two spots |
| 600–1400 | They slide apart to x 36 and 92 (Q 1.2 Hz ζ 0.7) with a streamer each; each lands with a mode 2 splat +0.22 | — | spots move apart |
| 1400–5000 | Twin mirrored crowns: each body runs a Rosensweig Crown scaled to 10.6 px (centre 7 px), axes tilted 20° **toward each other**: two poles of one field | stack: each crown owns a bank | swell per crown |
| ~3000 | — | Field-line arch: each sends a 1.2 r arm curling toward the other; the tips stop ≥ 12 px apart (PL no-loop rule) | — |
| 5000–6200 | Rejoin: both rush to x 64 (1.6 Hz ζ 0.6). A bridge forms under 0.3 r gap; on contact, swap back to one body r 15 | — | spots converge |
| 6200 | Merge splat: mode 2 +0.30, mode 4 +0.06, E 0.6; 3 droplets jump up 10–20 px and fall back | → Calm Dome 1 s | spark |

**Climb (stance move, 2.5–4 s).** Floor → wall, wall → ceiling, or back down.
1. Crawl to the near end of the floor (x 20 or 108).
2. A latch tentacle reaches up the wall 1.6 r and plants.
3. `αH` springs to the wall's axis (1.2 Hz ζ 0.9). The centre follows a quarter circle of radius
   r(1 − J0) around the corner, and the drop flows *around* it. While `αH` is within 45° of the
   corner both clamps are on, so for a moment he sits squeezed into the corner.
4. Arrival: mode 2 +0.12. Hold 6–15 s on the wall, running wall-safe patterns (crowns pointing
   into the screen, feelers toward the centre), then climb on or slide back down.

**Hang + Drip (stance move, FLIP equivalent, 5–7 s).**

| t | What |
|---|---|
| 0–300 | Crouch, look-free anticipation: mode 2 +0.12 |
| 300 | **Rope:** a tentacle shoots straight up to the ceiling (≈ 37 px, 3.0 Hz) and latches |
| 500–1300 | Hauled up on the tentacle (Q 1.2 Hz ζ 0.7), stretched tall (S 0.2). `αH` turns through 180° in the move's direction (`dir`, like FLIP), so the crown ends up pointing down |
| 1300 | Arrives on the ceiling: mode 2 +0.15, J0 → 0.04, pendant bulge 0.10 |
| 1300–4000 | Hang (FLIP hold 2.2–3.8 s). **Drip:** at ~2000 a droplet stretches down from the belly (a 0.6 r tentacle with gravity), pinches off at 1.2 r, falls (gravity 300 px/s²), splats on the floor (squash 1.6× wide for 120 ms) and sits |
| ~4000 | Lets go: free fall 36 px in ~0.5 s, stretched along velocity. `αH` keeps turning the same way to −90° (2.5 Hz ζ 0.8), so he rights himself "the same way round" |
| landing | Big splat: mode 2 +0.35, crown burst F 0.9 for 300 ms, absorbs the waiting drip if within 0.3 r, 2 droplets bounce |

**Tarzan (stance move, PENDULUM equivalent, 5–6 s).** Wide-screen hero.
1. From x ≈ 30 (or the mirror), a tentacle shoots up-and-across to the ceiling at (64, 1),
   ~60 px (the length cap). Latch.
2. He lifts off: the tentacle shortens to ℓ 45 px and he becomes a pendulum bob on it. Swing
   angle spring: pulled to −40°, then target 0 at **0.9 Hz ζ 0.08** (FaceMotion's pendulum
   tuning). At the bottom of the arc his belly skims 3 px above the floor.
3. Body stretches along the swing (S up to 0.25); one streamer trails; mode 2 sloshes at each end.
4. After 1.5 swings (~1.7 s) he lets go at the far end (+35°, x ≈ 90): ballistic flight, the
   rope retracts with a curl in flight, landing splat at x ≈ 100 and a 5-spike landing crown.

**Lap (stance move, SPIN equivalent, ~4.5 s).** He crawls the whole screen edge: floor → right
wall → ceiling → left wall → floor, at 110 px/s on the straights (4.6 px/frame), corners as in
Climb, `αH` turning continuously (one full turn). Two streamers trail. SPIN2 runs two laps at
160 px/s and ends in the DIZZY temperament (§3.3) for 2.5 s.

**Magnet Sweep (M, ~5 s).** An invisible magnet slides under the floor: to x 110 (1.4 s),
to x 18 (2 s), back to his x (1.2 s).
- 5 spikes (H 0.55 r) lean toward it, lean = clamp(0.8 · angle to magnet, ±35°).
- Bulge 0.15 toward it. He chases on the x spring at 0.8 Hz ζ 0.6, so he lags and overshoots.
- When the magnet is over 30 px away the spikes lie down and one low feeler (1.6 r) reaches for it
  along the floor.

**Hop Train (M, ~3 s).** Three hops of 18–24 px in one direction, 900 ms apart.
- Each take-off: lift kick 0.6 r (9 px), mode 2 −0.15.
- Each landing: mode 2 +0.25 and a 5-spike crown F 0.7 for 150 ms ("sea urchin stomp").
- LED flash per landing.

**Rain (M, ~4.5 s).** 4–6 droplets (r 3 px) drip from the ceiling at random x within ±25 px of
him, 500–800 ms apart, falling with gravity 300 px/s².
- Before each one lands, the body bulges 0.12 toward it.
- Each impact: absorb, a 3-spike splash crown at the impact angle (F 0.6, 200 ms), mode 3 +0.05.
- The last drop is caught mid-air by a short feeler and pulled in.

**Lava Lamp (M, ~5.5 s).** Float stance.
1. He lifts off the floor and rises to centre y 24 on Q 0.5 Hz ζ 0.9 (~1.6 s), stretched tall
   (S 0.12), a soft streamer tail hanging below.
2. At the top a droplet (r 3.5) buds upward and drifts to the ceiling at 40 px/s, where it is held.
3. He sinks back (0.6 Hz). The bead lets go 600 ms later, falls, and is absorbed on landing.

**Peekaboo (S, ~4 s; SHY's signature).** Fast crawl (1.5 Hz) to the nearer wall, squeezing
on until 60 % of him is off screen (centre x −3 or 131). A short feeler (1 r) curls back on
screen and waves (curl ±1 rad at 1.4 Hz, 2 cycles). Then he slides back out.

**Two-Lobe Pulse (S, ~3 s; LOVE's signature).** Lub-dub kicks at 1.1 Hz × 3 beats, plus a narrow
dent (−0.20 r) on `αH` and two mounds at `αH` ± 40° (0.15 r): the top reads as a heart.

### 2.4 Mirror symmetry

PL's symmetry rules hold: the axis is `αH`, latched at the start; odd counts on the axis; mirror
pairs share target, spring and phase. On the floor that means everything is left-right
symmetric about his centre, which is the "magnetic field" look Kevin likes. The sanctioned
asymmetries are Carousel, Magnet Sweep, Lap, Tarzan and travel (crawl, hop, dash).

---

## 3. Schedules

### 3.1 Idle schedule (ferro mode on, nobody doing anything)

| Item | Value | Party |
|---|---|---|
| Rest after a pattern | S 1.0–2.0 s, M 1.5–3.0 s, B 3–5 s (incl. a 0.8 s Calm Dome) | 2–4 / 3–6 / 6–10 s |
| Tier odds | **S 0.20, M 0.60, B 0.20**. After 2 smalls, M ×1.6 | 0.45 / 0.50 / 0.05 |
| Big rules | Needs an M in the last 20 s; ≤ 2 per rolling 60 s; never two bigs back to back | 1 per 60 s |
| Cool-downs | Half of PL's per-entry values (e.g. Crown 15 s, Grand Crown 25 s) | PL §3.1 |
| Spike family | Never 3 spike-only patterns in a row | same |
| Stacking (`?stack=`) | **some (default): 60 % chance to add a layer 600–1200 ms after a start, max 2. wild: 85 %, max 3.** off: 1 | 40 % / 75 % |
| Stance moves (Climb, Hang, Tarzan, Lap) | Motion level rare: one every 60–120 s; often: 20–40 s; off: never (he stays on the floor). Picked 40 / 25 / 20 / 15 % | — |
| Crawl, pulse, twitch, filings | Always, during rests too (§2.1) | — |
| Expected minute | ~25 % rest, 15 % S, 45 % M, 15 % B; a big about every 25 s; ~2 stacks | 40 % rest |

**Stacking rules on 128x64:**
1. Up to 3 patterns, each owning a spike bank (as shipped). Calm Dome never stacks.
2. **One mover at a time.** Movers own the body's position: Crawl latch, Dash, Taffy, Twins,
   Climb, Hang, Tarzan, Lap, Hop Train, Lava Lamp, Peekaboo, Magnet Sweep. A mover can stack only
   with spike-only patterns (crowns, waves, splits, merges).
3. Live tentacles ≤ 5 (pool 6), ≤ 3 moving fast, ≤ 2 big. A layer that would break this is skipped.
4. Droplets ≤ 8 (pool), Rain and Pearl Chain need 4 and 3 free slots.
5. On the wall or ceiling the stacked set is the same, rotated with `αH`.

### 3.2 Show schedule (~50 s set)

Triggers: the web page's new **Show** button; 50 % of EXCITED or PROUD mood changes; and, in
"auto" (§4.4), every visit. Stacking is forced to wild for the set. Each act picks its items
from the listed pool with no repeats, so two shows differ.

| t (s) | Act | Pool (stack) | LED |
|---|---|---|---|
| 0–3 | Curtain | Crawl to x 64, Calm Dome (1) | dim to 30 % |
| 3–9 | Warm-up | Breathing Crown + Stadium Wave; pulse every 2 s (2) | breathe, flash on pops |
| 9–15 | Build | Rosensweig Crown or Spike Merge + Bloom or Pearl Chain (2) | swell with the field |
| 15–21 | Travel | Magnet Sweep or Hop Train, + Carousel (2) | chase follows x |
| 21–28 | Big 1 | Taffy + Crown on top (2) | ends lit, snap flash |
| 28–36 | Big 2 | Twins with mirrored crowns and arch (3) | two spots |
| 36–42 | Big 3 | Tarzan or Hang + Drip, landing into a Grand Crown (2) | chase, then spark |
| 42–47 | Finale | Rain + Carousel + Grand Crown, 4-drop splash (3) | sparkle + `spark()` |
| 47–50 | Bow | Calm Dome, one big jelly wobble (mode 2 +0.25), then a low Breathing Crown | fade to mood |

After a show: idle resumes with a forced 4 s rest and two small picks.

### 3.3 Mood temperaments

Expression changes still arrive (web page, cycles). In ferro mode there's no face, so each change
fires a **reaction beat** now and biases the scheduler until the next change. LEDs keep their
existing mood table.

| Expressions | Reaction beat | Bias | Spring tweak |
|---|---|---|---|
| NEUTRAL, CHILL, FOCUSED, SKEPTICAL | Calm Dome | Calm ×2, B ×0.5 | — |
| HAPPY, PROUD, WINKING, EXCITED | Hop Train (EXCITED: Grand Crown + hop) | Bloom, Hop Train ×2; EXCITED stack wild | L kicks ×1.3 |
| LOVE, KISSING | Two-Lobe Pulse | Bloom ×2 | — |
| SAD, WORRIED | Slump: J0 → 0.22 for the mood (a puddle), slow crawl | S ×2, B ×0.3, stance moves off | all f ×0.8 |
| SHY | Peekaboo | Peekaboo ×3, stays near a wall | — |
| ANGRY, ANNOYED, DEVIOUS, SASSY, MISCHIEF | Bristle (ANNOYED: the "nope" jiggle, 5.7 Hz sideways × e^(−5t)) | Crowns ×1.5, tremble at 3 Hz (PL: 2.2 Hz) | — |
| SURPRISED, SHOCKED | Grand Crown + hop 0.6 r | — | — |
| DIZZY, CONFUSED, NERVOUS, GLITCHING, THINKING | DIZZY: Carousel + Lap. CONFUSED: lean ±20° left then right (TILT). NERVOUS: shiver ×3. GLITCHING: the outline freezes 2–3 frames then jumps 60 ms ahead, spikes snap. THINKING: one feeler curls into a hook and taps the floor twice | — | — |

### 3.4 LED cues (8-LED strip, glows behind the screen)

A small cue layer over the mood mode (proposed `leds.cue(kind, pos, ms)`, `pos` 0..1 across the
strip; mapping depends on the strip's geometry, question 4):

| Cue | When | What |
|---|---|---|
| Pop flash | a crown pops, a hop lands | brightness ×1.5 for 120 ms |
| Swell | a pattern's field F | brightness follows 0.6 + 0.4·F |
| Chase | Crawl, Lap, Tarzan, Magnet Sweep | the lit spot follows his x (or his angle around the screen in a Lap) |
| Split | Twins | two spots in hues ±30 from the mood hue, moving apart and back |
| Ends | Taffy | outermost LEDs bright while the arms are latched |
| Spark | Grand Crown, Twins merge, show finale | the existing rainbow `spark()` |
| Lights on / off | enter / exit landing | white 150 ms, then fade to mood |

---

## 4. Entering and leaving ferro mode

### 4.1 Enter ("the ink takes the eyes"), ~1.1 s

The face's black background *is* the ferrofluid. It contracts into a drop and swallows the eyes.

| t | What |
|---|---|
| 0 | If a rotation move is running, retarget its angle to 0 (3 Hz ζ 0.9, ≤ 400 ms) and wait. Then a blink starts (`BW_SWAP`). |
| shut frame (~90 ms) | Swap to the ferro render: white fill, black blob of radius **76 px** (`hypot(64, 32) + 4`, covers the whole screen) centred (64, 32), and the real eyes drawn inside it at FACE_SCALE 0.41 with lids shut. The frame is identical to the face's blink frame, so the swap is invisible. Lids reopen. |
| 0.1–1.0 s | Progress spring P → 1 (1.4 Hz ζ 0.82, R1). R = lerp(76, r, smooth(min(1, 1.6P))). White opens in from the four corners, then the sides: an iris opening. Centre moves from (64, 32) to (64, 50.8) with a 3 px rise first. E held at 0.8 so the rim sloshes. |
| | Eyes ride inside at k = lerp(0.41, 0.18, P), centre +(0, −0.15R). At **P 0.45** (R ≈ 45 px, the eye pair is 37 px wide) they blink shut and stay shut; at P 0.6 they stop drawing. He closes his eyes and becomes the liquid. |
| landing (P > 0.97) | Mode 2 +0.22, mode 4 +0.04; splash crown F 0.8 for 300 ms across the arc; 2–3 droplets bounce out 10–20 px and roll home. LED: lights on. |
| +400 ms | Hello: one tentacle 1.2 r curls ±1 rad at 1.4 Hz for 3 cycles and retracts. The idle scheduler starts 1.5 s after landing. |

Cost: the eyes render for ≤ 0.6 s on top of the blob (≤ 5.5 + 1.5 ms), inside the budget.

### 4.2 Exit (reverse), ~1.2 s

1. **0–250 ms, gather.** Abort patterns (PL `abortSeq`), retract tentacles fast, droplets home at
   2.5 Hz. Crouch: mode 2 +0.12. Works from any stance: on the wall or ceiling he drops first.
2. **Launch.** Lift kick, P → 0. Centre arcs to (64, 32) while R grows to 76: the white shrinks
   into the corners, an iris closing.
3. At **P < 0.5** the eyes start drawing again, lids shut, and open on a blink as k grows back to
   0.41. All modes stiffen (ζ 0.6), so the edge is a clean circle by then.
4. **P < 0.03:** on a blink's shut frame, swap back to the normal render (black clear + face).
   FaceMotion reschedules its next move from now.

### 4.3 The rotation engine in ferro mode

Decision: **`motion.apply()` never runs in ferro mode and its own scheduler is paused.** Rotating
a white frame would swing the floor and break the physics, and the blob already has more motion
than the rare moves add. Its triggers are re-routed so nothing is lost:

| FaceMove / trigger | In ferro mode |
|---|---|
| TILT, mood leans (`onExpression`) | Lean: all live spikes and the bulge lean to the same side ± the lean angle (clamped to 25°), body bulge 0.10 that way, held for the move's time |
| WOBBLE | The "nope" jiggle: x jitter 0.12 r at 5.7 Hz × e^(−5t), mode 2/3 kicks |
| PENDULUM | Tarzan |
| DRIFT | A slow crawl back and forth, 3 legs of ±20 px |
| SPIN / SPIN2 | Lap / double Lap + DIZZY |
| FLIP | Hang + Drip |
| Web "move" test buttons | The same mapping, played now (`force`) |
| Motion level | off: no stance moves; rare: 60–120 s; often: 20–40 s (§3.1) |

### 4.4 Controls (no button on these builds)

- Web page: **Ferro** off / on / auto (NVS `ferro`). *Auto* = a ~60 s visit (enter, one show,
  idle, exit) every 8–15 min of face mode (question 1).
- **Show** button, **Boop** button (Dash or Wall Reach, 50/50), stack off / some / wild,
  size S / M / L, and `?seq=<name>` to play any pattern for tuning.
- Info mode from ferro: cut straight to it; returning from info replays the enter morph.
- `/state` adds `ferro.stance`, `ferro.seq` (e.g. `crown+taffy`) and the render stats (§5.4).

---

## 5. Performance plan

### 5.1 Cost model for the C3 (160 MHz, no FPU)

Derived from the measured rotation blit: 28 ms float vs 4 ms fixed over 8192 px is about 550 vs 80
cycles per pixel, so one soft-float add or multiply is about **0.4 µs** (~64 cycles).

| Operation | Planning cost |
|---|---|
| Float add / mul | 0.4 µs |
| Float divide / sqrtf | ~1 µs / ~4 µs |
| sinf, cosf / atan2f | ~15 µs / ~25 µs |
| Int32 add, shift, LUT read | ~0.01 µs |
| Q16.16 multiply (`mul` + `mulh`) | ~0.03 µs |
| Int32 divide | ~0.2 µs |

Calibrate on day one: a `/debug/bench` endpoint that times 1000 of each. The plan has a 2× margin
for this.

### 5.2 Element counts and number formats

| Part | Count (M) | Format | Notes |
|---|---|---|---|
| Body outline | **N = 64** vertices (5.625°, 1.5 px apart at r 15); 2 × 48 during Twins | **Fixed (Q16.16), sin LUT** | Angles are 16-bit binary angles. A 256-entry Q15 sin table (512 B, in RAM) makes every vertex, edge wave and mode term a lookup: vertex i at index (a0 + 4i) & 255, harmonic n at (n·idx + phase) & 255. No sinf and no rotation recurrences. `αH` is quantised to 1.4° (0.4 px at the rim). |
| Surface modes | n = 2, 3, 4 (cos + sin = 6 springs) | Fixed springs | — |
| Edge waves | n = 3, 4 | LUT | Amplitude and phase computed once per frame in float (4 sinf). |
| Spike slots | fan 9 + 3 banks × 9 + bristle 7 = 43 | Fixed springs, LUT adds | Only slots over 0.05 px are stepped and added. |
| Scalar springs | ~25 (E, F, g, D, L, x, y, Q, P, αH, κ, …) | **Fixed `FxSpring`** | Q16.16 x and v, coefficients `A = w²h`, `B = 2ζwh` precomputed in float only when f or ζ change; fixed h = 1/128 s (party's 8 ms), 5–6 sub-steps per frame, ~14 cycles each. |
| Tentacles | pool **6**, live ≤ 5, **8 nodes** (2 ride the body, 6 simulated), 21-vertex polygon (7 + 7 sides, 7-point bulb) | **Fixed, LUT** | Pose ψ(s) via the LUT; 2D node springs in Q16.16; length pass and normals with an alpha-max-plus-beta-min length (≤ 4 % error) and one int divide per node; bulb arc by a fixed 30° rotation. No sinf, hypotf or atan2f per node (party calls 3 trig per node: ~45 µs here). |
| Droplets | pool 8 (incl. satellites) + 2 bridges | State **float OK**, polygon fixed | ≤ 8 × ~40 flops. 8-vertex polygons from the LUT, stretched along velocity. |
| Rasteriser | ≤ 64 + 48 + 6·21 + 8·8 + 2·8 = **318 edges** | **Fixed** (Q12.4 crossings) | **Column-major** union fill (non-zero winding, as shipped). The U8g2 buffer stores 8 vertical pixels per byte, so a vertical span is 1–5 masked byte writes. Edges bucketed by first column; a column walks its active edges, sorts crossings, clears the spans. White background is one 1 KB memset to 0xFF. |
| Patterns, scheduler, events | ≤ 3 sequences, 20-event queue | **Float OK** | Closed-form envelopes, ≤ ~600 flops per frame. |
| Filings | 10 specks | Int | — |

**Rule of thumb for whoever writes it:** float is allowed only for work that runs ≤ ~100 times
per frame (≤ ~1000 flops, 0.4 ms in total). Anything per vertex, per node, per edge or per pixel
is integer.

### 5.3 Estimated cost per frame

| Stage | Typical (body, 2 tentacles, 1 crown, 1 drop) | Worst (wild stack: 5 tentacles, 43 slots, 8 drops, Twins) |
|---|---|---|
| White clear | 0.003 ms | 0.003 ms |
| Springs (~60 / ~110 live, fixed) | 0.02 ms | 0.05 ms |
| Outline + spikes (fixed + LUT) | 0.08 ms | 0.14 ms |
| Tentacles (fixed) | 0.06 ms | 0.15 ms |
| Droplets (float state) | 0.02 ms | 0.14 ms |
| Raster: edge setup + columns + fill | 0.15 ms | 0.45 ms |
| Pattern logic, scheduler (float) | 0.10 ms | 0.25 ms |
| Filings | 0.01 ms | 0.01 ms |
| **Total** | **≈ 0.5 ms** | **≈ 1.2 ms** |
| **With a 2× safety margin** | **≈ 1 ms** | **≈ 2.5 ms** |

Budget: ≤ 8 ms. The face's eye render (~5.5 ms) doesn't run in ferro mode, so ferro frames
should be *cheaper* than face frames: about 33 + 2 ms, held at the existing 40 ms frame cap
(~25 fps). Enter/exit frames add the eyes for ≤ 0.6 s: ≤ 7.5 ms.

The same scale written like `party_blob.h` (float springs at 8 ms steps, float outline, sinf per
tentacle node, float edges): springs ~1.3 ms, outline ~1.2 ms, 5 tentacles ~3.0 ms, raster ~1.5 ms,
**≈ 7–9 ms**. That is why the parts above must be fixed point.

**Memory:** ~8 KB static (edges 3.8 KB, tentacles 1 KB, outline 1 KB, banks 1.1 KB, LUT 0.5 KB),
no heap. The C3 has room.

### 5.4 Measuring and guarding

- `/state` → `ferro.render {avgUs, maxUs, stagesUs[clear, step, outline, tents, drops, raster, logic]}`,
  the same shape as party's.
- Guard: if a 2 s window has a frame over 8 ms, cap live tentacles at 3 and stacking at 2; lift
  after 10 s under 6 ms (Kevin prefers a dip in fps to losing tentacles, so this only catches a
  runaway).
- If the measured worst is under 3 ms, spend the headroom on **N = 128** (0.74 px vertices,
  rounder rims at L) and a tentacle pool of 8 for wild stacks, not on frame rate.

### 5.5 Build order (estimates)

1. Fixed-point core: LUT, `FxSpring`, 64-vertex body, column raster, base layer, floor stance,
   `/state` stats (about 4 h). Check the cost on the bench before going on.
2. Enter/exit morph and the FaceMotion hand-off (about 2 h).
3. Spike banks + scheduler + the surviving spike patterns (about 3 h; ported from `party_blob.h`).
4. Tentacles, droplets, Bloom / Feelers / Wall Reach, Crawl (about 4 h).
5. Stances and the new movers: Climb, Hang, Tarzan, Lap, Taffy, Twins (about 5 h), then Show,
   moods and LED cues (about 2 h).

---

## 6. Look dependencies **[look]**

Motion assumes these; the art director's spec decides them.
- **Edge and glint.** No rim or halo is assumed. If there's a glint (a 1–2 px white mark near the
  upper-left rim), it rides the outline at r − 3 px and hides while |stretch| > 0.10 or E > 0.6,
  so it doesn't flicker in fast moves.
- **Floor line.** None assumed (§1.2 keeps a 2 px gap so the contact patch doesn't read as
  cropped).
- **Full-white screen.** Highest OLED current and even wear. Roaming spreads the black area, so no
  fixed dark patch builds up.
- **Say bubble** in ferro mode needs an inverted style (black box, white text).

---

## 7. Open questions for Kevin

1. **Default ferro setting:** off, on, or **auto** (a ~60 s ferro visit with a show every 8–15 min
   of face mode)? Auto gives the bot the most life for free; it's my pick.
2. **Floor stance:** OK that he sits on the bottom edge and climbs the walls and ceiling, instead
   of floating in the middle like a corner blob?
3. **Tiny eyes:** strictly none (this spec), or a 2×3 px white pair that peeks for ~1 s in three
   moments (landing after Enter, Peekaboo, the end of a show)?
4. **LED strip geometry:** is the 8-LED strip a line left-to-right behind the screen, or a ring?
   The Chase, Split and Ends cues need to know where LED 0 is.
