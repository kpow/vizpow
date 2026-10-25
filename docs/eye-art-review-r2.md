# vizBot eyes: art review, round 2

Reviewer: art director. Scope: the "after" model in the eye lab (`scratchpad/eyelab/eye-lab.html`), judged against `docs/eye-art-direction.md` §6.
Method: rendered every P1 at rest on the 1.69" device, with and without brows and mouth, in 1-bit mono, and in ambient mode with amber. Also rendered blink frames, transition filmstrips, all 56 P1→P1 pairs at 20 fps, and 60 simulated minutes of idle NEUTRAL. Every proposed change below was checked in a patched scratch copy of the lab, not in the lab itself. The frames are in `scratchpad/eyelab/review/`: `p1_*` = lab as built, `r2_*` = with the changes applied.

**Verdict: the model is right. The faces need small changes.** Lids, per-eye asymmetry and the cut-out pupils already read better than the old faces. SAD, ANGRY and CHILL pass as they are. The two biggest problems are in motion, not in the faces: a blink leaves the eye **completely empty** on the closed frame, and the wink closes as a squashed lens.

---

## 1. Verdict per P1 expression (lab as built)

Criterion 1 is my provisional read at actual size. The official tick stays with Kevin.

| Expr | 1 Eyes carry it | 2 Silhouette | 3 1-bit | 4 Transitions | 5 Palette | 6 Idle | Read |
|---|---|---|---|---|---|---|---|
| NEUTRAL | pass | **fail** (THINKING .976) | pass | pass | pass | **fail** (blinks up to 26/min, gaze up to 27/min) | Friendly and alive. The pupil inset works. |
| HAPPY | weak | pass (SHY .863) | weak | pass* | pass | — | Reads as "pleased squint". The bottom crescent is too flat to read as a smile, especially on mono. |
| SURPRISED | pass | **fail** (SHOCKED .977) | pass | marginal (→ANGRY .26) | pass | — | Strong. Dot pupils in big round eyes say "whoa". |
| SAD | pass | pass (.812) | pass | pass* | pass | — | The best face in the set. The inner-up slant plus the plead pupils carry it with no brows. |
| ANGRY | pass | pass (DEVIOUS .897) | pass | marginal | pass | — | Unmistakable V even without brows. |
| CHILL | pass | pass (.815) | pass | pass* | pass | — | Heavy lid with the pupil cut by it: relaxed, not flat-chopped. Good. |
| MISCHIEF | weak | pass (SASSY .859) | pass | pass* | pass | — | Reads as suspicious or skeptical rather than sly. The side-eye is too small to see (0.22 R minus the inset). |
| WINKING | pass | pass | **fail** (arc ends are 1 px on the OLED) | **fail** (shut eye steps 47–58 % per frame; the mid-close frame is a "lemon with a dot") | pass | — | At rest the closed eye is a flat dash, not a wink. |

\* These fail only in pairs with WINKING (see conflict 3).
**All blinks fail the brief's intent.** In both blink modes the closed frame draws nothing (`blink_brief.png`, `blink_intent.png`), so at 20 fps a blink is a one-frame blackout flash. With brief numbers, ANGRY and SAD also collapse to corner slivers at b = 0.8.

After the change list (`r2_*.png`): all 8 pass criteria 2–6 by measurement. P1 nearest-neighbour IoU is at most .876 (SURPRISED→EXCITED) except NEUTRAL→WORRIED .924 (see conflict 2). The only P1→P1 steps over 25 % are the closing eye in WINKING pairs; the open eye peaks at .15. Idle runs 11–21 blinks/min, 12–23 gaze moves/min, and the longest still gap is 6.6 s.

---

## 2. Conflict decisions

1. **Blink: neither mode. Use one closing path for blink and `shut`.**
   - The top lid travels to **0.58 H** and the bottom lid to **max(lidBot, 0.42) H**, so the lids meet 58 % down.
   - Over the last part of the close, the eye morphs into the `shut` "‿" arc, so the held frame shows a lid line, never an empty eye.
   - Lid travel is `smoothstep(bump(c, 0, .7))`. The arc morph is `smoothstep(bump(c, .4, 1))`, where `c = max(blink, shut)`.
   - Timing stays 80 / ≥30 / 140 ms, and the +4 % width squash is unchanged. Remove the `ui.blinkMeet` toggle.
2. **IoU: fix the two pairs with params, then narrow the criterion.**
   - SHOCKED becomes a taller oval: IoU vs SURPRISED drops to .862. THINKING gets a real one-eye squint: vs NEUTRAL < .90, nearest neighbour now SKEPTICAL .879.
   - The fix is not to call pupils the separator. SURPRISED vs SHOCKED was exactly the "only the pupils differ" case §6 forbids.
   - Narrowing: IoU < .90 is required for the named pairs and for P1 vs P1. An IoU ≥ .90 elsewhere is a **flag for an eyeball check**, not an automatic fail. It passes if the lid corners differ by ≥ 2 OLED px or the face is asymmetric. Reason: a slanted lid moves few pixels but changes the read completely. NEUTRAL→WORRIED (.924) is such a case, and it is plainly distinct.
3. **Transitions: don't slow the springs. Changes in `shut` count as blinks.**
   - With conflict 1's closing path, a wink is a one-eye blink, and the blink exemption applies to that eye only. The open eye stays bound by the 25 % rule; it peaks at .15.
   - SURPRISED is not the culprit: its only over-limit pair is →ANGRY at .26. Fix that by giving ANGRY a 230 ms shape spring.
   - I measured 15 failing pairs at 20 fps, not 20: 14 wink pairs plus SURPRISED→ANGRY.
4. **Idle blinks: any blink restarts the auto timer.**
   - Double, saccade and fidget blinks currently add to the auto blinks instead of replacing them. After any blink, set `next = now + rnd(2600, 6800)/rate`.
   - Saccade-triggered blinks also need ≥ 1.5 s since the last blink.
   - Gaze moves also overshot (up to 27/min), so the idle saccade interval moves to 2.8–6.6 s.
   - Result over 60 simulated minutes: blinks 11–21/min (mean 16), gaze 12–23/min, longest still gap 6.6 s.
5. **Pop guesses.**
   - Confirm: THINKING 0, SKEPTICAL .3, CONFUSED .5, SHY 0, GLITCHING 0.
   - Replace: NEUTRAL .3→**0** (the face entered after every other face; a bounce on each return reads as a hiccup). DIZZY .5→**0** (the pop lands under the swap blink, so it is never seen). DEVIOUS .3→**0** (scheming is slow).

---

## 3. Change list (old → new)

### Motion and geometry
| # | Where | Change | Why |
|---|---|---|---|
| M1 | `eyeGeom` | Replace the `ltB`/`lbB` lines with the code below. Return `shut: arcK` and `pup: 1 - Math.max(lidK, arcK)`. In `renderAfter`, scale `rx`/`ry` by `g.pup` instead of `(1 - g.shut)`. | Blinks and winks close like lids and end on a lid line. Pupils shrink as the lids close, so they don't notch the thin arc. |
| M2 | `GDEF.arcW`; `rasterEye` | `arcW` .16 → **.18**; arc taper `(.3 + .7 * q)` → **`(.55 + .45 * q)`** | Arc ends become ≥ 2 px on the OLED (they were 1 px). |
| M3 | `blinkStart`, `newSacc` | In `blinkStart`: `B.last = now; if (why !== 'auto') B.next = now + (ui.pendant ? rnd(4000, 9000) : rnd(2600, 6800) / rate);`. In `newSacc`, add the condition `now - AS.blink.last > 1500` before the 25 % saccade blink. | Caps blinks at 23/min. |
| M4 | `updateGaze` default | Idle saccade interval `rnd(2400, 6200)` → **`rnd(2800, 6600)`** | Gaze moves stay at 25/min or below; the still gap stays under 7 s. |

```js
const close = Math.max(bE, live ? shut : 0), lidK = smooth(bump(close, 0, .7)), arcK = live ? smooth(bump(close, .4, 1)) : shut;
const ltB = lt < .58 ? lt + (.58 - lt) * lidK : lt;
const lbB = lb + (Math.max(lb, .42) - lb) * lidK;
```

### P1 SPEC
| # | Expr | Change | Why |
|---|---|---|---|
| S1 | HAPPY | `bow` .3 → **.5** | Makes a real smiling crescent; it now survives mono. |
| S2 | MISCHIEF | `gx` .22 → **.32**; `lidBot` [.15, .05] → **[.2, .12]**; `bow` → **[.35, .35]** | A visible side-eye plus raised cheeks read as "sly" rather than "suspicious", and separate it from SKEPTICAL. |
| S3 | WINKING | `cup` [0, .7] → **[0, 1]** | The closed eye reads as a cheeky "^" instead of a dash. |
| S4 | ANGRY | add `transitionMs: 230` to its SPEC row (was 200 from `bot_faces`) | SURPRISED→ANGRY drops below the 25 % step. |
| S5 | NEUTRAL | `pop` .3 → **0** | Conflict 5. |

### P2 faces that now look wrong next to the revised P1s
| # | Expr | Change | Why |
|---|---|---|---|
| S6 | SHOCKED | `aspect` 1.02 → **1.12**, `scale` 1.1 → **1.12**, add `lidTop: 0` | It was a copy of SURPRISED; now a tall, lidless stare. |
| S7 | THINKING | `lidTop` [.1, .04] → **[.24, 0]**, add `lidBot: [.1, 0]`, `scale: [.96, 1.06]` | It was NEUTRAL plus a gaze; now "hmm". |
| S8 | NERVOUS | `slant` −.2 → **−.3**, add `lidBot: .14`, `scale: .94` | It was .968 vs NEUTRAL; now tense and shrunk. |
| S9 | DEVIOUS | `lidBot` .14 → **.22**, `bow` .2 → **.45**, `pop` .3 → **0** | It looked like ANGRY (.897); the grin crescent makes it scheming (.814). |
| S10 | ANNOYED | add `pdy: .02` (overrides `peek`'s −.18) | The `peek` pupil left a small notch on the lid line; now a half-pupil shows under the lid. |
| S11 | DIZZY | `pop` .5 → **0** | Conflict 5. |

Lab-only bugs, not design:
1. An instant `setExpr` snaps the springs before `gxOut`/`gyOut` refresh, so the first frame shows the old gaze.
2. A forced (SASSY entry) eyeroll that has expired survives an instant `setExpr` into the next face.

---

## 4. Brief amendments (not applied)

1. §4 Blink shape: replace "top lid to 0.97, bottom +0.08" with the closing path in M1 (meet at 0.58 H, end on the ‿ arc).
2. §4 Blink timing: "any blink restarts the auto interval; saccade blinks need ≥ 1.5 s since the last blink". Idle saccades every 2.8–6.6 s.
3. §4 Mood pop: add the five confirmed values and the three replaced ones from conflict 5.
4. §3.1: `arcW` default .18, arc ends ≥ 0.55 of the centre thickness. §3.2 rule 2: "pupils scale with closure". This is occlusion, not dodging.
5. §5 rows: S1–S3 and S6–S10. SURPRISED keeps `dot`; SHOCKED gets the tall oval.
6. §6.2: the IoU rule is narrowed as in conflict 2. §6.4: `shut` changes are blink-class, and the rule applies to the open eye.
7. Out of scope but now visible: the OPEN_O mouth sits tight under the taller SHOCKED eyes, and on mono the SURPRISED mouth clips the bottom edge. Flag this for the mouth pass.

---

## 5. Round 3: SURPRISED entry

**What round 2 missed.** My pair runner compared each frame with the frame before it, but it never compared the first frame after the switch with the resting source face. The lab's own monitor does. Counting that frame, 6 P1→SURPRISED pairs fail the 25 % rule, and all of them fail on the first frame: CHILL 47.5 %, MISCHIEF 46.5 %, ANGRY 43.6 %, SAD 37.5 %, HAPPY 35.6 %, WINKING 28.1 %.

**Decision: keep the 150 ms spring and pop 1 on SURPRISED. Amend the rule with a startle window. No motion change, no firmware change.**
- The fast open *is* the expression. The 50 ms frame is a real in-between: lids part-way up, the eye already rounder. It is about 60 % of the way, then 100 ms is nearly open and 150 ms settles. That reads as a startle, not a snap (`review/r3_film_*_surprised.png`).
- A spring slow enough to pass the old rule needs more than 350 ms (300 ms: 27.4 %, 350 ms: 25.5 %). On screen that is a slow lid lift, which reads as "waking up" or "oh, interesting", not "whoa" (`review/r3_alt_chill_surprised_350ms.png`).
- The arithmetic leaves no room. CHILL→SURPRISED grows the eye 2.5×. At 25 % per frame that takes at least 4 frames (200 ms) even with perfect geometric pacing, and a spring is not perfect.
- Rejected:
  - Two-stage entry: it slows the attack, which is the wrong end to slow.
  - Area or velocity clamp: firmware would need per-frame area maths, and the open becomes a linear ramp.
  - Pop tuning: the pop is only 2–3 points of the step.
  - Entry pupil: pupils are not part of the area.

**Rule amendment (§6.4).** Add: "Startle entry: for the first 100 ms of spring time after switching into SURPRISED or SHOCKED, an eye may open by up to 50 % of its area per frame. A shrinking step, a step over 50 %, or any step after 100 ms still counts." The window is in time, not frames, as §4 requires. At 20 fps it covers 2 frames; at 25 fps it covers 2.

**Measured** (all 56 P1→P1 pairs, 20 fps, first frame included, the lab's own `measure()` on the patched copy):

| | Lab as is | With the amendment |
|---|---|---|
| Round 2 baseline | 47.5 % CHILL→SURPRISED (6 pairs fail) | **24.3 %** SURPRISED→ANGRY, 0 fail |
| Kevin's 7 saved tweaks loaded | 46.5 % MISCHIEF→SURPRISED (6 pairs fail) | **24.3 %** SURPRISED→ANGRY, 0 fail |
| Same, 25 fps (vizMini) | 42.2 % / 41.4 % | 20.9 % / 20.9 % |

The largest step the window allows is 47.5 %, under the 50 % cap. Min gap is 4 px and no pupil escapes. Kevin's tweaks cause no failures.

**Notes for the P2 pass (not fixed here):**
- ANNOYED→SURPRISED opens 53 % in the first frame.
- SHOCKED's 100 ms spring opens up to 60 % from P1 faces (CHILL) and 66 % from ANNOYED. A 150 ms spring brings that to 58 %.
- So these pairs still fail under the cap. That is the intended result: they are close to a teleport.

**Lab change.** Applied to the current `eye-lab.html`, this dry-runs clean with `patch -p1 < r3.diff` or `patch eye-lab.html < r3.diff`. The same diff is saved as `scratchpad/eyelab/r3.diff`. It adds a spring-time entry clock, exempts the startle window in `measure()`, and updates the c4 scorecard text.

```diff
--- a/eye-lab.html
+++ b/eye-lab.html
@@ -821,7 +821,7 @@
   eyes: [{}, {}], brow: [{}, {}],
   gx: new Spring(0, 6, 1), gy: new Spring(0, 6, 1),
   popS: new Spring(0, F_POP, 1), popA: new Spring(0, 2.2, .45), bStr: new Spring(0, F_POP, 1),
-  expr: 0, enteredAt: 0, overlay: 'none', brows: true, pending: null,
+  expr: 0, enteredAt: 0, entryT: 1e9, overlay: 'none', brows: true, pending: null,
   blink: { ph: 0, t: 0, b: 0, next: 0, last: -1e9, queue: [] },
   gaze: { tx: 0, ty: 0, mode: 'sacc', next: 0, side: 1, up: false, micro: false },
   idle: { nextLook: 0, look: null, nextFid: 0, fid: null, lastFid: '' },
@@ -1024,7 +1024,7 @@
   return T;
 }
 function afterSetExpr(i, now, instant) {
-  AS.expr = i; AS.enteredAt = now;
+  AS.expr = i; AS.enteredAt = now; AS.entryT = 0;
   const P = AP[i], E = EXPR[i];
   AS.idle.look = null;
   const fid = AS.idle.fid;
@@ -1054,7 +1054,7 @@
   if (P.entry === 'eyeroll') startFidget(now, 'eyeroll', true);
 }
 function stepAfter(now, dt) {
-  dt = Math.min(dt, .05);
+  dt = Math.min(dt, .05); AS.entryT += dt;
   const P = curP(), E = EXPR[AS.expr], lp = ui.pendant;
   updateIdle(now); updateGaze(now); updateBlink(now);
   const T = targets(now);
@@ -1424,6 +1424,9 @@
 }
 
 // ------------------------------------------------------------------ measurements
+// Round 3: a startle may open fast. For the first 100 ms of spring time after entering SURPRISED or SHOCKED,
+// an opening step up to 50 % is allowed; a shrinking step, a step over 50 % or anything after 100 ms still counts.
+const STARTLE = new Set([3, 20]);
 function measure(now, m) {
   const blinkNow = AS.blink.ph !== 0;
   // Review conflict 3: a change in shut is blink-class, so it exempts that eye only. The open eye still counts.
@@ -1433,7 +1436,9 @@
     for (let e = 0; e < 2; e++) {
       if (shutMove[e] || mon.prevShutMove[e]) continue;
       const a = m.area[e], p = mon.prevArea[e], den = Math.max(a, p);
-      if (den > 40) { const st = Math.abs(a - p) / den; if (st > mon.maxStep) { mon.maxStep = st; mon.worst = mon.label || EXPR[AS.expr].name; } }
+      if (den > 40) { const st = Math.abs(a - p) / den;
+        if (STARTLE.has(AS.expr) && AS.entryT <= .1 + 1e-6 && a > p && st <= .5) continue;
+        if (st > mon.maxStep) { mon.maxStep = st; mon.worst = mon.label || EXPR[AS.expr].name; } }
     }
   }
   mon.skipNext = false; mon.prevBlink = blinkNow; mon.prevArea = m.area.slice(); mon.prevShut = shutNow; mon.prevShutMove = shutMove;
@@ -1538,7 +1543,7 @@
   ['Eyes carry it', 'Actual size, 60 cm, brows and mouth hidden: you name it from the 25 within 2 s.', 'c1'],
   ['Silhouette distinct', 'Pupils off. IoU below 0.90 for the named pairs and P1 vs P1. IoU 0.90 or more elsewhere is an eyeball flag, not a fail: it passes if the lid corners differ by ≥ 2 OLED px or the face is asymmetric.', 'c2'],
   ['Survives 1-bit', '128×64 at ×0.45: features ≥ 2 px, lid slant shows ≥ 2 px corner difference.', 'c3'],
-  ["Transitions don't pop", 'No eye area step over 25 % per frame. Blinks and shut changes are excepted for the eye that closes; the open eye still counts. Swaps under a blink. Pupils inside. Never merge.', 'c4'],
+  ["Transitions don't pop", 'No eye area step over 25 % per frame. Blinks and shut changes are excepted for the eye that closes; the open eye still counts. Startle entry (SURPRISED, SHOCKED): opening steps up to 50 % in the first 100 ms. Swaps under a blink. Pupils inside. Never merge.', 'c4'],
   ['Stays in palette', 'Only background and botFaceColor, hard edges, no black boxes in ambient, in all 4 colours.', 'c5'],
   ['Calm but alive', '60 s idle NEUTRAL: 10–25 gaze moves, 9–23 blinks, no still gap over 7 s, ≤ 1 fidget per 8 s.', 'c6'],
 ];
```
