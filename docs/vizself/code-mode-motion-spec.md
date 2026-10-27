# Code mode: "Ink Reader" (motion spec)

Status: approved by Kevin 2026-10-27 (decisions in §7). No code changed yet. It builds on v3.9.0 "ferrofluid": `vizbot/party_blob.h`,
`docs/party2-motion-spec.md` (**MS**), `docs/party2-art-direction.md` (**AD**), `docs/party2-pattern-library.md`
(**PL**), and on vizbrain's live-clip speech (`server.py` `start_turn`, `brain.py` `FILLERS`).

Target: the Stackchan (CoreS3, 320×240, tier B: 3 tentacles, 2 droplets, modes n = 2–4). Voice only exists on
the CoreS3, so code mode is a Stackchan feature. The 1.69 can run it through the test endpoint for the lab.

Units: `r` is the code-mode body radius (**66 px**, §3.1). Angles are screen space, y down, 0° = right, −90° = up.
All motion runs on `BotSpring` with real time, like party mode.

---

## 0. The calls in one table

| # | Call |
|---|---|
| 1 | **Concept: "Ink Reader".** He becomes a big ink drop in the middle of the screen, over a dim scrolling "code minimap". Two mirrored reader tentacles trace lines of code while his eyes do reading saccades. Magnetic patterns stack on top of his head as "thinking". |
| 2 | **The loop is a playlist, not an animation.** The readers are a steady base layer. The PL scheduler plays a code-mode set on top (Compile, Carousel "spinner", crowns, waves, an "aha" finger), stacking 2 then 3 deep, getting busier the longer the wait. |
| 3 | **Quips: the voice is the magnet.** Pens down, he turns to you, the face's mouth surfaces in the ink, and a 5-spike "voice crown" pops with the speech envelope. Then a slump ("ba-dum") and back to reading. |
| 4 | **Found it: the hero.** One line in the backdrop lights up, both readers grab its ends at full reach (~2 r), a Grand Crown bursts with a mirrored droplet pair, then he yanks the line into himself and morphs back into the face to answer. |
| 5 | **Everything reuses party mode.** Same `PartyBlob` object, same rasteriser, tentacles, banks, scheduler, morph. New code: a centre tier and axis, the backdrop, the reader state machine, three patterns (`compile`, `aha`, `grand`), the quip and found beats, the mouth-in-blob, and `/bot/code`. |
| 6 | **Cost: about 6.5 ms average, 8.3 ms peak** on the CoreS3, under the 9 ms cost guard. The backdrop replaces the ambient effect, so the whole frame is likely cheaper than party mode. |
| 7 | **Head: gentle.** Yaw ±8° max, pitch −2…+5° from home, every move ≥ 1200 ms, ≥ 2 s between commands, idle drift off for the whole mode. |

---

## 1. Concepts

### A. "Typist"
Two tentacles hang over an invisible keyboard below him and tap. Each tap throws a tiny droplet "keystroke" and
a woodblock tick.
- Good: instantly reads as typing; the tick sound is great.
- Bad: fingers tapping rhythmically is the AD "hand" read, and constant tapping breaks the "slow at rest" rule.
  On tier B there are only 2 droplets, so the keystrokes run dry. **Keep the tick sound, drop the concept.**

### B. "Glyph drops"
Droplets pinch off and hold in formation as `{ }`, `</>` or `;` around him. A held-droplet chain (PL §5.7, not
built yet) draws the glyphs.
- Good: literal code imagery.
- Bad: tier B has 2 droplets, beads read as dots (not glyphs) at desk distance, and a static formation is the
  canned feeling PL was written to kill. Needs the most new code.

### C. "Ink Reader" (recommended)
A big centred drop reads a scrolling code minimap with two mirrored tentacles. Magnetic patterns stack above his
head, quips make the voice the magnet, and "found it" is a full-reach grab plus a Grand Crown.

**Why C:**
- **It is ferrofluid, not a cartoon prop.** Every move is one PL already proved on the device: tracked tentacles,
  crowns, carousel, splat. The only new imagery is the backdrop, and the blob's per-pixel rim picks up its
  colours for free (AD §3.1 "local reflection").
- **Kevin's taste:** two tentacles on screen all the time and a third now and then ("never see more than 1"); medium
  patterns; stacking that grows into "crazyness" as the wait drags on; symmetric magnetic shapes (mirrored readers,
  crowns on the axis); restrained reach (1.0–1.4 r) in the loop and a 2 r grab on the hero.
- **Purpose:** AD rule 4 says every tentacle reaches *for* something and the eyes look at it. Reading a line is the
  clearest purpose there is, and eyes doing reading saccades are the strongest "he is doing code" cue for the cost.
- **It loops forever without repeating:** the scheduler, cool-downs and stacking already guarantee that (PL §4).
- **Dark on dark:** a dim backdrop with a black drop is exactly the black-on-black look Kevin likes. The rim stays
  as shipped.

---

## 2. Storyboard

Times are ms from each beat's trigger. "Body" covers modes, hop, spikes and κ. "Eyes" covers `botEyes`
(look via `look()`, `popS`, blinks, expressions through `expr()` / `exprReq`).

### Frames (Stackchan 320×240, 1 char ≈ 8×16 px)

```
F0  normal face (voice THINKING)          F1  enter, 200 ms: the outline rounds into the drop
+----------------------------------------+ +----------------------------------------+
|                                        | |   - --- -- ----      --- -- ----  -    |
|                                        | |  -- - ---- ------  -- ---- - --- --    |
|                                        | |            ___________________         |
|             (O)       (O)              | |          /    (o)       (o)    \       |
|                                        | |         |                       |      |
|                 \___/                  | |          \_____________________/       |
|                                        | |   ---- -- --- ---     -- ----- --      |
+----------------------------------------+ +----------------------------------------+

F2  loop: reading (readers trace lines)    F3  loop: Compile stacked on Carousel
+----------------------------------------+ +----------------------------------------+
| ---- ------  --  ---    ----- ---  --  | | ---- ------    ^  ^  ^  ^  ^  --- --   |
|  -- ---- ---   ________   -- -- ----   | |  -- ---- -- ^_/^^^^^^^^^^^\_^  ---     |
| ==o~~~~~__ __/        \__ ~~~~~~o==    | | ---o~~~~~__/              \__~~~~o--   |
|  ---  ~~ /    (o)  (o)   \  -- ---     | |  --  ~~ /    (o)  (o)      \ -- ---    |
| ---- --  |                |  --- ----  | | ---- -- |                  | --- --    |
|  --- --   \______________/  -- ----    | |  --- --  \________________/  -- ---    |
| -- ----- ---  ----  --- ---- -- ----   | | -- ----- ---  ----  --- ---- -- ----   |
+----------------------------------------+ +----------------------------------------+
  == the line under each bulb glows a step     spikes pop outside-in, centre last ("built")

F4  quip: pens down, mouth up, voice crown F5  found it: line lit, full-reach grab, Grand Crown
+----------------------------------------+ +----------------------------------------+
| ---- ------  --  ---    ----- ---  --  | |  .     ^ ^ ^ ^ ^ ^ ^ ^ ^       .       |
|  -- ---- ---   __^_^_^__  -- -- ----   | |      ___ __/^^^^^^^^^^^^^\__ ___       |
| ----    o__ __/          \__ __o   --  | | o##########/               \#########o |
|  ---       /    (^)  (^)   \     ---   | |  --  ~  /    (O)  (O)   \  ~  ---      |
| ---- --  |       (  )      |  --- ---  | | ---- -- |                 | --- --     |
|  --- --   \______________/  -- ----    | |  --- --  \_______________/  -- ---     |
| -- ----- ---  ----  --- ---- -- ----   | | -- ----- ---  ----  --- ---- -- ----   |
+----------------------------------------+ +----------------------------------------+
  readers shrink to nubs, eyes on you          ### = the found line; . = flung droplets
```

### 2.1 Enter (≤ 600 ms, from the normal face)

Party off. The face is centred at (160, 105) showing THINKING.

| t (ms) | Body / screen | Tentacles | Eyes / head / sound |
|---|---|---|---|
| 0 | Blink-swap (BW_SWAP): the face's eye stroke becomes the drop at the morph start ellipse (`PARTY_MORPH_R` 78.5, `PARTY_MORPH_J` 0.31), **in place**. Backdrop rows fade in from black over 450 ms. | — | Blink. Idle drift off (`scIdleServo.enabled = false`). |
| 0–380 | `P` 0 → 1 at **2.0 Hz ζ 0.85** (party uses 1.4 Hz): the ellipse rounds into r 66 while the centre glides 13 px down to (160, 118) | — | Eyes scale 1.0 → k 0.55 (`morphK`). THINKING → FOCUSED at 200 ms |
| 380 | Landing: mode 2 +0.12, mode 4 +0.03. "Spark": 3 spikes on the up-axis pop to 0.20 r for 250 ms (a thought pops up) | — | — |
| 450 | — | The reader pair spawns (mirror, §2.2) and grows at 1.6 Hz ζ 0.5, arriving ≈ 750 ms | Eyes glance right (pad 0.4): the first line to read |
| 600 | Loop running | Readers still arriving: the enter overlaps the loop by design | First head move starts (yaw −6°, 1400 ms). Ticks may start |

**Party on:** he is a corner blob. He flies to the centre with a fast slingshot (`startRun` with a new
`PARTY_CODE` run: lead 80 ms, Q 2.2 Hz ζ 0.85, 1 grab tentacle, 2 streamers), while a radius spring (2 Hz ζ 0.9)
grows r from his party size to 66. That lands at about 550 ms. The ambient show is replaced by the backdrop under
the launch (see question 1). On exit he flies back and the show returns.

### 2.2 Main loop (5–60 s, never the same twice)

The loop has three layers. Only the top one is scheduled.

**Layer 0, always on: breath, backdrop, effort ramp**
- Breath and edge waves as in party mode, with κ 0.85 (a bit calmer than party, so the patterns read).
- The backdrop scrolls up at 8 px/s (one 12 px row every 1.5 s).
- **Effort ramp:** with `e = clamp(t / 45 s, 0, 1)`, the agitation floor goes 0.10 → 0.35, the scroll and the
  reading speed go ×1.0 → ×1.4, and the stack cap goes 2 → 3 at 15 s. The longer it takes, the harder he works.
  That is the build-up that keeps a 60 s wait from feeling flat.

**Layer 1, base: the readers (new `CodeReader`, not a `PartySeq`, so all 3 sequence slots stay free for stacking)**
- A mirrored pair (`mirror = true`, shared `ph`), kind `PT_SEQ`, `free = true`, `track = true`.
- Roots at **−20° and 200°** (right and left, a little above horizontal), curl 0.25 (a gentle downward C, like a
  pen), sway 0.
- Each reader's tip target is `(x(t), y_row)`. `y_row` is the y of a backdrop row within ±2 rows of the root
  height, so it rides up with the scroll and hops down a row on each carriage return. x eases from 0.4 r to 1.3 r
  past the rim (outward on both sides, mirrored).
- One line: 1400 ms sweep (ease in-out), then a 260 ms carriage return: λ target 0.55 (pen up), the next row,
  regrow.
- Rhythm variants per line, so the reading never ticks like a metronome:

| Variant | Share | What happens |
|---|---|---|
| Read | 55 % | 1400 ms sweep, as above |
| Skim | 25 % | 2 lines at 600 ms each, eyes in one long saccade per line |
| Stare | 20 % | The tip stops on a token for 1000 ms and boops twice (`boops = 2`): "hm, this one". Eyes pupils small |

- The row under each bulb brightens ×1.8 with a 400 ms trail, so you can see what he is reading.
- **Eyes read one side at a time:** 3 saccades along the line (pad x 0.15 → 0.35 → 0.55, mirrored for the left
  side), a return sweep on the carriage return, and the side alternates per line. 30 % of carriage returns get a
  blink. The body stays symmetric; only the gaze travels (PL §2.4: gaze tracking is fine for travelling patterns).

**Layer 2, scheduled: the code-mode playlist (PL scheduler, new def table)**

The axis for every pattern is **up (−90°)**, so crowns sit on his head like a thinking cap and mirror about the
face.

| Key | Status | Tier | Weight | Cool-down | Role here |
|---|---|---|---|---|---|
| `compile` | **new** | M | 16 | 14 s | Progress bar, symmetric (below) |
| `carousel` | reuse | M | 14 | 14 s | The loading spinner |
| `breath` | reuse | S | 14 | 5 s | Background thinking |
| `crown` | reuse | M | 12 | 12 s | "Ooh": centre-out nucleation |
| `wave` | reuse | M | 12 | 10 s | A scan across the crown |
| `merge` | reuse | M | 10 | 12 s | Many ideas into three |
| `split` | reuse | M | 8 | 14 s | One idea forks |
| `aha` | **new** | M | 8 | 18 s | A third tentacle: one short finger raised up the axis (0.7 r, 2 boops). Readers + aha = 3 limbs with the centre shorter (PL Bloom-triple rule) |
| `calm` | reuse | S | 6 | 20 s | Palate cleanser, solo |

- Tier odds: S 0.35, M 0.65, B 0 (big is saved for "found it").
- Rest between stacks 0.8–2.0 s (party: 2–6 s). Cool-downs are ×0.4 of party's because the set is smaller.
- Stacking: chance 0.6, cap 2 until 15 s, then 3 (or `?stack=`).
- Every PL rule still holds: no repeat, no third spike pattern in a row, calm only plays alone.
- **Quip guard:** vizbot learns the quip period from the last two quips (default 8 s). No M pattern starts when the
  next quip is predicted within 2.5 s; an S pattern or a rest goes there instead. Patterns don't get cut mid-hero.
- **Faces under crowns:** FOCUSED while reading; THINKING (looking up the axis, pad 0.4) while any slot is above
  0.30 r. PL rule: never ANGRY or MISCHIEF with a sharp crown.

**`compile` (new):** slots 0–8 on the up-axis (`setSlot` standard 18° grid). Pairs pop from the outside in, every
220 ms: ±72° to 0.14 r, ±54° 0.18 r, ±36° 0.22 r, ±18° 0.28 r, then the centre at 0.40 r (7 wide) with
`popS(0.03)`. That is the reverse of the Rosensweig crown, so it reads differently. Hold 500 ms with a 2.2 Hz
±5 % tremble (mirror phase). Then all drop at once, mode 2 +0.07 slump, HAPPY 600 ms ("build passed"). Total
2.6 s. With the synth, each pop is a soft tick (§3.4).

**`aha` (new):** `spawnTent` with `dir = axis`, L 0.7 r, curl 0, sway 0.06 at 0.4 Hz, gf 1.3 / gz 0.55, `boops = 2`,
kind `PT_SEQ`. It holds 1.6 s and retracts. Eyes look up the finger (pad 0.5), SURPRISED 400 ms then HAPPY. Skipped
when a reader slot is missing (the tier B pool is 3).

**What a minute looks like:** 7 chunks of about 7 s split by quips. Each chunk is 1–2 stack starts drawn from 9
entries, layered 1–3 deep, with 3 reading variants underneath. The odds of seeing the same stack twice in a minute
are low, and the effort ramp makes minute one look different from second ten.

### 2.3 The quip beat (~every 8 s, while a quip is spoken)

The quip is a sentence vizbrain appends to the live clip, so the bot plays it and lip-syncs it itself. The
`?quip=` signal only gives the motion a head start (§5.2).

| t | Body | Tentacles | Eyes / mouth / head / LEDs |
|---|---|---|---|
| q0 (signal, ≈ 300 ms before audio) | Running patterns release gently (falling springs: the crown slumps, which reads as "stops to tell you something"). κ → 0.6 | **Pens down:** both readers retract to λ 0.3 and hold as short nubs (not absorbed, so they come back fast) | Blink-swap to the quip face (default HAPPY, or `expr=` from vizbrain). Look at the viewer (pad 0, −0.1). Head: yaw → 0°, pitch home +3° over 1000 ms |
| audio onset (first lip-sync frame ≥ 2) | Mode 2 +0.06 (a breath in) | — | **The mouth surfaces:** the face's lip-sync mouth draws inside the drop (§3.2) |
| talking | **Voice crown:** 5 slots on the up-axis (the Breathing Crown layout and thresholds, H 0.30 r). `F = 0.15 + 0.065·lvlSm`, with `lvlSm` the lip-sync level (0–12) on a 3 Hz ζ 0.8 spring, so the spikes ride phrases, not syllables (under the 4.5 Hz cap). **Body lip-sync:** mode 2 target −0.010·lvlSm (taller when loud, floor −0.10, PL rule 5), agitation +0.02·lvlSm | Nubs sway 0.08 rad at 0.4 Hz, mirrored | Mouth follows `botMode.face` lip-sync. LED ring: warm white at 6 % + 1.5 % per level |
| end (mouth quiet 350 ms, or `ms` + 600 ms) | **Ba-dum:** voice crown drops, mode 2 +0.08 slump, mode 4 +0.03 | — | Mouth sinks (stops drawing). Blink. 1 quip in 3: rimshot cue (if synth) |
| end + 300 ms | κ → 1. Scheduler rests 1.0 s, then resumes | Readers regrow (1.6 Hz) onto the next row | FOCUSED, reading resumes |

- If the quip face is MISCHIEF, SASSY, DEVIOUS or ANGRY, the voice crown is replaced by a mode-3 pump
  (+0.02·lvlSm): no spikes with those faces (PL rule).
- If no audio arrives within 1500 ms of the signal, the beat cancels quietly (readers regrow).
- If the bot hears speech audio during code mode without a signal (an old vizbrain, or the first "One sec."
  filler), it runs the same beat from audio onset. The signal is an optimisation, not a requirement.

### 2.4 "Found it!" (hero, ≈ 1.4 s, then the exit)

If a quip is still talking, the hero waits for its end (at most 1.5 s), so the joke isn't stepped on.

| t (ms) | Body / screen | Tentacles / drops | Eyes / head / sound / LEDs |
|---|---|---|---|
| 0 | Patterns abort (`abortSeqs`). The backdrop stops scrolling. **The found line** (the row the eyes are on, nearest the axis) lights to face colour at 60 % | Readers retarget: tips IK to the line's two ends | Forced blink → SURPRISED, `popS(0.04)`. Head: pitch home +5° over 500 ms ("perk up") |
| 0–150 | Crouch: mode 2 −0.10 | Readers whip out at 3.0 Hz ζ 0.6 (the grab spring) to **full reach, ≈ 2 r** (capped by `capLen()` 130 px and the screen) | — |
| 150 | **`grand`** (new, PL §11 Grand Crown): slots 0–8 on the up-axis, H 0.55 r, F steps to 1 at 6 Hz ζ 0.7, centre-out in ≈ 80 ms, 7 wide over 0.40 r | — | `SEQ_SUCCESS` chime (synth only). LEDs: ring flashes green 40 % for 300 ms |
| 300 | Slots 2 and 6 drop to 0.3 h for 150 ms, then regrow | **A mirrored droplet pair** flung from those tips along their axes, 180 px/s, r 0.09 r (the whole tier B pool) | — |
| 300–900 | Hold, tremble 2.5 Hz ±5 % (mirror phase) | Droplets bounce off the glass | — |
| 900 | — | **The yank:** readers retract fast (4 Hz ζ 0.8). The lit line shrinks toward the centre between the two bulbs and disappears behind the drop (the backdrop draws first, so the drop "swallows" it) | — |
| 1000 | F → 0, spikes snap back with dimples. Splat: mode 2 +0.20, mode 4 +0.05 | Droplets return with bridges and get absorbed by ≈ 1600 | PROUD 1.2 s. LED green fades over 800 ms |
| 1400 | Exit starts (§2.5), **or earlier when the answer's audio starts** (never before 600 ms) | Droplets still out are forced home at 2.5 Hz | — |

### 2.5 Exit into normal speaking (≈ 500 ms)

| t (ms) | Party off | Party on |
|---|---|---|
| 0 | Backdrop fades out over 300 ms. Readers and drops drained. `P` 1 → 0 at 2.0 Hz: the drop grows into the face's outline ellipse and glides up to (160, 105) | Run back to his corner (`PARTY_CODE` run reversed); radius springs back to his party size; the ambient show returns |
| 0–400 | The blob mouth keeps lip-syncing, scaled with `morphK`, so speech never loses its mouth | Body lip-sync (§2.3) keeps going; no mouth (party rule) |
| ≈ 400 | Swap under a blink to the normal face. `botMode.face` lip-sync continues. Idle drift back on after 2 s | Lands; party continues and he answers as the corner blob |

**Shrug exit** (`?v=0` without found: error, budget out, cancel): readers retract fast, a 500 ms Calm Dome,
CONFUSED 800 ms, then the exit above. About 900 ms in all. A tap that cancels the voice turn exits at once.

---

## 3. Placement, eyes, mouth, LEDs, sound, head

### 3.1 Where it lives

- **Centre of the screen, full size.** Centre (160, 118), body r 66 (diameter 132 of 240 px), eye scale k 0.55.
  It is the main event of the turn and the viewer is watching, so eyes and mouth need to read at desk distance.
  A corner blob at M (k 0.36) is too small to lip-sync.
- Room check: top of body at y 52 leaves 52 px for crowns (the Grand Crown is 36 px). Readers at 1.3 r end at
  x ≈ 8 and 312, inside the screen. Hero reach clamps to the glass with `onGlass` (the CoreS3 has square
  corners, `PARTY_GLASS_R` 0).
- The **backdrop** fills the screen behind him: rows of rounded bars (a "code minimap"), 12 px pitch, 6 px tall,
  indents 0–4 × 12 px, 1–5 tokens of 8–48 px per row. Four dim syntax hues at 18–28 % luma, tinted toward
  `botFaceColor`. This replaces the ambient effect or the solid background for the length of the mode.
- No wander: he holds still so the reader roots are stable. Breath, modes and the effort ramp give the life.
- No bubbles (as party mode). The voice task's "Hmm..." bubble is hidden while code mode is on.

### 3.2 Eyes and mouth inside the drop

- Eyes: `botEyes.render(...)` as in party, with brows on (`brows = true` in the code tier).
- Expressions used: FOCUSED (reading), THINKING (under crowns), HAPPY (quips, `compile` end), SURPRISED → PROUD
  (found), CONFUSED (shrug). No ANGRY/MISCHIEF family while spikes are up.
- **Mouth (party off only):** during a quip and the exit morph, draw `drawBotMouth()` with a copy of
  `botMode.face` scaled by k (width and curve ×k), at the eye centre + `mouthOffsetY`·k. While the mouth shows,
  the eye offset target moves up by 0.12 r. It is clamped above the bottom floor (0.85 R − 6 px). The mouth only
  draws while the lip-sync level was above 0 in the last 300 ms, so no flat line sits there between quips.

### 3.3 LEDs (12-pixel base ring)

Only when the ring's mode isn't OFF. The previous mode comes back on exit.

| Beat | Ring |
|---|---|
| Loop | Mirrored 2-dot scanner in face colour at 12 %, starting from the top centre and stepping down both sides with each reading saccade. A carriage return resets it to the top |
| Quip | All pixels warm white, 6 % + 1.5 % per lip-sync level |
| Found | Green 40 % flash 300 ms, fade 800 ms |
| Shrug | Amber 15 % for 500 ms |

### 3.4 Synth cues (optional)

- **Only with the Unit Synth** (`botSounds.useMidi`). On the M5.Speaker fallback the speaker belongs to his voice
  (and `stop()` would cut it), so code mode stays silent there.
- Loop: `SEQ_TYPING` (woodblock click) in bursts of 3–7 ticks at 3–6 Hz, on 50 % of reader lines. Each `compile`
  pop is one tick.
- Never while the mouth is active.
- Found: `SEQ_SUCCESS`. Rimshot after 1 quip in 3: two percussion hits, a new 2-event sequence (P2).

### 3.5 Head motion (gentle, brownout-safe)

| Rule | Value |
|---|---|
| Idle drift | Off for the whole mode (`scIdleServo.enabled = false`), so code mode is the only source of moves |
| Yaw | ±8° max. Loop: toward the side the eyes read, ±6°, one move every 2 lines (≈ 3.4 s) |
| Pitch | home (48°) −2…+5°. Quip +3°, found +5°, exit back to home |
| Move time | ≥ 1200 ms (quip 1000 ms, found perk 500 ms only for the 5° pitch), through `scMoveYaw` / `scMovePitch` |
| Spacing | ≥ 2.0 s between commands; never retarget a move in flight (the brownout lead is constant spring-retargeting) |
| Yield | Skip moves while `scHeadBusyUntilMs` is more than 2.5 s away (the 5-minute rail cycle). An external `/bot/head/*` call wins and pauses code-mode head moves for 3 s |

---

## 4. Parameters

### 4.1 New vs reuse

| Piece | Reuse (exact names in `party_blob.h` unless noted) | New |
|---|---|---|
| Body, rim, shadow, sheen | `buildOutline`, `placeOutline`, `drawRimAndBody`, `drawShadow`, `drawSheen`, `PartyRaster`, modes `mc`/`ms`, `modeKick`, `PARTY_MODE_DEF`, `kappa`, `E`, `L`, `g` | `PARTY_CODE_TIER`; `tier()` returns it while code mode is on |
| Centre and axis | `home()`, `alphaHome()`, `clampCone()` | Code-mode branch: `home()` → `CODE_CX/CY`, `alphaHome()` → `CODE_AXIS` |
| Enter / exit morph | `enter()`, `beginExit()`, `stepMorph()`, `P`, `morphR()`, `morphK()`, `morphOn()`, BW_SWAP blink | Morph in place, `P` at 2.0 Hz; `PARTY_CODE` run (a `PartyRun`) for party on |
| Readers | `PartyTentOpt` (`track`, `tx`, `ty`, `L`, `kind = PT_SEQ`, `free`, `mirror`, `side`, `ph`, `curl`, `swA`, `boops`, `hold = -1`), `spawnTent`, `stepTent` tracking, `retract`, `tentOf`, `later(PE_BOOP)` | `CodeReader` state machine (line sweep, carriage return, variants, row choice, eye saccades) |
| Patterns | `startSeq`, `stepSeq`, `stepSeqs`, `endSeq`, `abortSeqs`, `abortOne`, `PartySeqDef`, `PartyBank`, `PartySlot`, `setSlot`, `bankSprings`, `crownH`, `claimBank`, `stepSlots`, `throbUntil`, `seqLook`, `seqFace`, `seqBlink`, `popS`, `calmEnd`, `PARTY_STACK_MOST`, `PARTY_STACK_CHANCE` | `SEQ_COMPILE`, `SEQ_AHA`, `SEQ_GRAND` cases; a `CODE_SEQ_DEF` table; `pickSeq`/`eligible`/`scheduleSeq` take the table, rest range and cool-down scale as inputs |
| Droplets | `spawnDrop`, `stepDrop`, `buildDrop`, bridges, forced return | — |
| Voice crown | Breathing Crown layout (`SEQ_BREATH` slots and thresholds), `crownH` | `F` from `lvlSm`; body lip-sync term in `step()` |
| Lip-sync source | `voiceSpeakT0`, `voiceEnv`, `voiceEnvFrames` (`bot_mode.h`), `botMode.face` mouth fields | Read the level inside `PartyBlob::step` |
| Mouth | `drawBotMouth()` (`bot_eyes.h`) with a scaled face copy (as the album-art corner does) | Placement and show/hide rule |
| Backdrop | `gfx->fillRect`, canvas | `CodeBackdrop`: 24-row ring, scroll, highlight, found line, fades |
| Cost | `partyRenderStats`, `stagesUs`, `guard()`, `partyTentCap` | Stage 6 "backdrop" in `stagesUs` |
| Head / LEDs / sound | `scMoveYaw`, `scMovePitch`, `scHeadBusyUntilMs`, `SC_SERVO_Y_HOME_DEG`, `scIdleServo`, `scLeds`, `scSetBaseLedColor`, `scRefreshBaseLeds`, `cmdPlaySequence(SEQ_TYPING / SEQ_SUCCESS)` | A small code-mode choreographer for each |

### 4.2 Values

| Name | Default | Range | Notes |
|---|---|---|---|
| `PARTY_CODE_TIER` | `{k 0.55, r 66, env 79, below 82, wander 0, 0, brows true}` | k 0.45–0.60, r 56–70 | Keep `r − 2.04·48·k − 3 ≥ 8` px for the eye clamp |
| `CODE_CX`, `CODE_CY` | 160, 118 (CoreS3); 120, 150 (1.69 lab) | — | |
| `CODE_AXIS` | −90° | −90° only | All patterns mirror about it |
| `CODE_MORPH_F`, `_Z` | 2.0 Hz, 0.85 | 1.6–2.6 Hz | `P` spring for code enter/exit |
| `CODE_KAPPA` | 0.85 | 0.6–1.0 | Loop edge-wave gain |
| `CODE_ROW_PITCH`, `CODE_BAR_H` | 12, 6 px | 10–16, 4–8 | |
| `CODE_SCROLL` | 8 px/s | 4–16 | × (1 + 0.4·e) |
| `CODE_BAR_LUMA` | 0.18–0.28 | 0.10–0.35 | Don't brighten: black-on-black is the look |
| `CODE_HILITE` | ×1.8, 400 ms trail | ×1.4–2.2 | Row under a reader bulb |
| `CODE_FOUND_LUMA` | 0.60 of face colour | 0.4–0.8 | The found line |
| `CODE_FADE_IN`, `_OUT` | 450, 300 ms | 200–600 | |
| `CODE_READ_ROOT` | ±20° above horizontal (−20°, 200°) | 0–35° | |
| `CODE_READ_L0`, `_L1` | 0.4 r, 1.3 r past the rim | L1 1.0–1.6 r | Kevin's medium reach |
| `CODE_READ_LINE_MS` | 1400 | 900–2000 | × (1 − 0.25·e) |
| `CODE_READ_CR_MS`, `_CR_LAM` | 260 ms, 0.55 | 180–400, 0.4–0.7 | Carriage return |
| Reader springs | grow 1.6 Hz ζ 0.5, curl 0.25, sway 0 | — | `PartyTentOpt` defaults otherwise |
| Variants | read 55 %, skim 25 %, stare 20 % | — | |
| Saccades | 3 per line, pad x 0.15 → 0.55, blink on CR 30 % | 2–4 | |
| Effort ramp | `e = t / 45 s`; E floor 0.10 → 0.35 | ramp 30–90 s | |
| Scheduler | tiers S 0.35 / M 0.65 / B 0; rest 0.8–2.0 s; cool-down ×0.4; stack chance 0.6; cap 2 → 3 at 15 s | `?stack=` | Weights in §2.2 |
| Quip guard | no M start within 2.5 s of the predicted quip | 1.5–4 s | Period learned, default 8 s |
| `compile` | 9 slots, pairs every 220 ms, heights 0.14/0.18/0.22/0.28/0.40 r, hold 500 ms | interval 150–300 | Springs 3.2/0.45 up, 4.5/0.38 down |
| `aha` | L 0.7 r, 2 boops, hold 1.6 s | 0.5–0.9 r | |
| Quip pens-down | λ 0.3, κ 0.6, wait ≤ 1500 ms for audio | λ 0.2–0.5 | |
| Voice crown | 5 slots, H 0.30 r, `F = 0.15 + 0.065·lvlSm`, `lvlSm` spring 3 Hz ζ 0.8 | H 0.20–0.38 r | Breathing Crown thresholds |
| Body lip-sync | mode 2 −0.010·lvlSm (≥ −0.10), E +0.02·lvlSm | 0–0.015 | |
| Quip end | quiet 350 ms; slump mode 2 +0.08, mode 4 +0.03; resume +300 ms | — | |
| `grand` | slots 0–8, H 0.55 r, F 6 Hz ζ 0.7, rise 4 Hz ζ 0.4, thresholds 0.20 + 0.06·\|k−4\| | — | PL §11 |
| Found reach | grab spring 3.0 Hz ζ 0.6, L = `capLen()` | — | ≈ 2 r on the CoreS3 |
| Found drops | 2, 180 px/s, r 0.09 r | — | Tier B pool |
| Found splat | mode 2 +0.20, mode 4 +0.05 | — | |
| Found → exit | 1400 ms, or answer audio onset (≥ 600 ms) | — | |
| Head | yaw ±8°, pitch −2…+5°, move ≥ 1200 ms, ≥ 2 s apart | — | §3.5 |
| Safety timeout | exit (shrug) after 75 s with no signal, or when the voice turn ends | 45–120 s | |

### 4.3 Cost per frame (CoreS3, estimates)

Reference numbers: party v3.8 on the CoreS3 measured **4.3 ms average**; v3.9 with the library measured
**4.0–5.2 ms** on the 1.69; the guard caps tentacles when a 2 s window has a frame over **9 ms**.

| Item | Average | Peak | Why |
|---|---|---|---|
| Party body at M, tier B (baseline) | 4.3 | 5.3 | Measured (v3.8), plus the library |
| Body, shadow, sheen at r 66 instead of 44 | +1.2 | +1.5 | Rim ∝ perimeter (×1.5), fill and shadow ∝ area (×2.25, mostly memset) |
| Eyes at k 0.55 instead of 0.36 | +0.6 | +0.6 | Eye area ×2.3. Guess: the per-stage split for the CoreS3 isn't recorded |
| 2 readers (vs 0–1 feelers in party) | +0.25 | +0.35 | MS: ≈ 0.12 ms per tentacle at M, ×1.4 for the bigger rim |
| `aha` third tentacle | — | +0.12 | |
| Stacked spike banks (up to 3) | +0.05 | +0.15 | PL §5 |
| Backdrop (≈ 30 bars, ≈ 6 k px) | +0.15 | +0.2 | `fillRect` runs |
| Mouth (quips only) | — | +0.1 | 3 `fillCircle` |
| Grand Crown + 2 drops (found only) | — | +0.3 | PL §11 + droplets |
| **Total** | **≈ 6.5 ms** | **≈ 8.3 ms** | Under the 9 ms guard; tier B never needs the cap |

The backdrop **replaces** the ambient effect (≈ 11 ms of "effect/other" on the 1.69), so the full frame should be
faster than party mode. First port step: read `stagesUs` and the loop fps on skunky in party mode, then in code
mode, and record both here.

---

## 5. Interface

### 5.1 Bot endpoint: `GET /bot/code` (shaped like `/bot/party`)

The handler only sets volatile requests (like `partySeqReq`); the render loop acts on them.

| Query | Effect |
|---|---|
| `v=1` | Start code mode (enter). Optional `why=vizself` (logged in `/state`) |
| `quip=<ms>` | A quip is coming, about `<ms>` of audio. Optional `expr=<name or index>` for the quip face (default happy), `lead=<ms>` |
| `found=1` | Found-it hero, then exit into speaking. `exit=0` plays a small "found one" version (no Grand Crown: one `compile`, line flash) and stays in code mode |
| `v=0` | Exit without the hero (shrug). `fast=1` skips the shrug (cancel) |
| `seq=<key>` | Play one code-mode pattern now (tuning): `compile`, `aha`, `grand`, or any party key |
| `stack=off\|some\|wild` | Stack depth for code mode (own setting, default some) |
| `hero=quip\|found\|shrug` | Play a beat with no voice turn, for tuning on the device and on the 1.69 |

Response, like `/bot/party`:
```json
{"code": true, "phase": "enter|loop|quip|found|exit|off", "t": 12850, "party": false}
```
`GET /state` adds `"code": {"phase": "loop", "seqs": "compile+breath", "reader": "skim", "t": 12850}`, and the
blob's `render` stats gain the backdrop stage.

Old firmware answers 404, so vizbrain checks the firmware version (`firmware_ok`) or ignores the error.

### 5.2 How vizbrain drives it (for whoever builds the vizself connector)

1. When the first vizself call starts (right after the "One sec." filler is queued): `GET /bot/code?v=1`.
2. A quip timer runs while the lookup runs: every ≈ 8 s, `mouth.say(quip)` into the same live clip, then
   `GET /bot/code?quip=<pcm_ms>`. Send it right after the clip append, so it lands before the audio.
3. On results: `GET /bot/code?found=1`, then let Claude's answer stream as usual. The bot exits when the answer's
   audio starts or at 1.4 s, whichever is first.
4. On error, budget out or no answer: `GET /bot/code?v=0`.
5. Optional: a `Bot.code(...)` helper in `bot.py` next to `expression()` and `gesture()`.

Dependencies to flag (not part of this spec):
- `LOOKUP_BUDGET_S` is 15 s and `MAX_LOOKUPS` 4 in `brain.py`. A 60 s vizself search needs its own budget.
- The bot's live-clip read times out after 45 s of no data (`VOICE_HTTP_TIMEOUT_MS`). The quips every 8 s keep
  that stream fed, so they double as a keep-alive. If quips are ever turned off, a long search ends the turn.
- `/brain/play` returns 409 during a voice turn, so quips must go into the live clip, not through `/brain/play`.

### 5.3 Voice states

| State | Code mode |
|---|---|
| THINKING (before any audio) | May start; a pure visual wait. The "Hmm..." bubble is hidden |
| SPEAKING with the clip starved (the normal case during a search) | The loop |
| SPEAKING with audio flowing | A quip beat (signalled or detected from the lip-sync level) |
| Answer audio after `found` | The exit: the face (or the corner blob) takes over the lip-sync |
| `voiceFinish()` → IDLE while still on | Shrug exit. Also after 75 s with no signal |
| Cancel (`voice.cancelRequested`, a tap) | Fast exit, no shrug |
| `cmdSetExpression()` from the voice task | Lands on `botEyes` as usual; code mode re-asserts its own face at its next beat |
| A touch-UI sheet is open (`uiHidesBotOverlays`) | Not drawn (as party); the state machine keeps time |

### 5.4 Party mode

- One `PartyBlob` object: code mode is a flag on it, so there is no second blob and no handover glitch.
- On entry, the party scheduler stops (`abortSeqs`), roams, fidgets, pulses and the beat grab are suspended, and
  his corner is remembered.
- The code playlist uses the same banks and sequence slots, so stacking just works.
- On exit he runs back to the remembered corner and party resumes with `schNextAt = now + 4000`.
- A shake during code mode still does the porcupine and the splash (fun, harmless), then the loop resumes. A poke
  does not send him to a corner while code mode is on: he flinches (dent, readers snap in) and regrows.

---

## 6. Build order (short)

1. Lab first: add a "Code" tab to the Tentacle Lab artifact (centre tier, backdrop, readers, `compile`, quip and
   found buttons, a fake lip-sync envelope). Kevin tunes reach, scroll, stack and tempo. About 3 h.
2. Firmware: the code tier and axis, backdrop, readers, the three patterns, the beats, `/bot/code` with `hero=`.
   Measure `stagesUs` on skunky. About 4–5 h.
3. vizbrain: the four calls in §5.2 in the vizself connector work.

---

## 7. Kevin's decisions (2026-10-27)

Kevin took the recommended answer on all five open questions:

1. **Backdrop:** the dim scrolling code minimap replaces the background during code mode.
2. **Mouth:** the face's lip-sync mouth surfaces inside the drop during quips (party mode off only; with party on,
   the no-mouth rule holds and the body plus voice crown carry the lip-sync).
3. **Party on:** he flies to the centre for code mode and back to his corner afterwards.
4. **Stackchan tier:** measure on skunky first; promote to tier A (5 tentacles, 6 droplets) if it fits the cost
   guard, otherwise stay on tier B.
5. **Sound:** key-tap ticks and the found chime only when the Unit Synth is plugged in, plus a rimshot after
   1 quip in 3.
