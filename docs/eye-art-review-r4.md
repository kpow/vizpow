# vizBot eyes: art review, round 4 (the 17 P2 faces)

Reviewer: art director. Scope: the P2 faces in the eye lab, judged against `docs/eye-art-direction.md` §6 as amended in round 2 (IoU rule) and round 3 (startle window).
Method: Kevin's 7 saved tweaks loaded as **changed keys only** over the round 2 baseline. Every P2 face was rendered at rest on the 1.69" device (with and without brows and mouth), in 1-bit mono, in ambient (amber and pink), and as pupil-off silhouettes. I also rendered 51 transition filmstrips into and out of each face, including from P1 faces. The lab's own `measure()` ran over **all 600 pairs** (25 × 24) at 20 and 25 fps, with the first frame included. Every change was tested in a patched scratch copy, not in the lab. The frames are in `scratchpad/eyelab/review/r4_*`: `r4_p2_*` and `r4_before_*` show the lab as it is, and `r4_after_*` show it with the changes applied. The patched lab is `review/r4_lab_patched.html`, and the diff is `scratchpad/eyelab/r4.diff` (it dry-runs clean against `eye-lab.html`).

**Verdict: the P2 set is the same character as the revised P1s.** The faces are built from the same lid vocabulary, and the lab as it is fails mostly in motion. One cause sits behind most of it: SHOCKED (the biggest eye) and ANNOYED (the smallest open eye) are 3.2× apart in area, and SHOCKED's 100 ms spring makes that change in one frame. Three faces are too close to a neighbour at rest (CONFUSED, KISSING, NERVOUS). The glitch overlay is too timid to read on mono.

## 1. Per-face verdict (lab as it is, Kevin's tweaks loaded)

Criterion 1 is my provisional read; the official tick stays with Kevin. Criterion 6 (idle NEUTRAL) doesn't apply to P2. The "After" column is with §2 applied.

| Face | 1 Eyes | 2 Silhouette (nearest IoU) | 3 1-bit | 4 Transitions | 5 Palette | Read | After |
|---|---|---|---|---|---|---|---|
| LOVE | pass | flag ok (SHY .904) | weak: heart .36 < .42 mono min | pass | pass | Hearts read at once; Kevin's open top lid is right. The heart is small on the OLED. | pass |
| DIZZY | pass | flag ok (NEUTRAL .905, asym) | pass | weak: spiral cuts the closing lid into dashes | pass | Continuous spirals survive mono. | pass |
| THINKING | pass | pass (SKEPTICAL .879) | pass | **fail** (→SHOCKED .51) | pass | Squint + big eye + orbit gaze says "hmm". | pass |
| EXCITED | pass | pass (SURPRISED .876) | weak: star .40 < .42 | pass | pass | Stars plus Kevin's raised inner corners read as eager. | pass |
| SKEPTICAL | pass | pass (THINKING .879) | pass | **fail** (→SHOCKED .58; from ANNOYED .26, DEVIOUS .25) | pass | Kevin's odd pupils sell it. | pass |
| WORRIED | pass | flag ok (NEUTRAL .924, corners 5.4 px) | pass | pass | pass | Clear, but NERVOUS is a smaller copy of it. | pass |
| CONFUSED | weak | pass (NEUTRAL .74) | pass | **fail** (→SHOCKED .59; from ANNOYED .25) | pass | A mirror image of SKEPTICAL: the narrowed eye has the same inner-down lid. | pass |
| PROUD | pass | pass (HAPPY .14) | pass | pass | pass | Beaming ^ ^, distinct from HAPPY's open crescent. | pass |
| SHY | weak | flag ok (LOVE .904) | pass | **fail** (→SHOCKED .51) | pass | A lighter HAPPY; only the gaze says shy. | pass |
| ANNOYED | pass | pass (ANGRY .58, CHILL .815) | pass | **fail** (8 pairs, worst →SHOCKED .66) | pass | An iconic flat half-lid. Because it is the smallest open eye, everything leaving it pops. | pass |
| FOCUSED | pass | pass (SASSY .88) | pass | pass | pass | Determined squint. | pass |
| DEVIOUS | pass | pass (ANGRY .814) | pass | **fail** (→SHOCKED .64, →SURPRISED .52, →SKEPTICAL .25; from SHOCKED .26) | pass | The grin crescent reads as scheming. | pass |
| SHOCKED | pass | pass (SURPRISED .862) | **fail**: brows leave the OLED as a 1 px sliver | **fail** (11 entries at 51–66 %; exits to ANNOYED .33, ANGRY .28) | pass | The tall stare reads; the entry is a teleport. | pass |
| KISSING | **fail** | pass (PROUD .59, same ^ shape) | pass | pass | pass | The eyes are a flatter PROUD; only the mouth says kiss. | weak (A4) |
| NERVOUS | **fail** | pass (WORRIED .823) | pass | pass | pass | At rest a shrunken WORRIED. Kevin's tweak removed the lower lid that set it apart. | pass |
| GLITCHING | weak | **fail** by rule (NEUTRAL 1.0) | **fail**: shifts are 1–3 OLED px | pass | pass, no boxes | Rows shift but the pupil stays round, so mono reads it as a ragged NEUTRAL. | pass (A1) |
| SASSY | pass | flag ok (WORRIED .911, asym) | pass | pass | pass | Entry eyeroll plus side-eye. | pass |

As it is: 4 of 17 pass every criterion (PROUD, FOCUSED, WORRIED, SASSY). The P2 pairs also expose P1 failures: CHILL, ANGRY and MISCHIEF→SHOCKED, ANNOYED→HAPPY, SHOCKED→ANGRY and SURPRISED→ANNOYED.

## 2. Change list (old → new)

**Kevin's keys are untouched.** Changes marked **K** touch one of his tuned faces through keys he did not change.

| # | Where | Change | Why |
|---|---|---|---|
| M5 | `buildParams`, `blinkStart`, `afterSetExpr` | New SPEC flag `exitBlink`. `buildParams`: `p.exitBlink = !!o.exitBlink`. `blinkStart`: `why === 'exit'` bypasses `blinkAllowed`, like `'swap'`. `afterSetExpr` (non-instant): read `prevExpr = AS.expr` before overwriting it, then before `requestSwap`: `if (AP[prevExpr].exitBlink && prevExpr !== i && AS.blink.ph === 0) blinkStart(now, 'exit');` | Leaving the two size extremes happens under a blink. Shock released by a blink is natural, and annoyed's slow blink is the "ugh". One flag fixes 8 pairs. |
| S12 | SHOCKED | `transitionMs` 100 → **200**; add `exitBlink: 1` | At 100 ms, 9 faces open by 51–66 % in one frame. At 200 ms it opens in 3 frames inside the startle window (CHILL: 42 / 29 / 11 %). |
| S13 | SURPRISED (P1) | add `transitionMs: 170` (was 150) | DEVIOUS→SURPRISED goes from 51.7 % to 48 %. It still opens in 2 frames; nobody will see 20 ms. |
| S14 | ANNOYED | add `transitionMs: 350` (was 250) and `exitBlink: 1` | SURPRISED→ANNOYED goes from .289 to .217. A slow lid drop is the right tempo for a pop-0 face. |
| S15 **K** | SKEPTICAL | `pop` .3 → **0** | DEVIOUS→SKEPTICAL goes from .254 to .244. A raised-eyebrow appraisal doesn't bounce. |
| S16 **K** | `PRESETS` (LOVE, EXCITED) | `heart` .36 → **.42**; `star` .40 → **.42** | Meets the brief's §7.3 mono minimum. The heart gains the most (`r4_variants_mono`). Only these two faces use the presets. |
| S17 | KISSING | `cup` .8 → **.2** | ‿ ‿ (eyes closed, savouring) instead of a flatter copy of PROUD's ^ ^. |
| S18 | CONFUSED | `slant` [0, .25] → **[0, −.2]**; add `gy: −.1` | The narrowed eye now tilts up toward the nose (puzzled) instead of down (suspicious). It stops mirroring SKEPTICAL, and the upward look says "huh?". |
| S19 | SHY | add `lidTop: .16` | A lowered top lid over the cheek crescent makes the down-left look demure. LOVE IoU goes from .904 to .794. |
| S20 **K** | NERVOUS | add `aspect: 1.05` and `pw`/`ph` **.16** (was `small` .18) | Wide, round, pinprick eyes (wired) versus WORRIED's droop, without bringing back the lower lid Kevin removed. At 7.2 px the pupils meet the mono minimum. |
| O1 | glitch: `stepAfter` bands, `renderAfter` | `dx` `rnd(.05, .15)` → **`rnd(.1, .2)`** R. Keep an unshifted copy of each eye mask and clip pupils to it. Then shift the pupil rows by the same bands (within that eye's half) and AND the result with the shifted silhouette. | The eye now reads as sliced on mono (`r4_after_film_overlays_mono`). Firmware: a row memmove in the eye rect after the pupils are drawn. |
| O2 | `renderAfter`, X and spiral | Skip the glyph when `g.pup < .3`; otherwise scale the X arm and the spiral `rmax` by `g.pup`. | Glyphs close with the lids, like pupils do. The spiral no longer dashes the lid line during the swap blink. |
| O3 | `renderAfter`, brows | Skip a brow if `min(y0, y1) − half < 0`. | Removes SHOCKED's 1 px brow sliver on mono. Brows are optional there. |

13 changes, 3 of which (S15, S16, S20) touch 4 of Kevin's tuned faces: LOVE, EXCITED, SKEPTICAL and NERVOUS. One P1 changes: SURPRISED's spring, by +20 ms.

## 3. Rule amendments

- **A1, §6.2 overlay faces.** Score DIZZY and GLITCHING on the overlay, not on the rest silhouette, which is NEUTRAL by design (GLITCHING IoU 1.0). The lab already skips them as IoU neighbours. Pass: the glyph is legible at actual size and on mono (stroke or shift ≥ 2 OLED px), is swapped under a blink, and closes with the lids.
- **A2, §4 exit blink.** A face may end with a blink. Leaving it starts a blink immediately, even inside the next face's no-blink window, and §6.4 treats it like any blink. Consequence: after ANNOYED→SHOCKED, the stare opens out of a double-take blink, and its 1.5 s no-blink window effectively shrinks to about 1.2 s.
- **A3, §6.4 tempo guidance (not a pass rule).** Below about 170 ms, a shape spring is a teleport at 20 fps from the narrow faces. Fast faces should rely on the startle window and pop, not on shorter springs.
- **A4, §6.1 mouth-led faces.** No eye-only kiss cue exists. KISSING passes criterion 1 if the eyes read as closed and content and are distinct from PROUD; the mouth names it. Kevin may reject this and keep KISSING a fail.

## 4. Measured after the changes

The figures below come from the patched copy with Kevin's tweaks as changed keys only, using the lab's own `measure()`, first frame included.

| | Lab as it is | After |
|---|---|---|
| Pairs run | 600 | 600 |
| Worst area step, 20 fps | **65.8 %** ANNOYED→SHOCKED | **24.4 %** DEVIOUS→SKEPTICAL |
| Failing pairs, 20 fps | 19 | **0** |
| Worst step and failures, 25 fps | 62.6 %, 8 fail | 22.2 %, 0 fail |
| Min gap / pupils outside / swaps not under a blink | 4 px / 0 / 0 | 4 px / 0 / 0 |
| IoU flags | NEUTRAL–WORRIED .924, SASSY–WORRIED .911, DIZZY–NEUTRAL .905, LOVE–SHY .904 (all ok by corners or asymmetry); GLITCHING–NEUTRAL 1.0 fails | The first three remain and are ok. LOVE–SHY is gone (.794). GLITCHING passes under A1. |

Every P1→P1 pair still passes (worst is SURPRISED→ANGRY at 24.3 %).

**Left open.**
1. KISSING passes criterion 1 only under A4.
2. For the mouth pass: SASSY's tongue is red, which is a third colour, and SHOCKED's open-O mouth clips the bottom of the OLED.
3. Every swap blink shows one frame of a flat letterbox at b ≈ .5 on the way open. This is the round 2 closing path; it is not a fail at 20 fps, but Kevin may notice it on LOVE, EXCITED, DIZZY and GLITCHING entries.
4. THINKING, SHY and ANNOYED lean on their gaze patterns, so judge criterion 1 in the lab with idle on.

**Decision (2026-10-24):** Kevin accepted A4. KISSING passes criterion 1 as a mouth-led face; ticked in the eye lab.
