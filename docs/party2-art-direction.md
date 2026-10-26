# Party mode round 2: art direction (tentacles + jiggle)

Audience: Kevin, the motion designer, whoever builds `party_blob.h` v2.
Scope: look, character, quality bar. Motion curves and the shape representation belong to the motion spec; where this brief sets a limit, the motion spec must stay inside it.
Baseline read from `origin/main`: `docs/plan-party-mode.md`, `docs/party-mode-motion-spec.md`, `docs/party-mode-layout-spec.md`, `docs/eye-art-direction.md`, `vizbot/party_blob.h`.

Sizes below use **M (r = 44 px, eye scale 0.36)** unless marked. Ratios are to body radius `r`.

---

## 0. The one-line pitch

**He is a drop of ink that wants the music.** The body is a jelly, the eyes are the person, the tentacles are his hands. Everything new must make him look *more curious and more delighted*, never more alien.

---

## 1. Character

### 1.1 Who he is when exaggerated

A toddler-cat made of ferrofluid. He's greedy for the beat and nosy about light and fingers, and he flinches when touched, then peeks back. He never gets angry with his body; anger stays in the eyes.

### 1.2 Two tentacle vocabularies (the core decision)

| | **Spikes** (exist today) | **Reachers** (new) |
|---|---|---|
| Means | excitement, the field, the music | intent: "I want that" |
| Shape | sharp Rosensweig cone, short | plump finger with a round, slightly bulbed tip |
| Triggers | beat, pulse, shake, landing | tap, finger hold/drag, bright spot in the show, roam/run |
| Max length past the rim | 0.42 r (today's value) | 1.0 r normal, 1.25 r only while following a finger |
| Count at once | fan of 9, or porcupine 12 | **max 2** |

Rule: **sharp tips never point at a finger.** Touch always gets round tips. Sharp is for music and shock only.

### 1.3 Personality rules

1. **Eyes lead, body follows.** The eyes look at the target first, and a reacher starts 80–150 ms later. A reacher never goes where he isn't looking.
2. **Reach slow, retract fast.** Going out takes ≥ 3 frames (≥ 150 ms). Coming back takes 2–3 frames, overshoots into a dimple and bounces the body. The asymmetry is what reads as shy.
3. **Mass is conserved.** When a reacher is out past 0.5 r, the body shrinks up to 6 % in radius. On retract the body swells back with a jiggle. It reads as one liquid, not an animal with arms.
4. **Past 0.8 r the body joins in.** It leans and stretches toward the reach (velocity-stretch axis aimed at the tip). A long reach is the whole drop being pulled, never a long arm on a still body.
5. **Idle is calm.** At rest only breath, wobble and eyes move (today's rule stays). Reachers appear at most once per 12 s on their own, and are back in within 1.5 s.
6. **Poke = flinch, then peek.** All tentacles snap in on a poke. One small reacher (≤ 0.3 r) may peek back toward the finger before he runs.

### 1.4 Do / don't

| Do | Don't |
|---|---|
| Round, gummy reacher tips | Hooks, claws, barbs, forked tips |
| One smooth curve per reacher (C shape); a brief S only during a retract | Two or more bends, coils, loops, knots |
| Tentacles that touch **merge** (liquid union) | Tentacles crossing, overlapping as separate layers, or punching holes |
| Pure black, same as the body | Colour, suckers, texture, segments, joints |
| Reach into the show, toward light, beats and fingers | Reach at the screen edge, the viewer-facing corner, or the clock |
| Slow out, fast in, wobble afterwards | Constant writhing, wriggling at rest, independent squirming |
| Body visibly the biggest black mass | More tentacle than body: tentacle area ≤ 0.6 × body area at all times |
| Drips that fall back into the body | Bits that detach and stay detached, or crawl around on their own |
| Spike fan on the beat | Using tentacles as legs (walking or crawling across the screen) |

**Creepy test:** if a still frame looks like a spider, a squid or a hand, it fails. Every still frame must read as *a drop with eyes*.

---

## 2. Silhouette and shape language

### 2.1 Reacher profile

- **Root.** Width 0.55 r (24 px), blended into the body with concave flanks: a fillet of ≥ 0.15 r, like today's `PARTY_SPK_LUT`. It grows from a mound, never from a hard corner.
- **Taper.** `w(u) = w_tip + (w_root − w_tip)·(1 − u)^1.6`, where u runs 0 at the root to 1 at the tip. It stays plump most of the way and narrows late.
- **Tip.** A round cap, never a point. `w_tip = max(5 px, 0.14 r)`: 5 px at S, 6 px at M, 8 px at L. Past 0.5 r of extension the cap bulges to 1.15× the width just below it, like a droplet about to form. That bulb is the cute.
- **Retract by length, not width.** A retracting reacher shortens. Its width never drops below `w_tip`. That one rule stops thin parts aliasing (§3.3).
- **Curvature.** The centreline bend is ≤ 70° over the whole length, and the tip curl is ≤ 30° ("beckon" is the only exception, §6.3).

### 2.2 Spike profile (unchanged look, tighter limits)

- A sharp cone, up to 0.42 r. The last 4 px may taper to a point **only** with edge AA on (§3.2).
- Porcupine: up to 12 short spikes, no reachers at the same time.

### 2.3 Reach and count limits

| Item | Limit | M in px |
|---|---|---|
| Reacher length past the rim, normal | ≤ 1.0 r | 44 |
| Reacher length while following a finger | ≤ 1.25 r; beyond that the body hops toward the finger instead | 55 |
| Reachers at once | ≤ 2, at least 40° apart at the root | |
| Total black area | tentacles ≤ 0.6 × body | |
| Direction | within ±60° of the screen-centre direction (today's spike rule) | |
| Tip clearance | ≥ 4 px from the 1.69's rounded-corner safe area (r_c 40) and the flat edges; ≥ 4 px from any visible overlay pill (clock, toast) | |

**Why 1.0 r.** Past about one body radius, a constant-width arm reads as a separate limb (squid). With the taper, the bulb tip and the body leaning (rule 1.3.4), up to 1.25 r still reads as "the drop is being pulled".

### 2.4 Eyes read first

1. The eye whites are the only bright, round, hard-edged things on the blob. **Nothing round and bright goes on a tentacle.** A specular dot on a tip reads as a third eye.
2. The body keeps today's eye clearance: radius ≥ 0.97 r within ±35° of horizontal and ≥ 0.85 r elsewhere. Reachers root **outside** the ±35° horizontal bands only when the body is squashed wide (J > 0.15); otherwise anywhere in the inward half-plane.
3. The eyes look along an active reacher (rule 1.3.1). The eye spring keeps its 2.4 Hz lag.
4. Negative space: keep a ≥ 1 tip-width gap of show between two reachers and between a reacher and the body below its root fillet. If they would touch, they merge into one (union), never a slit.

### 2.5 Jiggle ("much jigglier")

| Item | Today | Round 2 cap |
|---|---|---|
| Squash J on events | about ±0.16 | −0.22 (tall) … +0.30 (splat) |
| Edge-wobble boost at agitation 1 | 2.5× | 3.5× |
| Body oscillation frequency | 2.2 Hz | 1.5–3 Hz. **Nothing body-wide above 4 Hz**: at 20 fps it reads as judder, not jelly |
| Visible bounces after a big event | 2–3 | 3–4, settled within 1.5 s |
| Rim speed at rest | ≤ 0.55 px/frame | unchanged; ≤ 1.5 px/frame during events |

All of this stays inside the eye-clearance floors in §2.4.2. Jiggle is low, wide and well damped (jelly), never a fine tremble. The shake's 5.7 Hz sideways jiggle is the one sanctioned exception.

---

## 3. Surface and light at RGB565

### 3.1 Does flat rim + sheen + shadow still work? Yes, with four changes

| Element | Body | Tentacles |
|---|---|---|
| Rim `max(show × 0.5, face × 0.22)`, 1.5 px | keep | keep, constant 1.5 px whatever the width. It is per pixel, so a reacher in a red patch gets a red rim for free: local reflection |
| Sheen crescent (show-tinted, upper left) | keep | none of its own. The crescent may run into a reacher's root when the root is in the 200°–250° band |
| Specular dot | keep, hidden in fast motion | **never** (third-eye risk) |
| Floor shadow (2 tones, screen-anchored dither) | keep | none on the floor: reachers are up in the air |
| **New: lift shadow** | no | one tone at 75 % brightness, offset (+1, +2) px down-right (light from the upper left, the same as the sheen), drawn only on show pixels and never on the body. No dither |

The lift shadow is what keeps a thin black reacher separate from a busy mid-tone show, where the rim alone is weakest (§4.3).

### 3.2 Anti-aliasing: the one place we break "no gradients"

**Decision: 1 px coverage AA on the outer edge of the rim only, 4 levels, on by default, behind a toggle (`aa=0` on `/bot/party`) for bench A/B.**

- **Why here.** Today the eyes are hard-edged and that's right: they hold still and they're the identity. The blob is different. Its edge moves continuously, and round 2 makes it move faster and thinner. Integer spans make a moving edge **crawl** (pixels popping on and off along a slow wobble), and thin tips **blink**. AA on the moving silhouette is quality, not decoration.
- **How (art spec, not code).**
  - Coverage of the boundary pixel only. The band is 1 px, never wider: wider looks blurry and off-style.
  - Horizontal coverage from the existing 1/16 px crossings. Vertical coverage from 2 sub-rows (y + 0.25, y + 0.75), so near-horizontal edges (the top of a sideways reacher) are covered too.
  - Quantise to quarters: 0, ¼, ½, ¾, full. Blend show → rim colour with shift-and-mask on RGB565 (the same trick as `partyDark75`). Four levels don't band at 1 px.
  - The inner boundary (rim to black body) stays hard. The rim is already the soft step.
- **Sub-pixel position.** With AA on, the outline uses the float centre. Drop today's 0.6 px snap hysteresis for the outline, or AA buys nothing. The eyes keep integer positions with hysteresis (they're hard-edged and ride a lagging spring, so the 1 px relative jitter hides in the lag).
- **8-bit canvas fallback.** No AA, no rim, no shadows (as today). The shape must still read.

Nowhere else gets a gradient: no radial body shading, no glow, no blur, no alpha on the eyes.

### 3.3 Thin-part floor

| Part | Minimum black core | Notes |
|---|---|---|
| Reacher anywhere | 5 px at S, 6 px at M, 8 px at L | that's `w_tip`; total width with rim is about 8–11 px |
| Spike tip | may reach a point in its last 4 px | only with AA on; with AA off, spikes cap at 3 px tip width |
| Gap between parts | ≥ 1 tip width | below that, merge |

Below about 3 px, a moving shape filled as spans breaks into dotted pixels at 20 fps, whatever the AA. The floor is set above that with margin.

### 3.4 Banding and shimmer rules

1. Dither only on things that move slowly: the floor shadow. Never on tentacles, the lift shadow or the sheen. Moving dither swims.
2. The sheen stays one flat colour per frame, low-passed (τ 0.3 s, as today), so it never strobes with the show.
3. The rim colour is computed per pixel from the show, so it flickers exactly as much as the show does. That's fine, because it's reflection. Never add frame-to-frame noise of our own.
4. Tip speed cap: ≤ 12 px per frame. Faster than that reads as a teleport at 20 fps.

---

## 4. Interaction with the ambient show

### 4.1 He wants the light

- **Reach target.** Every 200 ms, sample about 6 points on an arc 1.5–2 r out in the inward half-plane, and read their luminance from the canvas. A reacher may go toward the brightest one when it's clearly brighter than the rest. This is the "greedy" in the character, and it makes the show and the creature look connected.
- On the 8×8 hi-res effects, sample block centres so the target doesn't jitter between neighbouring pixels.

### 4.2 Reflect and shadow

- **Reflect.** The per-pixel rim (§3.1) already reflects locally, and the body sheen reflects the screen centre. That's enough. No mirrored image and no environment mapping.
- **Shadow.** The body keeps its floor shadow. Reachers get the lift shadow (§3.1). Both darken, neither paints a colour.

### 4.3 Legibility by scene type

| Scene | What carries the silhouette | Risk | Mitigation |
|---|---|---|---|
| Bright, saturated (plasma, fire) | black against colour; rim at show/2 is a soft edge | none | none |
| Dark (galaxy, starfield) | rim floor: face colour at 22 % | thin reachers vanish | if a 6 px reacher isn't continuous on galaxy at 60 cm, raise the floor to 28 % **globally**. Never per part: one outline, one rule |
| Busy mid-tone | rim weakest (show/2 vs black is a small step) | reacher edges merge into texture | the lift shadow |
| Fast strobe effects | rim flickers with the show | none | none (reflection is allowed to flicker) |

### 4.4 Overlays

Overlays draw on top. A tentacle sliding under the clock pill looks like a bug, so reach targets exclude the keep-out of any visible overlay (§2.3). When the toast is up, reachers that point at it shorten.

---

## 5. Quality bar: bench acceptance criteria

Run on the 1.69 (vizBot-old-dino) and the CoreS3 (skunky) at sizes S, M and L. "60 cm" means a normal desk distance. Frame checks use a 240 fps phone slow-mo or the web screenshot endpoint (`web_server.h` reads the byte-swapped canvas).

| # | Criterion | Pass | Fail |
|---|---|---|---|
| 1 | **Frame rate** | ≥ 20 fps sustained with two reachers plus the spike fan out on the heaviest ambient effect. `party.render` ≤ 6 ms avg and ≤ 9 ms max in `/state` (today 3.0 / 4.0). No frame gap over 70 ms | any visible hitch, or stats over budget |
| 2 | **No edge crawl** | 30 s of idle on a flat bright effect: the rim moves as one shape. In slow-mo, no edge pixel toggles more than twice a second where the edge moves under 0.3 px/frame | pixels sparkle along a still-ish edge |
| 3 | **No thin-part flicker** | while reachers extend and retract, no tip blinks out, no break, no 1-frame gap, no hole where a reacher overlaps the body or another reacher | any gap, hole or blink |
| 4 | **Eyes read first** | at **S**, 60 cm, in the middle of the biggest tentacle event, Kevin names each P1 expression (NEUTRAL, HAPPY, SURPRISED, SAD, ANGRY, CHILL, MISCHIEF, WINKING) within 2 s. No eye pixel ever outside the body | he hesitates, or eye pixels show outside |
| 5 | **Creepy test** | 20 random paused frames during events: every one reads as "a drop with eyes" | any frame reads as a spider, a squid or a hand |
| 6 | **Corners** | in all four corners at L, at max reach, no pixel of tentacle, rim, AA or shadow clipped by the bezel. Check against the r 30/35/40/45 test arcs | any clip |
| 7 | **Every scene** | over the brightest effect, galaxy and the busiest effect, the thinnest reacher stays continuous at 60 cm, in all 4 face colours (white, cyan, amber, pink) | it disappears or breaks into dots |
| 8 | **Calm idle** | 60 s idle: reachers out under 15 % of the time, ≤ 5 reaches, none longer than 0.6 r without a trigger | it looks restless or anxious |
| 9 | **Jiggle feel** | after a poke the body settles in ≤ 1.5 s with 3–4 visible bounces; no body-wide motion above 4 Hz | jitter, or it rings for more than 1.5 s |
| 10 | **AA toggle** | `aa=0` vs `aa=1` A/B on a bright effect: Kevin prefers 1, and it doesn't look blurry | blurry, or no visible difference (then drop it) |

---

## 6. Hero moments (design these first)

### 6.1 "The beat grab" (CoreS3 with music; the 1.69 pulse gives a gentler version)

1. Between beats: he's settled, eyes half-lidded (CHILL), the rim glinting the show.
2. Bass hit: the eyes flick to the brightest part of the show. 100 ms later a reacher shoots out (≥ 3 frames) and its bulb tip "taps" that spot right on the next beat.
3. Snap back in 2 frames. The body splats (J +0.25) and wobbles 3 times.
4. On a sustained drop: two reachers take turns, left-right, on alternate beats, like drumming. The spike fan pumps on the body between them.
5. Music stops: the reachers drain back, he gives one satisfied squash and blinks (HAPPY).

### 6.2 "Shy poke"

1. The finger lands on the blob: a dent at the touch point, every tentacle yanks in within 2 frames, eyes SURPRISED with a forced blink.
2. Big jelly: J +0.30 splat, 4 bounces.
3. Beat of stillness (300 ms). One tiny reacher (≤ 0.3 r) peeks out toward where the finger was, with the eyes looking along it.
4. It snaps back and he dashes to another corner (the existing poke run).

### 6.3 "Follow the finger"

1. The finger touches the show and holds: the eyes look, and a reacher grows toward the finger, round tip first.
2. The finger drags: the tip follows like a cat's paw, ≤ 1.25 r, with the body leaning and stretching toward it.
3. The finger goes past reach: the tip **beckons** (the one allowed 30–60° curl), then the body hops one step toward the finger, keeping inside its corner's wander box.
4. Release: the reacher drains back, the body bobbles, and he blinks HAPPY.

### 6.4 "Slingshot corner run" (upgrades the roam and the poke dash)

1. Glance (existing 0.7 s lead): the eyes look at the destination corner.
2. A reacher stretches along the arc toward it. At max reach the tip flattens slightly, as if it had stuck.
3. The body stretches (S at its max) and pulls itself along the reacher. The reacher shortens as the body travels, so the reach is spent by arrival.
4. Landing: J +0.30 splat, a spike burst toward the screen centre, 3–4 bounces, a blink. On a poke dash this is faster with no glance; on a roam it's lazy.

### 6.5 "Wake-up stretch" (enter party mode)

1. Existing morph: blink, the stroke around the eyes becomes the drop, a hop into the corner, a splat.
2. A beat of rest, then one reacher stretches slowly toward the screen centre while the eyes squint (CHILL), like a cat's morning stretch.
3. A second reacher on the other side (≥ 40° apart), shorter.
4. Both drain back, a big contented jiggle, eyes open wide (HAPPY). The party starts.
