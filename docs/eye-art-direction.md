# vizBot eyes: art direction brief (round 1)

Audience: the designer building the browser eye lab, and whoever reviews the lab later.
Scope: the eyes only, plus how brows sit with the lids. Mouths stay as they are. Nothing here changes firmware.

References. vizBot: `vizbot/bot_eyes.h` (renderer), `vizbot/bot_faces.h` (expression table), `vizmini/bot_eyes.h` plus `vizmini/gfx_mono.h` (1-bit port). void: `app/static/index.html` (MIT, reference only). Line numbers below are `file:line`; void lines are in `index.html`.

---

## 1. North star

vizBot is a **flat, bold, two-colour face on a small physical screen**, seen at arm's length on a desk, often out of the corner of your eye. Every expression has to read **as a silhouette** in under a second, from 60 cm, in any face colour, and on a 128×64 1-bit OLED. void is a glossy orb with a glow, sparks, a halo and a body that sways. It earns its charm from **soft, spring-driven lids and pupils that never stop living**. We take void's *anatomy and motion* (lids, per-eye asymmetry, springs, how the pupils behave) and leave its *rendering* (gradients, glow, a 3D ball, colour pupils). The eyes should feel like they belong to a small, curious creature that has big round eyes and very expressive eyelids.

---

## 2. Keep / Borrow / Reject

**Keep (this is vizBot):**
1. Big near-round white eyes on black: about 0.4 of the screen width each. The aspect is a little wider than tall (`bot_faces.h:140`: 50×45).
2. Pupils are **cut-outs in the background colour** (`bot_eyes.h:28`), not a colour of their own. The default pupil is a round disc (no slits).
3. Thick brows as separate bars (`bot_eyes.h:742-774`). They stay, but the lids now do most of the emotional work (see §3.4).
4. The palette is user-configurable and limited to: background, `botFaceColor`, pupils = background. In ambient-background mode the face gets a 5 px black stroke (`bot_eyes.h:35, 451`).
5. Robot-only specials: GLITCH, the spiral, and X eyes.

**Borrow from void (specific):**
1. **Lid model** from `eyePts()` (`index.html:883-898`): the top lid has `top`, slant `sl` and middle sag `tc`; the bottom lid has `bot` and arch `bow`; there is a squash `sq` and a per-eye scale `es`. Lids **cut** the eye outline. They are not painted over it.
2. **Per-eye `[left, right]` values** on every lid and size parameter (`MOODS`, `index.html:802-841`). This is what makes skeptical, mischief and confused possible.
3. **Closed-eye morph** `shut` + `cup`: the oval melts into a "‿" arc (asleep) or a "^" arc (beaming) (`index.html:888-890`). It is per eye, so a wink is just `shut:[0,1]`.
4. **Pupil morph** from `pupilPts()` (`index.html:900-906`): one shape blends circle to heart (`hr`) to 4-point star (`st`). Also the presets `dot / small / big / plead / joy / peek / odd` (`index.html:785-797`).
5. **Time-based damped springs** on every parameter (`Spring`, `index.html:928-935`). The **mood "pop"** kicks scale, squash and pupil size (`index.html:943, 1006-1007`).
6. **Blink shape**: a fast close, a slower open, and an 18 % chance of a double blink (`index.html:1120-1122`). Also the **idle "change of look"** (`index.html:1065-1070`), **per-mood gaze behaviour** (`index.html:1097-1115`), and the **elliptical pupil clamp** (`index.html:1139-1141`).
7. **Eyes never merge.** Bigger eyes push apart (`index.html:1131`).
8. Pupils sit slightly **toward the nose** (`PUP.*.dx`), so he looks *at* you instead of through you. We use a much gentler version (§3.2).

**Reject (and why):**
1. Glow, halo, sparks, gloss, radial gradients, rim and translucency. These are alpha and blur on an RGB565 sprite, and they cost too much to read. They also don't exist in 1-bit.
2. The **sphere projection** `sph()` (`index.html:911-917`) and **whole-face tilt/rotation** (`tilt`). The Stackchan already has a real head to turn, and rotated ellipses are not a primitive on device.
3. **Slit pupils and the tall leaning "/ \" ovals** (`EYE.tilt`, `index.html:780`). They are void's identity, not ours.
4. **Coloured pupils and whites** (`LOOK.pupL`/`whiteL`). That breaks the cut-out identity and the 4-colour limit.
5. Body fidgets: `roll`, `chase`, `hum` and `stretch` (`index.html:1031-1046`) move a body we don't have. Only the eye-only fidgets carry over.

---

## 3. Parametric eye spec

One model draws every eye state. **All sizes are relative to `R`, the eye's half-width**, so the same numbers scale to the touch-UI chips (`bot_mode.h:714`) and to vizMini (×0.45). `H` is the full eye height, `2·R·aspect`. "Per-eye" means a value can be `[L, R]`. **L is the screen-left eye** (the one at `cx − spacing`).

### 3.1 Eye silhouette

| Param | Meaning | Units | Range | Default | Per-eye |
|---|---|---|---|---|---|
| `R` | eye half-width | design px at 240 wide | 40–56 | **48** | no |
| `aspect` | half-height ÷ half-width (void `sq`) | ratio | 0.55–1.15 | 0.92 | yes |
| `scale` | per-eye size (void `es`) | × | 0.85–1.12 | 1.0 | yes |
| `gap` | minimum horizontal gap between the two silhouettes | ×R | 0–0.3 | **0.08** | no |
| `lidTop` | top lid coverage from the top | ×H | 0–1 | 0.04 | yes |
| `slant` | top-lid tilt; **+ = inner corner down** (angry), − = inner corner up (sad) | ×H/2 at the corners | −0.6 – +0.5 | 0 | yes |
| `sag` | top lid droops in the middle (heavy, sleepy) | ×H | 0–0.3 | 0 | yes |
| `lidBot` | bottom lid coverage from the bottom | ×H | 0–0.6 | 0 | yes |
| `bow` | bottom lid arches up in the middle (smiling-eye crescent) | 0–1 | 0–0.5 | 0 | yes |
| `shut` | morph to a thin arc | 0–1 | 0–1 | 0 | yes |
| `cup` | arc direction: 0 = "‿", 1 = "^" | 0–1 | 0–1 | 0 | yes |
| `arcW` | arc stroke thickness when shut | ×R | 0.12–0.2 | 0.16 | no |

Eye centres are derived, not authored: `spacing = R·scale + gap·R/2`. The old fixed `eyeSpacing` goes away. When the eyes are at their biggest (`scale` 1.12), the face must stay within 0.92 of the screen width.

**Hard rule: the silhouette has exactly one vertical span per pixel column.** The span is `[max(ellipseTop(x), topLid(x)), min(ellipseBot(x), botLid(x))]`. The `shut` arc is one span too. This is what lets firmware draw an eye as about 100 `drawFastVLine` calls. Don't design anything that needs a hole or a second span in a column. The only exceptions are pupils and overlays (§3.3).

**Decision: stop the overlap.** Today the eyes overlap by 12 px (`bot_faces.h:95, 140`: W 50, spacing 44), so the two whites merge into one "goggle". With merged whites, inner-corner lid slant is impossible because the other eye fills the cut back in, and inner-corner slant is the single strongest emotional cue. The new default is nearly touching (a 4 px gap). The lab must keep a **"legacy overlap" A/B toggle** so Kevin can judge the trade.

### 3.2 Pupils

| Param | Meaning | Units | Range | Default | Per-eye |
|---|---|---|---|---|---|
| `pw`, `ph` | pupil half-width and half-height | ×R | 0.06–0.42 | 0.27 | yes |
| `inset` | shift toward the nose (mirrored) | ×R | 0–0.2 | **0.08** | yes |
| `pdy` | vertical bias | ×R | −0.2 – 0.2 | 0 | yes |
| `heart` | heart-ness | 0–1 | 0–1 | 0 | yes |
| `star` | 4-point-star-ness (`heart + star ≤ 1`) | 0–1 | 0–1 | 0 | yes |
| `spin` | star rotation | rad/s | 0–1.2 | 0.6 when `star > 0.5` | no |
| `gazeX`, `gazeY` | look direction | ×R | ±0.35 / ±0.25 | 0 | no (shared) |

**Presets** (the expression table names a preset and may override it):

| Preset | Values |
|---|---|
| `circle` | 0.27 |
| `small` | 0.18 |
| `dot` | 0.08 |
| `big` | 0.36 |
| `plead` | w 0.32, h 0.34, `pdy` +0.10 |
| `joy` | 0.30, `pdy` −0.12 (peeks over a raised bottom lid) |
| `peek` | 0.20, `pdy` −0.18 (tucked up under a low top lid) |
| `odd` | [0.16, 0.26] |
| `heart` | 0.36, `heart` 1 |
| `star` | 0.40, `star` 1 |

Pupil rules:
1. **Clamp elliptically** inside the *un-lidded* ellipse with a 0.06 R margin (void `index.html:1139-1141`). Today's box clamp (`bot_eyes.h:468-471`) lets a diagonal look poke the pupil through the outline.
2. **Lids occlude pupils. Pupils never dodge lids.** A pupil under a lid is simply cut off. Today the pupil is squeezed toward the centre on blink and pinned to y = +4 in `EYE_HALF` (`bot_eyes.h:649`).
3. Pupils are clipped to the silhouette. On a black background this is free. In ambient mode it is required.
4. Optional **catchlight**: a `botFaceColor` disc of 0.3 × pupil radius at (−0.35, −0.35) × pupil radius, in the **same direction in both eyes** (one light source). It is off when the pupil radius is under 7 px, off on mono, and off for heart, star and dot. Priority P2.

### 3.3 Special eyes: morph or overlay

| Today (`bot_faces.h:22-35`) | New | Kind |
|---|---|---|
| `EYE_NORMAL` | the model | morph |
| `EYE_HALF` | `lidTop` 0.4 (slant per expression) | morph |
| `EYE_CLOSED` (sleep) | `shut` 1, `cup` 0, "‿" | morph |
| `EYE_CARET`, `EYE_CURVED` | `shut` 1, `cup` 1, "^" (beaming) | morph |
| `EYE_WINK` | `shut` [0, 1] | morph |
| `EYE_DOT` | pupil `dot` | morph |
| `EYE_HEART` / `EYE_STAR` | `heart` 1 / `star` 1 | morph |
| `EYE_DIAMOND` (unused) | `star` 0.5 | morph |
| `EYE_X`, `EYE_SPIRAL`, `EYE_GLITCH` | glyph replaces the pupil, lids still apply | **overlay** |

Overlay rules:
1. Any discrete swap (overlay on or off, pupil to glyph) happens **under a blink**: the lids close, the swap happens while closed, then they open.
2. **X**: two strokes 0.14 R thick, arms 0.42 R from the centre.
3. **Spiral**: one *continuous stroke* 0.1 R thick, 1.75 turns, radius 0.7 × min(R, H/2), 0.5 rev/s, with left and right counter-rotating. Today it is 40 dots of r 2 (`bot_eyes.h:249-260`), which break apart on mono.
4. **Glitch**: shift *rows of the silhouette* by ±0.15 R in 3 bands, re-randomised every 120–250 ms, with the two eyes out of sync. Don't paint background-coloured bars.

### 3.4 Brows and lids

1. Lids carry the emotion and brows amplify it. Each brow follows its own top lid:
   - `browY = un-blinked eye top + 0.5·lidTop·H − 0.24R`
   - `browAngle = authored angle + 20° × slant`
2. Brows **don't follow the blink.** Today `browY` uses `effectiveEyeH` (`bot_eyes.h:743`), so brows drop about 40 px during a blink and vanish at `blink ≥ 0.8` (`:742`). New behaviour: brows dip 0.04 R at full close and stay visible.
3. The lab test: with brows **off**, every P1 expression must still read (§6).
4. Mouths are out of scope. Keep the current mouth types and params.

---

## 4. Motion rules

**Integrator.** Use a damped spring: `v += (ω²(target−x) − 2ζω·v)·h`, then `x += v·h`, where ω = 2πf. Step in sub-steps of 8 ms or less, using real elapsed time clamped to 50 ms (void `index.html:928-933`). Never animate per frame count: vizBot runs about 20 fps (33 ms `delay` plus render and push, `bot_mode.h:50`, `vizbot.ino:740`), vizMini runs 25 fps (`vizmini/config.h:73`), and pendant mode runs slower. Springs replace the linear lerp, and they also replace today's eye-mode snap at 50 % (`bot_faces.h:541-545`).

| Spring group | Params | f (Hz) | ζ | Feel |
|---|---|---|---|---|
| Shape | aspect, scale, all lids, shut, cup, inset, brows | **0.75 / T**, where T is the expression's `transitionMs` in seconds (150 ms → 5 Hz, 300 ms → 2.5 Hz, 400 ms → 1.9 Hz) | 0.85 | settles in ≈ T, under 1 % overshoot |
| Pupil shape | pw, ph, heart, star | 3.0 | 0.6 | about 10 % overshoot: the "pop" |
| Saccade | gazeX/Y | 6.0 | 1.0 | 90 % in about 100 ms (2 frames), no overshoot |
| Pursuit | gaze when IMU-driven, reading sweeps | 2.0 | 0.9 | smooth follow |

**Blink:**
- **Shape.** The top lid drops to `lidTop` 0.97 while the bottom lid rises +0.08, so the lids meet about 55–60 % of the way down, not at the centre. Height must not scale about the centre (today's version, `bot_eyes.h:414-417`).
- **Curve.** Close in 80 ms (smoothstep), hold fully closed for at least 30 ms (one frame or more), open in 140 ms (smoothstep). Total about 250 ms. Today the blink is a 150 ms linear triangle (`bot_eyes.h:52, 88-90`), which at 20 fps often misses the closed frame.
- **Timing.** The interval is uniform 2.6–6.8 s. There is an 18 % chance of a double blink, 320 ms start to start. A saccade larger than 0.25 R has a 25 % chance of triggering a blink.
- **Squash.** While closed, the eye is 4 % wider. On reopening, `aspect` overshoots by +3 %, peaking about 120 ms after open.
- **Where it applies.** Blinks work in every state with an open eye, including heart, star and overlays. Today blinks are blocked there (`bot_mode.h:609`). There are no blinks while `shut ≥ 0.5`, and none for the first 1.5 s of SHOCKED (it's a stare).

**Mood pop.** On an expression change, kick these springs, scaled by a per-expression `pop` value from 0 to 1:
- scale: peak +6 % at about 120 ms
- pupil size: peak +15 %
- aspect: a dip of −0.04, then a stretch

`pop` is 1.0 for surprised, shocked, excited, love and happy. It is 0.5 for mischief, winking, sassy, kissing and proud. It is 0 for sad, chill, focused, annoyed, worried and nervous: low-arousal moods don't bounce.

**Idle life.** This replaces today's 0.8–2.5 s drifting tweens (`bot_eyes.h:130-158`), which read as restless sliding.
1. **Saccades.** One every 2.4–6.2 s, to a uniform target within ±0.22 R horizontally and ±0.14 R vertically. 15 % of them return to the centre.
2. **Change of look.** Every 9–22 s, swap to the `dot`, `big` or `small` pupil for 1.8–4.5 s. 30 % of the time, play a 2.6 s mini-mood instead: curious (`lidTop` [0, 0.18], `scale` [1.06, 0.95]) or bored (`lidTop` 0.44).
3. **Eye-only fidgets.** One every 8–16 s, chosen from:
   - eyeroll: 1.7 s, `lidTop` 0.22, `small` pupils
   - peek: 2.3 s
   - glance sequence: 4.2 s
   - triple blink: 1.1 s
   - yawn: 3 s, only after 5 minutes idle
4. **Per-mood gaze patterns.** These replace idle saccades while the mood is active:

| Expression | Gaze pattern |
|---|---|
| thinking | a slow orbit up and to one side (0.15 Hz, 0.12 R) |
| nervous | darts every 260–500 ms (±0.15 R) |
| shy | holds down and to the side, glancing up every 3 s |
| focused | fixates, with a 0.05 R saccade every 6–10 s |
| annoyed | flicks to the side, holds 4 s, alternates |
| shocked | random tremble of ±0.03 R per frame |

**Low power / pendant mode** (`lowPowerMode`):
1. Saccades every 8–15 s. No fidgets and no changes of look.
2. Blinks every 4–9 s, with no doubles.
3. ζ = 1 everywhere, so there is no pop overshoot.
4. Spiral and star spin are frozen. Glitch becomes one static offset.
5. **Skip render and push entirely** whenever every spring is at rest (within 0.25 px and near-zero velocity) and no blink is running.

---

## 5. Expression targets

Notation: values are §3 params. `[a, b]` = [screen-left, screen-right]. "Reads wrong" comes from `bot_faces.h:136-412` and the renderer.

| # | Name | What reads wrong or flat today | Target treatment | Pri |
|---|---|---|---|---|
| 0 | NEUTRAL | Pupils dead centre in both eyes; no lids, so a blank stare. This is the face seen most often. | `lidTop` .04, `inset` .08, `circle`, idle life from §4 | **P1** |
| 1 | HAPPY | Eyes just get *bigger* (52×50), so it reads as mild surprise. The smile lives only in the mouth. | `lidBot` .35, `bow` .3, `joy` pupils, `scale` 1.03, pop 1 | **P1** |
| 2 | SAD | A shorter neutral with pupils down. The droop is only in the brows. | `lidTop` .22, `slant` −.45, `lidBot` .1, `bow` .3, `scale` .96, `plead`, gaze (0, +.2) | **P1** |
| 3 | SURPRISED | Close to SHOCKED (55×55 vs 55×52). A 150 ms lerp with no pop. | `aspect` 1.0, `scale` 1.1, `lidTop` 0, `small` → `dot` via the pupil spring, pop 1 | **P1** |
| 4 | CHILL | `EYE_HALF` is a flat rectangle chop (`bot_eyes.h:300-312`): black box in ambient mode, pupil pinned. | `lidTop` .38, `sag` .08, `slant` −.05, `lidBot` .06, `circle` `pdy` +.05, slow shape spring | **P1** |
| 5 | ANGRY | A squashed oval (52×32) plus V brows. With the brows hidden it reads bored. | `lidTop` .3, `slant` +.45, `lidBot` .12, `aspect` .85, `small`, pop 0.5 | **P1** |
| 6 | LOVE | Heart snaps in at 50 %. Brows hidden. | `heart` morph, size × (1 + .06·sin 7t) pulse, `lidBot` .25, `bow` .4 | P2 |
| 7 | DIZZY | Dotted spiral breaks apart on mono. Asymmetric brows only. | Stroke spiral overlay, swapped under a blink; `scale` [1.04, .94] | P2 |
| 8 | THINKING | Pupils fixed up-right; one brow does all the work. | `lidTop` [.1, .04], `slant` [−.1, .1], `small`, orbit gaze | P2 |
| 9 | EXCITED | Static star snaps in. | `star` morph, spin .6, `scale` 1.08, pop 1 | P2 |
| 10 | MISCHIEF | The comment says "one eye narrowed" (`:248`), but there is no per-eye param, so both eyes are identical. | `lidTop` [.32, .08], `slant` [.2, .05], `lidBot` [.15, .05], gaze (+.22, 0) | **P1** |
| 11 | SKEPTICAL | Same as MISCHIEF: the "narrowed eye" isn't possible today. | `lidTop` [.38, 0], `slant` [.15, −.05], `scale` [.95, 1.05], gaze (+.12, 0) | P2 |
| 12 | WORRIED | 52×50 with r11 pupils is nearly SURPRISED. | `lidTop` .12, `slant` −.35, `small`, `pdy` +.06, nervous darts at half rate | P2 |
| 13 | CONFUSED | Asymmetric brows and a side look only. | `lidTop` [0, .3], `slant` [0, .25], `scale` [1.06, .88], `odd` pupils | P2 |
| 14 | PROUD | 50×35 squash reads like a mild ANNOYED. | Beaming "^ ^": `shut` 1, `cup` 1, `scale` 1.04, pop 0.5 | P2 |
| 15 | SHY | Small pupils down-left; the eye shape is unchanged. | `lidBot` .3, `bow` .4, `scale` [1, .97], shy gaze | P2 |
| 16 | ANNOYED | 52×28 squash, no lid line. | `lidTop` .5, `slant` .03, `aspect` .88, `peek`, annoyed side-flick | P2 |
| 17 | FOCUSED | 48×38 is barely different from NEUTRAL. | `lidTop` .2, `slant` .08, `lidBot` .1, `small`, fixation | P2 |
| 18 | WINKING | The right eye is a full white ellipse with a black line through it (`bot_eyes.h:284-290, 720`), so it reads as a split eye, not a closed one. | `shut` [0, 1], `cup` [0, .7]; left eye `lidBot` .2, `bow` .3; pop 0.5 | **P1** |
| 19 | DEVIOUS | Same flat chop as CHILL. | `lidTop` .36, `slant` .3, `lidBot` .14, `bow` .2, `small`, gaze (+.15, 0) | P2 |
| 20 | SHOCKED | The dot is pinned at the centre; the 1 px glint is invisible. | `dot` with tremble, `scale` 1.1, `aspect` 1.02, no blink for 1.5 s, pop 1 | P2 |
| 21 | KISSING | The eyes are neutral; the mouth does everything. | `shut` 1, `cup` .8 (contented "^"), pop 0.5 | P2 |
| 22 | NERVOUS | Brows only. | `slant` −.2, `lidTop` .08, `small`, darting gaze, blink interval halved | P2 |
| 23 | GLITCHING | Paints black bars (`bot_eyes.h:327-340`), which shows as boxes in ambient mode. | Row-shift glitch overlay, eyes out of sync | P2 |
| 24 | SASSY | Neutral eyes plus a side look. | On entry: eyeroll fidget, then `lidTop` [.25, .15], gaze (+.2, −.05) | P2 |
| — | *sleep (state)* | `EYE_CLOSED` is full whites with a line through them (`bot_mode.h:615`). | `shut` 1, `cup` 0, the arc bobs 0.03 R at 0.2 Hz | P2 |

**P1 set (8): NEUTRAL, HAPPY, SURPRISED, SAD, ANGRY, CHILL, MISCHIEF, WINKING.** Together they cover the most-triggered faces (HAPPY 11 call sites, NEUTRAL 10, SURPRISED 9, MISCHIEF 7, CHILL 6). They also exercise every new mechanism: lids, slant in both directions, per-eye asymmetry, the bottom-lid crescent, the shut arc and pop.

---

## 6. Review scorecard

Score each P1 expression in the lab. An expression fails if it fails any criterion. Round 1 ships at 8 of 8.

| # | Criterion | Pass | Fail |
|---|---|---|---|
| 1 | **Eyes carry it.** Real size (lab "actual size" for a 1.69" panel), viewed at 60 cm, with brows and mouth hidden. | Kevin names the expression from the 25-name list within 2 s. | It needs the mouth or the brows to read. |
| 2 | **Silhouette distinct.** Both eyes as solid shapes, pupils off. | Overlap (IoU) with its nearest neighbour is below 0.90. Pairs to check: NEUTRAL/FOCUSED, HAPPY/PROUD, SAD/WORRIED, ANGRY/ANNOYED, CHILL/DEVIOUS, MISCHIEF/SKEPTICAL, SURPRISED/SHOCKED. | IoU is 0.90 or higher, or only the pupils differ. |
| 3 | **Survives 1-bit.** 128×64 threshold preview at ×0.45. | Every feature is 2 px or larger. Lid slant shows at least a 2 px corner difference. Still identifiable. | Features break into dots, or two P1s look the same. |
| 4 | **Transitions don't pop.** All 56 P1→P1 pairs plus blinks. | No eye's area changes by more than 25 % between consecutive frames (blinks excepted). Every discrete swap happens under a blink. Pupils stay inside the eye. The eyes never merge. | A visible snap, a pupil outside the outline, or the whites touching. |
| 5 | **Stays in palette.** Face colours white, cyan `#00FFFF`, amber `#FFB000`, pink `#FF6EC7`, each on black and on the ambient background with the 5 px stroke. | Only background and `botFaceColor` are used. Hard edges are the reference (anti-aliasing is allowed only behind a toggle). No black boxes in ambient mode. | Gradients, alpha, a third colour, or paint-over artefacts. |
| 6 | **Calm but alive.** 60 s of idle NEUTRAL. | 10–25 gaze moves, 9–23 blinks, no still gap over 7 s, no more than 1 fidget per 8 s. | It looks anxious (constant drifting) or dead. |

---

## 7. Hardware notes

1. **Redraw cost.** On the LCD boards, bot mode *already* redraws the whole frame into an off-screen `LGFX_Sprite`. It clears it every frame and calls `prevFrame.invalidate()` (`bot_mode.h:762-848`, line 837), so `BotPrevFrame`'s partial erase is effectively legacy. The real costs are:
   - The CPU fill per frame.
   - The SPI push: a 134 KB full-frame push at 240×280×16-bit is about 13–27 ms.
   - The fallbacks. Without PSRAM the canvas drops to 16-bit internal RAM, then to **8-bit (RGB332)**, which quantises `botFaceColor`. If allocation fails completely it draws direct, and then morphing shapes *will* flicker, because `BotPrevFrame` only tracks eye bounding boxes (`display_lcd.h:111-158`).
   - vizMini clears a 1 KB U8g2 buffer every frame and pushes it over software I2C.
   - Morphing doesn't need a new erase strategy here. It needs **span-based shapes**: about 200 vertical lines for both eyes, plus pupils.
2. **Math budget.** The ESP32-C3 (vizMini) has **no FPU**. The S3 has a single-precision one. That means at most 1 `sqrtf` per column, no per-pixel trig, no `double`, and spring state of about 30 floats.
3. **Mono (vizMini).** vizMini uses a copy of the same renderer (`vizmini/bot_eyes.h` differs only in the face centre), scaled ×0.45 to 128×64 (`vizmini/config.h:66-70`, `gfx_mono.h`). At the design scale:
   - minimum stroke 4.5 px (2 px on the OLED)
   - minimum pupil radius 7 px, except `dot`
   - no catchlight
   - heart and star pupils at 0.42 R or more
   - Gotcha for firmware: `fillCircle` with r < 1 draws a pixel before setting its colour (`gfx_mono.h:58`).
4. **Touch-UI chips** (`touch_ui.h:770-830`) redraw every expression with their own simplified code. The new model must be the single source, so the lab should also preview at chip scale (about ×0.25).
5. **Designer must not:**
   - renumber, rename or remove expressions. Indices are an API for vizbrain (`voice_client.h:559`) and `/expr?v=` (`web_server.h:1393`), so new ones go at the end only
   - paint the background colour over the eye to fake a lid. Lids change the outline.
   - use more than one span per column (pupils and overlays excepted)
   - use rotation, 3D, gradients, alpha or glow
   - time anything by frame count

**The lab must have:**
1. Per-param sliders with `[L, R]` linking.
2. An expression picker that shows both the old and the new face.
3. An A→B transition player that can slow down to 0.25×.
4. Actual-size, 1-bit 128×64, chip-scale and ambient-stroke previews.
5. The legacy-overlap toggle, a silhouette/IoU view, and an export of the full param table as JSON.
