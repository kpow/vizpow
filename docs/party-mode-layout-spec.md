# Party mode: corner blob layout spec

> **Superseded in part (Kevin, 2026-10-25):** there's no mouth in party mode, and corner is not a setting: he picks a random corner at the start and runs to a new one each time he's poked. Where this spec differs, `docs/plan-party-mode.md` wins.

Owner of this doc: layout (size, placement, spacing, legibility, config). Motion (wobble shape, wander path, squash, shadow animation, blob-to-face transitions) belongs to the motion designer. Where the two meet, this doc gives the motion designer an **envelope contract** (section 1.4) instead of a shape.

Nothing in the repo was edited. Numbers come from the files cited, and they were checked with a small script (a throwaway `party_calc.py`, not kept).

---

## 0. Ground truth read from the code

| Fact | Value | Source |
|---|---|---|
| Eye half-width at scale 1 | `BOT_EYE_R` = 48 px, **the same on every board** | `bot_eye_params.h:53` |
| Gap between the eyes | 0.08 R | `bot_eye_params.h:54` |
| Eye centre offset | `Rw + 0.04 R`, so neutral eyes sit at ±1.04 R, outer edge ±2.08 R (with the 4 % blink widening) | `bot_eye_model.h:816-822` |
| Biggest eyes | scale 1.12 (doc max) × mood pop +6 % → outer edge ±2.47 R | `eye-art-direction.md` §3.1, mood pop |
| Brow top | about −1.22 R typical (lid top −0.92 R, −0.24 R, half thickness), −1.46 R on SURPRISED or SHOCKED with a pop | `bot_eye_model.h:1045` |
| Brow thickness | 5-7 px × k | `bot_faces.h` table |
| Mouth | `mouthOffsetY` 56-65 px × k below centre, so the face's bottom edge is about +1.55 R | `bot_faces.h` |
| Ambient stroke | 5 px × k black outline, drawn when `botBackgroundStyle == 4` | `bot_eyes.h:348` |
| Album-art precedent | circle r 48, 2 px `0x3186` ring, centre (W−51, 51), face scale 0.36 nudged up 4 px for the mouth | `bot_mode.h:811-822`, `album_art.h:19-21` |
| Time overlay | 72×24 pill at x = (W−72)/2, y 4 | `bot_overlays.h:372-377` |
| Toast | 84-216 × 24 pill, top centre, y 6 | `touch_ui.h:495-502` |
| Notification | full-width 24 px banner sliding from y −24 to 0 (only used for "Personality: X") | `bot_overlays.h:316-334`, `bot_mode.h:1031` |
| Speech bubble | y = `OVERLAY_BUBBLE_Y` (210 on the 1.69, H−68 = 172 elsewhere), width up to W−6, height 36/56/76 (1-3 lines), tail up; tilted upside down it moves to y 8 with the tail down | `config.h`, `layout.h:35-38`, `bot_overlays.h:184-240` |
| Swipe handle | 32×4 at (104, 262), for 4 s after a touch | `touch_ui.h:1306-1308` |
| Top pill / sheets / landscape rails | Only drawn when `ui.screen != UI_HOME`. On the home screen only the face, the overlays, the toast and the handle are drawn | `touch_ui.h:1304-1320` |

**Pixel pitch.** 1.69" 240×280 = 0.1164 mm/px (218 ppi). CoreS3 2.0" 320×240 = 0.127 mm/px (200 ppi). 1.3" 240×240 = 0.0973 mm/px (261 ppi).

**1.69 rounded corners.** I found nothing about them in the repo or the libs. **Estimate r_c ≈ 40 px (about 4.7 mm).** The top pill's left cap at x 22 sits inside a 40 px arc, which fits that estimate. **The layout does not depend on the exact value.** A disc of radius ρ ≥ r_c lies inside the rounded rectangle exactly when its centre is at least ρ from both flat edges. The rounded rectangle is the inner rectangle grown by a disc of r_c, so for ρ ≥ r_c the containment condition reduces to the flat-edge one. The smallest envelope here is 45 px (S), so the blob is safe for any r_c ≤ 45. I also checked the shadow ellipse against r_c = 40 in both bottom corners for all three sizes. It fits, and S is the tightest at 37.5 px from the arc centre. To confirm r_c on the bench, draw quarter arcs at r 30/35/40/45 in one corner and see which is the first one that shows in full. The CoreS3 and the 1.3 have square corners.

---

## 1. Blob size

### 1.1 Rules

1. **Eyes only, no mouth, at every size.** The mouth adds +1.55 R below centre (the face becomes 2.77 R tall instead of 2.14 R). That would either force a much bigger blob or push the eyes off-centre. Kevin's brief is "two eyes sit inside the blob". The eye-art legibility test 1 already requires every expression to read with the mouth and brows hidden (`eye-art-direction.md:232`). Talking feedback becomes a blob squash, which is a motion designer item.
2. **Brows on at M and L, off at S.** At M they are 1.8-2.5 px thick, at L 2.2-3.1 px. At S (1.5-2.1 px) they drop under the doc's 2 px feature floor and start to look like noise. The eye model already has the `drawBrows` argument (`bot_eye_model.h:1025`). `renderBotFace` just doesn't pass it yet.
3. **The eyes sit at the blob centre, with no nudge.** Without a mouth, the content (brows to lower lids) runs from −1.22 R to +0.92 R. Its midpoint is −0.15 R (2.6 px at M), so the eyes read as sitting a little high in the body, which looks right for a creature. This differs from album art, which nudges the face up 4 px only because the mouth hangs below it.
4. **Body radius r0 = 2.55 × eye R.** At rest that leaves 0.47 R (8 px at M) of black between the outer eye edge and the blob edge.
5. **Size floor k = 0.30.** The thinnest authored features are the X-eye strokes (0.14 R) and the closed arc (0.18 R). At k 0.30 the X strokes are exactly 2 px, the doc's rule-3 floor (`eye-art-direction.md:234`). Below that, the X, spiral and lid-slant details break up. Never ship a size under 0.30.

### 1.2 Size tiers (the same pixels on every target)

`BOT_EYE_R` is 48 px on every board, so one table serves all of them.

| Tier | Eye scale k (`renderBotFaceAt`) | Eye R | Eye centres | Eye content half-width typ / peak | Body r0 | Envelope ρ (+20 %) | Below-centre extent (shadow) | Brows |
|---|---|---|---|---|---|---|---|---|
| S | **0.30** | 14.4 | ±15 | 30.0 / 35.6 | **37** | 45 | 47 | off |
| **M (default)** | **0.36** | 17.3 | ±18 | 35.9 / 42.7 | **44** | 53 | 55 | on |
| L | **0.44** | 21.1 | ±22 | 43.9 / 52.2 | **54** | 65 | 68 | on |

"Eye centres" is the horizontal offset of each eye from the blob centre: 1.04 R, before any per-face scale.

### 1.3 Screen share and legibility

| Target | Tier | Body area (π r0²) | Eye width | Body diameter | Eye width at 60 cm |
|---|---|---|---|---|---|
| 1.69 240×280 | S / **M** / L | 6.4 % / **9.1 %** / 13.6 % | 3.4 / **4.0** / 4.9 mm | 8.6 / **10.2** / 12.6 mm | 19 / **23** / 28 arcmin |
| CoreS3 / Stackchan 320×240 | S / **M** / L | 5.6 % / **7.9 %** / 11.9 % | 3.7 / **4.4** / 5.4 mm | 9.4 / **11.2** / 13.7 mm | 21 / **25** / 31 arcmin |
| 1.3 240×240 | S / **M** / L | 7.5 % / **10.6 %** / 15.9 % | 2.8 / **3.4** / 4.1 mm | 7.2 / **8.6** / 10.5 mm | 16 / **19** / 24 arcmin |

- **Why M = 0.36.** It equals `ART_FACE_SCALE`, so the eyes are exactly the size Kevin already ships and approved in the album-art corner. A cover arriving or leaving swaps the frame around the eyes and never resizes them. M is about 9 % of the screen, so the animation stays dominant. At 60 cm a 23 arcmin eye is roughly five times the size of a 20/20 letter detail, so the silhouette reads easily. The lid slant (0.45 × H/2 ≈ 7 px) and the pupil presets (dot ≈ 3 px across) stay visible.
- **The 1.3 is physically the smallest** (3.4 mm eyes at M). It is still legible, and Kevin has no 1.3s running, so it ships with the same default and stays untested. If it reads small on the bench, give the 1.3 a default of L. That is a one-line board override.
- **Comparison with album art.** That circle is r 48 because it has to fit a mouth. The blob is r 44 at the same eye scale because it holds eyes only. The eyes span 72 of the 88 px body, which gives the "big eyes in a small sooty body" look.

### 1.4 Envelope contract for the motion designer

All values are relative to body radius r0 and the blob centre.

| Item | Limit | M in px |
|---|---|---|
| Radius peaks (spikes, bulges) | ≤ **1.20 r0** | 53 |
| Radius troughs within ±35° of horizontal (3 and 9 o'clock) | ≥ **0.97 r0** | 42.7. On a mood pop the eyes reach 2.47 R (42.7 px), so a deeper dent there would show eye pixels outside the body |
| Radius troughs elsewhere (above and below the eyes) | ≥ **0.85 r0** | 37.4. The content only reaches 1.46 R (25 px) up and 1.17 R (20 px) down, so there's plenty of room to squash |
| Whole-body squash/stretch | must stay inside the two rows above | |
| Eye offset inside the body (bob, lag) | ≤ 0.08 r0 total | ±3.5 px |
| Ambient stroke | No action needed: the 5k px black stroke merges into the black body | |

### 1.5 Shadow

The shadow is a soft ellipse below the body. It is drawn before the body, so only its lower crescent shows.

| | Ratio to r0 | S | M | L |
|---|---|---|---|---|
| Centre, below the blob centre | +0.95 r0 | +35 | **+42** | +51 |
| Half-width rx | 0.90 r0 | 33 | **40** | 49 |
| Half-height ry | 0.30 r0 | 11 | **13** | 16 |
| Lowest pixel, below the blob centre | 1.25 r0 | 47 | **55** | 68 |
| Visible crescent under the body | 0.25 r0 | 9 | **11** | 14 |

- **Look.** Darken the canvas, don't paint a colour. Use three concentric bands about 3 px apart at roughly 70 / 50 / 30 % brightness, inner to outer, as a read-modify-write on the RGB565 sprite (`(c >> 1) & 0x7BEF` is 50 %). There is no third colour, so the eye-art palette rule holds.
- **Direction.** The shadow always falls straight down (one light from above), in all four corners.
- The motion designer can shrink and lighten it as the blob bobs up. The layout reserves the full 1.25 r0.

---

## 2. Placement

### 2.1 Model

Three numbers per tier define placement: the envelope ρ = 1.2 r0, the extent below centre `below` = 1.25 r0 (the shadow), and the wander box (0.60 r0 × 0.45 r0).

- **Tuck point T.** The closest the blob *centre* ever gets to its corner:
  - right corners: T.x = W − EDGE − ρ. Left corners: T.x = EDGE + ρ.
  - top corners: T.y = EDGE + ρ. Bottom corners: T.y = H − EDGE − below.
  - EDGE = 2 px. At the tuck point a full spike just kisses the edge. On the 1.69 that also clears the rounded corners (see section 0).
- **Wander box.** It runs *inward* from T: 0.60 r0 wide and 0.45 r0 tall (M: 26 × 20 px, about 3 × 2.3 mm on the 1.69). The blob centre may be anywhere inside it. Because the box only grows inward, wandering never pushes the blob toward the edge, and it doesn't need a bigger margin.
- **Home.** The centre of the wander box, where the blob rests when wander is off. It is also where it settles after a poke.
- **Keep-out rectangle K.** The bounding box of every pixel the blob can touch: the wander box grown by ρ left, right and up, by `below` down, plus a 6 px gap. Overlays avoid K (section 3).

### 2.2 Numbers at M (default). Same columns for every target

| Target | Corner | Tuck T | Wander box (centre x, centre y) | **Home** | Keep-out K (x, y) |
|---|---|---|---|---|---|
| **1.69** 240×280 | **TR** | (185, 55) | x 159-185, y 55-75 | **(172, 65)** | x 100-240, y 0-136 |
| | BR | (185, 223) | x 159-185, y 203-223 | **(172, 213)** | x 100-240, y 144-280 |
| | BL | (55, 223) | x 55-81, y 203-223 | **(68, 213)** | x 0-140, y 144-280 |
| | TL | (55, 55) | x 55-81, y 55-75 | **(68, 65)** | x 0-140, y 0-136 |
| **CoreS3 / Stackchan** 320×240 | **TR** | (265, 55) | x 239-265, y 55-75 | **(252, 65)** | x 180-320, y 0-136 |
| | BR | (265, 183) | x 239-265, y 163-183 | **(252, 173)** | x 180-320, y 104-240 |
| | BL | (55, 183) | x 55-81, y 163-183 | **(68, 173)** | x 0-140, y 104-240 |
| | TL | (55, 55) | x 55-81, y 55-75 | **(68, 65)** | x 0-140, y 0-136 |
| **1.3** 240×240 | **TR** | (185, 55) | x 159-185, y 55-75 | **(172, 65)** | x 100-240, y 0-136 |
| | BR | (185, 183) | x 159-185, y 163-183 | **(172, 173)** | x 100-240, y 104-240 |
| | BL | (55, 183) | x 55-81, y 163-183 | **(68, 173)** | x 0-140, y 104-240 |
| | TL | (55, 55) | x 55-81, y 55-75 | **(68, 65)** | x 0-140, y 0-136 |

### 2.3 Home points at S and L

| Target | S: TR / BR / BL / TL | L: TR / BR / BL / TL |
|---|---|---|
| 1.69 | (182,56) / (182,222) / (58,222) / (58,56) | (157,79) / (157,198) / (83,198) / (83,79) |
| CoreS3 | (262,56) / (262,182) / (58,182) / (58,56) | (237,79) / (237,158) / (83,158) / (83,79) |
| 1.3 | (182,56) / (182,182) / (58,182) / (58,56) | (157,79) / (157,158) / (83,158) / (83,79) |

Wander box at S: 22 × 17. At L: 32 × 24.

### 2.4 What the blob must avoid, and how

| Thing | Where it is | Resolution |
|---|---|---|
| Top pill, dock, sheets, landscape rails | Only off the home screen | **The party layout is suspended whenever `ui.screen != UI_HOME`.** The face uses the normal `uiFaceTransform` mini preview over the effect, and returns to the blob on Home. The blob-to-mini morph is a motion item. This removes every pill, rail and sheet conflict. |
| Toast, clock, notification, bubble | Top centre and bottom centre | Section 3 |
| Swipe handle (104-136, 262-266) | 1.69 bottom centre | At BR or BL the shadow's left tip can pass under it while wandering. The handle is drawn last, so this is harmless. No change. |
| Rounded corners (1.69) | r_c ≈ 40 | Covered by ρ ≥ r_c plus the flat-edge margin (section 0). No extra inset. |
| Battery and Wi-Fi status | Only in the top pill, which isn't on Home | Nothing to do |

**Two smaller items.** Album art and sleeping Zzz:

- **Album art in party mode.** Keep the art circle exactly as it is (ring, r 48, scale 0.36, mouth on), but mirror it into the party corner: centre x = W − 51 or 51, y = 51 or H − 51. The bot shouldn't jump corners when a cover arrives. The eye scale is identical, so only the frame changes.
- **Sleeping Zzz.** They are drawn at `BOT_FACE_CX + 50, BOT_FACE_CY − 40` (`bot_mode.h:889`), which is wrong in party mode. Anchor them at (blob.x ∓ (r0 + 4), blob.y − 0.3 r0) on the side facing the screen centre, step each glyph away from the blob, and cap the rise at 0.9 r0 (40 px at M) so they stay on screen in the top corners.

---

## 3. Overlays in party mode

**Principle.** Each overlay stays in its usual band (top or bottom). If it intersects K, it slides sideways into the free span beside K. If it doesn't fit there, it moves to the opposite band. Nothing is suppressed, and the blob is never drawn over.

### 3.1 Per element

| Element | Blob in a **top** corner (TR, TL) | Blob in a **bottom** corner (BR, BL) |
|---|---|---|
| **Clock** (72×24) | Top band, far side: x = `PARTY_CLOCK_INSET` (TR) or W − inset − 72 (TL), y 4. Inset is 22 on the 1.69 (clears the corner arc: pill cap 38 px from the arc centre, under 40) and 8 elsewhere. | Unchanged, top centre |
| **Toast** (84-216 wide) | Top band, centred in the free span beside K if it fits (span = inset to K.x0 − 0 for TR). Otherwise it goes to the bottom band, centred, y = `PARTY_TOAST_LOW_Y` = H − 44 (236 on the 1.69, which clears the handle at 262; 196 on 240-tall screens). | Unchanged, top centre |
| **Notification banner** | Slides up from the bottom edge (y H → H − 24) | Unchanged, top |
| **Speech bubble** | Unchanged position (bottom band), tail up. Tail x = clamp(blob.x, bubble.x + 16, bubble.x + w − 16), so it points at the blob. While a bottom-band toast is up, the bubble lifts so its bottom is 6 px above the toast, but never above K.y1. | Top band, y 8 (34 when the clock is on), **tail down**, tail x toward blob.x as on the left |
| **Upside-down bubble flip** (`accelX < −0.3`) | Ignored in party mode, so it can't land on the blob. The horizontal tilt shift stays. | Ignored |

### 3.2 Resolved at M

| Target | Top-band free span beside K (TR / TL) | Clock in TR mode | Toast in TR mode |
|---|---|---|---|
| 1.69 | 0-100 / 140-240 (100 px; 86 usable after the corner inset) | x 22-94 ✓ | Only an 84 px toast fits beside K, so in practice **bottom band y 236** |
| CoreS3 | 0-180 / 140-320 (180 px) | x 8-80 ✓ | Beside K when the toast is ≤ 172 px wide (all scene names and most toasts). Wider ones go to the bottom band at y 196. |
| 1.3 | 0-100 / 140-240 | x 8-80 ✓ | No toasts (no touch UI) |

**Checked clearances at M.**
- Top corner, every target: the bubble's y (210 on the 1.69, 172 elsewhere) is below K.y1 = 136. ✓
- Bottom corner, 1.69: K starts at y 144, so any bubble at y 8 or 34 clears it. ✓
- Bottom corner, 240-tall screens: K starts at y 104. A bubble at y 8 clears it with 1-3 lines. With the clock on (y 34), a 3-line bubble reaches y 110, 6 px into the keep-out gap. It is still clear of the actual blob unless a spike peaks at the top of the wander box. Accepted.

**Known tight cases (accepted, both under 2 s):**
1. **L in a bottom corner on 240-tall screens.** K starts at y 75, so 2-3-line bubbles overlap the top of the blob's wander zone. The bubble is drawn after the face, so it covers the blob briefly.
2. **A wide toast plus a 3-line bubble together, top corner, CoreS3.** The lifted bubble hits K.y1, and the toast draws over the bubble's lower rows for its 1.5 s.

---

## 4. Configuration (minimal set)

### 4.1 What ships as settings

| Setting | Values | Default | NVS key (namespace `vizbot`) | Type |
|---|---|---|---|---|
| `partyMode` | off / on | **off** | `"party"` | `putBool` |
| `partyCorner` | 0 TR, 1 BR, 2 BL, 3 TL (clockwise, so "next" walks around the screen) | **0 TR** | `"pCorner"` | `putUChar`, clamp > 3 → 0 on load |
| `partySize` | 0 S, 1 M, 2 L | **1 M** | `"pSize"` | `putUChar`, clamp > 2 → 1 on load |

All three go through `markSettingsDirty()` and are loaded and saved next to `"bgStyle"` in `settings.h`. That follows the same pattern as `kscope`.

### 4.2 Not settings (compile-time defaults in layout.h)

- `PARTY_SHADOW 1` and `PARTY_WANDER 1`. These are taste calls for the motion designer and Kevin to settle once on the bench. As user settings they would be two toggles nobody changes after day one. They're easy to promote later because the layout already reserves room for both.
- **No eyes on/off.** A blob without eyes is just the ambient scene, and that's already a scene. A true "no face" screensaver is a separate feature.

### 4.3 HTTP (house style: `/bot/<thing>?v=`, with `v=2` toggling like `/bot/time`)

```
GET /bot/party                     -> read only
GET /bot/party?v=1                 -> on   (v=0 off, v=2 toggle)
GET /bot/party?corner=bl           -> tr|br|bl|tl or 0-3; doesn't change on/off
GET /bot/party?size=l              -> s|m|l or 0-2
GET /bot/party?v=1&corner=tl&size=m

200 application/json  {"party":true,"corner":"tl","size":"m"}
```

- Add the flat keys `"party":true,"partyCorner":"tr","partySize":"m"` to `GET /state`, next to `"kscope"`.
- With `v=1` while `botBackgroundStyle != 4`: switch to style 4 and keep `effectIndex`, so the blob is visible on an effect. `v=0` leaves the scene as it is.

### 4.4 Touch UI

- **Settings → Look:** add one row, `uiAddRow("Party", ROW_STEP, RK_PARTY, ...)`, that steps **Off → Top right → Bottom right → Bottom left → Top left → Off**. One row covers both on/off and the corner. Turning it off doesn't forget the corner. Look goes from 6 to 7 rows (the max is 8). Toast on change: "Party: Top right", in `UI_C_LOOK`.
- Size has no touch row. It's set from the web or vizbrain. Kevin sets it once, and it keeps the last Look slot free.
- **Scene stepping** (swipe left/right on the face, or the scene sheet) **skips "Black" while party is on**, because a black blob on black is just two floating eyes.
- Tap anywhere still pokes, and the blob reacts in place (motion). Swipe up and long-press still open the dock, which suspends the party layout (section 2.4).

### 4.5 vizbrain (good fit)

- Add one kpow-mcp tool, `vizbot_party(on?: bool, corner?: "tr"|"br"|"bl"|"tl", size?: "s"|"m"|"l")`, which calls `GET /bot/party` and returns its JSON.
- Voice requests like "party mode", "move to the bottom left" or "get smaller" map straight onto it.
- No auto-trigger in v1. If Spotify should turn party on, that's a later decision.

---

## 5. Entering and exiting: what party mode *is*

**Recommendation: a face-layout flag on top of the ambient scenes.** It is not a new `botBackgroundStyle`, not a scene, and not a mood.

| Option | Verdict |
|---|---|
| **New `botBackgroundStyle` 5** | No. Seven places treat 4 as "ambient": `bot_mode.h:754` (frame skip), `bot_eyes.h:348` (stroke), `stackchan_leds.h:478` (ring follows the effect), `touch_ui.h:388` (`uiSceneIndex`), `settings.h:82` (clamps > 4 to 0), `bot_mode.h:996` (setter clamps to ≤ 4), and `web_server.h:1456` (`constrain 0-4`). Each one would need a "4 or 5" edit, and older firmware would read the saved 5 as black. |
| **A scene** | No. Scenes choose *which effect* plays. Party has to work with all 16 of them. |
| **A mood** | No. Moods and personalities drive expressions. Party must keep every expression working, since the eyes carry them. |
| **Flag `partyMode`, effective only while style == 4** | **Yes.** None of the seven places change, auto-cycle and every effect work unchanged, and the scene model stays "Black + 16 effects". |

**State rules:**

1. **On:** from the Look row, `/bot/party?v=1`, or vizbrain. It persists across reboots. If the scene is Black, it switches to the last effect.
2. **Off:** from the same three places. If the user picks the Black scene from the web UI while party is on, the flag stays set but is dormant, and the blob comes back on the next effect.
3. **Temporarily yields:** to any open sheet or page (normal mini face), to album art (the art circle moves to the party corner), and to info and weather mode (full screen as today). It returns to the blob when that ends.
4. **Stays in the corner:** while sleeping (Zzz re-anchored), and during voice turns (listening and talking are shown through the blob's motion, with the bubble placed by section 3).
5. **Power:** style 4 already disables pendant-mode frame skipping, so party mode costs what an ambient scene costs today, plus about 6k px of body fill and about 1.6k px of shadow darkening at M.

---

## 6. Proposed layout.h block (a proposal, not an edit)

```cpp
// ---- Party Mode: the face as a small blob in a corner (bot_party.h) -------
// The ambient effect fills the screen. The face is eyes only (brows at M/L,
// no mouth) inside a black blob with a soft shadow. Every blob size derives
// from the eye scale k, and BOT_EYE_R is 48 px on every board, so one tier
// table serves 240x280, 320x240 and 240x240. Corners: 0 TR, 1 BR, 2 BL, 3 TL.
#define PARTY_SCALE_S           0.30f  // legibility floor: X-eye strokes (0.14 R) are 2 px here
#define PARTY_SCALE_M           0.36f  // = ART_FACE_SCALE: same eyes as the album-art corner
#define PARTY_SCALE_L           0.44f
#define PARTY_BODY_PER_R        2.55f  // body radius r0 = 2.55 x eye R
#define PARTY_WOBBLE_PEAK       1.20f  // max radius incl. spikes, x r0
#define PARTY_WOBBLE_FLOOR_SIDE 0.97f  // min radius within 35 deg of horizontal (eyes reach 2.47 R on a pop)
#define PARTY_WOBBLE_FLOOR      0.85f  // min radius elsewhere
#define PARTY_SHADOW_DY         0.95f  // shadow ellipse centre below body centre, x r0
#define PARTY_SHADOW_RX         0.90f
#define PARTY_SHADOW_RY         0.30f  // shadow ends 1.25 r0 below centre
#define PARTY_WANDER_W          0.60f  // wander box, x r0, running inward from the tuck point
#define PARTY_WANDER_H          0.45f
#define PARTY_EDGE_GAP          2      // envelope (spikes, shadow) to the screen edge, px
#define PARTY_KEEPOUT_GAP       6      // overlays stay this far from the blob's keep-out box
#ifndef PARTY_SHADOW
  #define PARTY_SHADOW          1
#endif
#ifndef PARTY_WANDER
  #define PARTY_WANDER          1
#endif

// Rounded panel corners. The blob needs no extra inset while LCD_CORNER_R
// <= the smallest envelope (45 px, S): a disc wider than the corner arc that
// clears both flat edges never reaches the arc.
#if defined(BOARD_ESP32S3_LCD_169)
  #define LCD_CORNER_R          40     // estimate for the ST7789V2 1.69" module; verify on the bench
  #define PARTY_CLOCK_INSET     22     // clock pill x from the side edge, clear of the arc
#else
  #define LCD_CORNER_R          0
  #define PARTY_CLOCK_INSET     8
#endif
#define PARTY_TOAST_LOW_Y       (LCD_HEIGHT - 44)  // toast slot when the blob is up top (236 on 280: clears the swipe handle)
#define PARTY_BUBBLE_HIGH_Y     8                  // bubble when the blob is down low; 34 with the clock on

// Pre-rounded pixels per tier (r0, envelope, extent below centre, wander box)
struct PartyTier { float k; int16_t r, env, below, wanderW, wanderH; bool brows; };
static const PartyTier PARTY_TIERS[3] = {
  {PARTY_SCALE_S, 37, 45, 47, 22, 17, false},
  {PARTY_SCALE_M, 44, 53, 55, 26, 20, true },
  {PARTY_SCALE_L, 54, 65, 68, 32, 24, true },
};
#define PARTY_CORNER_RIGHT(c)   ((c) == 0 || (c) == 1)
#define PARTY_CORNER_TOP(c)     ((c) == 0 || (c) == 3)
// Tuck point: the blob centre's closest approach to its corner
#define PARTY_TUCK_X(t, c)  (PARTY_CORNER_RIGHT(c) ? LCD_WIDTH - PARTY_EDGE_GAP - (t).env : PARTY_EDGE_GAP + (t).env)
#define PARTY_TUCK_Y(t, c)  (PARTY_CORNER_TOP(c)   ? PARTY_EDGE_GAP + (t).env : LCD_HEIGHT - PARTY_EDGE_GAP - (t).below)
// Wander box runs inward from the tuck point; home = its centre
#define PARTY_HOME_X(t, c)  (PARTY_TUCK_X(t, c) + (PARTY_CORNER_RIGHT(c) ? -(t).wanderW : (t).wanderW) / 2)
#define PARTY_HOME_Y(t, c)  (PARTY_TUCK_Y(t, c) + (PARTY_CORNER_TOP(c)   ?  (t).wanderH : -(t).wanderH) / 2)
```

---

## 7. Notes for the implementer (layout-relevant only)

1. **Mouth off.** `renderBotFace` always draws the mouth (`bot_eyes.h:351`). Party mode needs a path that calls `botEyes.render(cx, cy, k, botFaceColor, true, now, tier.brows)` and skips `drawBotMouth`.
2. **Render order inside `renderBotMode` for style 4 with party on:** effect, shadow (darken), body (black), eyes, Zzz, overlays (placed per section 3), then `uiRenderOverlay`.
3. **Order relative to album art.** `artRender()` returns early before the background is drawn. Only its circle centre needs to follow `partyCorner`.

**Separate from party mode:** the swipe handle at `touch_ui.h:1307` is hard-coded to (104, 262), so on the 320×240 CoreS3 it is drawn off-screen. Worth a separate small fix.
