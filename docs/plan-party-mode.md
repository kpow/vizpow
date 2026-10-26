# Plan: party mode

Party mode gives the whole screen to the ambient show. The face shrinks into a small black
ferrofluid blob in one corner. The blob has a wobbling edge, a soft shadow, and the real eyes
inside it, so the show is the focus and the bot is still there. It ships on the 1.69 first, then
on the CoreS3 and Stackchan (and the 1.3 if it costs nothing).

- Prototype: https://claude.ai/artifact/RMM4CvsGzmAg6Fujt3HJqW. It runs the eye lab's real eye
  model (`eyelab/shared`) and all 25 faces at device pixels and 20 fps.
- Motion spec: `docs/party-mode-motion-spec.md`, by the motion design agent. It covers shape,
  springs, spikes, shadow, the eyes and enter/exit.
- Layout spec: `docs/party-mode-layout-spec.md`, by the layout design agent. It covers sizes,
  corners, overlays, config and a paste-ready `layout.h` block.

## The design

**Body.** A 120-point polygon around the blob centre whose radius changes with angle, filled as row
spans straight into the canvas. The radius is the sum of:
- breath,
- three slow edge waves (n = 3, 4, 5, under 0.3 Hz, running in different directions),
- squash and stretch,
- a bulge toward where he looks,
- a fan of 9 ferrofluid spikes that rise toward an attractor and snap back with a small dimple.

Every value moves on the eye model's own `BotSpring`. The motion spec estimates about 1 ms per
frame and 2 KB of static arrays, with no heap.

**Look.** It stays flat and bold like the eyes, with no gradients:
- **Rim.** Each rim pixel is the larger of (show at half brightness) and (face colour at 22 %), per
  channel. On bright scenes that gives a soft edge. On dark scenes (galaxy) it gives a faint
  face-coloured line.
- **Sheen.** One flat crescent at the upper left, tinted with a low-passed sample of the show, so
  the blob reflects the party.
- **Shadow.** A floor ellipse that darkens the pixels under him (75 % and 50 % brightness, with a
  dither anchored to the screen). It shrinks when he hops.

**Eyes.** `botEyes` is drawn at the blob centre. The eyes trail the body on a 2.4 Hz spring, so
they lag on hops and settle late. The body bulges toward his gaze. **No mouth** (Kevin,
2026-10-25, after comparing none, a tiny 2 px mouth and the album-art mouth in the prototype).
**Brows show only at sizes M and L.**

**Sizes** (same pixels on every target, because eye R is 48 px everywhere):

| Size | Eye scale | Body r | Screen area on the 1.69 |
|---|---|---|---|
| S | 0.30 (legibility floor) | 37 | 6 % |
| **M (default)** | **0.36** (= album-art corner) | **44** | **9 %** |
| L | 0.44 | 54 | 14 % |

**Corner is his choice, not a setting** (Kevin, 2026-10-25). He picks a random corner when the
party starts, and runs to a different random corner every time he's poked. Every 25–60 s he also
strolls to a new corner on his own (see Roam below). Nothing about the corner is saved.

**Placement.** Home is 2 px plus the spike envelope in from the corner. The wander box only runs
inward, so wandering never clips on the bezel or on the 1.69's rounded corners (radius estimated
at 40 px, which needs a bench check). Spikes only point into the show.

**Events.**

| Event | Blob | Eyes |
|---|---|---|
| Tap on the screen | Spike fan toward the finger, plus a hop | Look at the tap |
| Tap on the blob (poke) | Dent and crouch for 260 ms. Then he runs to a random other corner along an arc that bows 45 % toward the screen centre, driven by one spring (1.5 Hz, ζ 0.85), so a diagonal run takes about 0.7 s. He stretches with speed, reaches spikes ahead, and lands with a squash and a spike burst. Taps are ignored while he runs. | Blink, SURPRISED, look where he's going. His face comes back 0.5 s after landing. |
| Shake | All-round "porcupine" spikes plus a side jiggle | DIZZY |
| Beat (CoreS3 / Stackchan mic) | Smoothed spike pump, and a hop on strong bass | Small pop |
| Pulse (1.69, which has no mic) | A spike fan every 5–12 s | none |
| Roam, on his own every 25–60 s | First a 0.7 s glance at where he's going. Then a calm stroll on a slower spring (0.95 Hz, ζ 0.9, about 1.1 s) with smaller kicks and a gentler landing than a poke. Three times in four he goes to a neighbouring corner, along one edge; otherwise he crosses the diagonal. | His face doesn't change. He looks where he's going, then blinks on landing. |
| Idle fidgets every 12–25 s | Probe, shiver or scoot | Look along the probe |

**Enter and exit.** He blinks, and the screen swaps while his eyes are shut. From black, a
screen-sized blob shrinks like an iris to reveal the show while he hops along an arc into the
corner. He lands with a squash and a spike burst. Exit runs the same way in reverse.

## How it fits the firmware

- **What it is.** Party mode is a flag that takes effect only on the ambient scenes
  (`botBackgroundStyle == 4`). It is not a new style 5: seven places in the code treat 4 as
  "ambient", and older firmware would read a saved 5 as Black. Turning it on from Black switches
  to the last effect. While it is on, scene swipes skip Black.
- **Render.** A new `party_blob.h`, hooked into `renderBotMode()` (`bot_mode.h`) between the
  ambient background and the face. Draw order: effect → shadow → rim → body → sheen → eyes. The
  album-art path (`bot_mode.h:814`) keeps working and moves into the party corner.
- **Pixel ops.** The sprite buffer is byte-swapped RGB565, so swap before and after the darken
  ops. The 8-bit canvas fallback uses a flat checker shadow.
- **Config.** Two settings. Corner is not one: he picks it himself.

  | Setting | NVS key | Values | Default |
  |---|---|---|---|
  | Party on/off | `party` | off / on | off |
  | Size | `pSize` | S / M / L | M |

  - **HTTP:** `GET /bot/party?v=&size=`. `/state` also reports the corner he's in right now
    (read only).
  - **Touch:** one on/off row in Settings → Look.
  - **vizbrain:** one tool, `vizbot_party(on, size)`.
  - Shadow and wander are compile-time defines (`PARTY_SHADOW`, `PARTY_WANDER`).
- **Overlays.** These follow whichever corner he's in, and swap over when he lands after a run.
  - The clock moves to the far top corner.
  - Toasts and the speech bubble use the band away from the blob.
  - Party layout pauses while any sheet or page is open.

## Where the two specs disagreed (resolved in the prototype)

1. **Size.** The motion spec assumed body r 40 and eye scale 0.28. The layout spec owns size and
   wins: M is 0.36 and r 44, the same eyes as the album-art corner.
2. **Eye position.** The motion spec raised the eyes 0.10 R. The layout spec centres them, because
   with no mouth there is nothing to balance. The prototype centres them.
3. **Spike reach.** The layout envelope is 1.2 r, but the motion spikes reach about 1.4 r. The
   prototype only lets spikes point within 60° of the screen centre, so the long ones go into the
   show and never toward an edge.
4. **Dents.** Within 35° of horizontal, the radius stays at 0.97 r or more so a mood pop can't push
   the eyes through the side. Elsewhere the floor is 0.85 r.

## Phases

1. **Tune in the prototype (Kevin, about 20 min).** Play with corners, sizes, scenes and events,
   then answer the open questions below. Later the prototype can move into the eyelab app as a
   Party tab (eyelab phase 4 or later).
2. **Firmware core on the 1.69 (about 3–4 h). Done 2026-10-25** on branch `feat/party-mode`,
   firmware `3.7.41-party.3` on vizBot-old-dino. Blob + eyes cost 2.97 ms avg, 3.97 ms max
   (`/state` → `party.render`, with per-stage `stagesUs`: step, outline, shadow, rim + body,
   sheen, eyes). Two speed-ups got it from 6.3 ms down to that:
   - Each edge drops its crossings into a row table, instead of every row testing all 120 edges.
   - Rim and body are filled in one pass, so only the thin ring is read and written back.

   The edge waves step around the outline by rotation (6 trig calls a frame, not 360). The
   CoreS3 build compiles. Version note: the eyelab deploy pipeline bumps `FIRMWARE_VERSION` on
   every deploy (3.7.40 and 3.7.41 are already used on `feat/eyelab-app`), so this branch
   tags its builds `3.7.41-party.N`. The real version gets set when it merges.
   What was in the phase:
   - `party_blob.h` with the polygon, fill, rim, body, sheen and shadow.
   - The springs, wander and synthetic pulse.
   - The hook in `renderBotMode`, the NVS keys and `/bot/party`.
   - Bump `FIRMWARE_VERSION`, OTA-flash vizBot-old-dino, and read the render cost from `/state`
     (budget: 3 ms on top of the effect).
3. **Interaction (about 2 h).** Tap and shake wiring, the poke dash and the idle roam (one
   corner-run routine with two tunings), idle fidgets,
   gaze reach, and the enter/exit morph with blink swaps.
4. **Overlays and album art (about 1–2 h).** Move the clock, toast and bubble, and put album art
   in the party corner.
5. **CoreS3 / Stackchan and release (about 2 h).** Beat input from `audioSpectrum`, the touch
   settings row, the vizbrain tool, bench checks on skunky (cold boot after flashing), then the
   release flow.

## Decisions (Kevin, 2026-10-25)

1. No mouth in party mode.
2. A poke sends him running to a random other corner. Corner is his behaviour, not a setting.
3. He also moves to another corner by himself every 25–60 s (the roam above). Pace confirmed.

## Open questions for Kevin

1. Keep the "puddle peek" fidget? In it the blob flattens and the eyes poke out above it. It is
   the only time the eyes leave the body.
2. Rounded corners on the 1.69: draw arcs at r 30/35/40/45 on the bench to confirm the 40 px
   estimate.
3. Entering from the ambient scene: the iris reveal, or start as a wide blob that matches the old
   5 px stroke around the eyes? (The prototype does the iris.)
