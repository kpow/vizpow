# StackChan vizBot: target design — Active Objects

Phase C of `/stackchan-stable`, 2026-11-22. One page. Kevin approves this
before any refactor starts. Background:
[stackchan-interactions.md](stackchan-interactions.md) (what collides),
[stackchan-patterns.md](stackchan-patterns.md) (what's missing).

## The pattern

**vizBot moves to the Active Object pattern** (the actor model for embedded
C/C++), the standard architecture for event-driven firmware. The reference is
Miro Samek's QP framework and *Practical UML Statecharts in C/C++*; ESP-IDF's
`esp_event` loop is built on the same idea. Three rules:

1. **One owner per resource.** Each active object is a FreeRTOS task with its
   own event queue, and it is the only code that touches its hardware.
2. **Talk only by events.** No shared globals, no locks between subsystems.
   Others post events to an owner's queue or subscribe to what it publishes.
3. **Never block.** Each owner is a state machine that handles one event, then
   returns. No `delay()`, no polling.

**What it replaces:** today's firmware is the "blocking superloop" anti-pattern.
It is Arduino and M5 example code joined together, each piece assuming it owns
the chip: `delay()`, grabbing the bus, private globals.

## The active objects

| Active object | Owns | Core |
|---|---|---|
| `BusAO` | the I2C bus: every device, M5Unified calls and the camera's bus window included | 0 |
| `AudioAO` | I2S, mic, speaker, amp; wake / listen / speak as one state machine; the mic stays running between turns | 0 |
| `MotionAO` | servos and rails: gestures, web moves, idle drift, recovery, rest, all through one queue | 0 |
| `TouchAO` | the Si12T logic: subscribes to disturbance events and takes a reference only when the bot is still | 0 |
| `UiAO` | screen, render, LEDs (written only on change) | 1 |
| `NetAO` | web, cloud, voice upload: one WiFi TX plan, upload and sync never at once | 0 |

- **The event bus** carries every state change (move start and end, torque on,
  LED punch, amp, mic, upload, camera, rail). A ring buffer of it is the system
  timeline at `/debug/timeline`.
- **Web handlers** only post events. Budgets for RAM, DMA and WiFi buffers are
  checked by the owner before it acts. There is a watchdog on every active
  object.

## How we get there: strangler fig, not a rewrite

Build the new core, then move one subsystem into it per stage. Each stage is
soaked before the next, and the old wiring shrinks until it's gone. The feature
code (faces, voice, party, eyes, cloud) and months of hardware fixes are kept,
and the other 4 boards keep building throughout.

| Stage | What | Removes | Work + soak |
|---|---|---|---|
| 0 | ✅ Pin every library (M5GFX was already stock; M5-SAM2695 pinned) | build differs per machine | done |
| 1 ✅ | Core: event bus, timeline, AO base; `TouchAO` | cold-boot storm, chains 1–4 | about 1 day |
| 2 ✅ | `MotionAO`; web handlers post events | loop stalls, core-0 hardware races | 1–2 days |
| 3 ✅ | `AudioAO` | codec reset per turn, mic races | about 1 day |
| 4 | `BusAO` + `UiAO` | lock gaps, ~400 needless I2C transactions a second | 1–2 days |
| 5 | `NetAO` + budgets | RAM and WiFi starvation | about 1 day |
| Done | 24 h normal + 8 h heavy soak, function check | — | 1.5 days |

Each stage is checked on the bench in about 10 minutes: flash, exercise what it
owns, and read `/debug/timeline` for the collisions it targets. No per-stage
soak (Kevin, 2026-11-22): the pattern makes correctness visible on the
timeline, so only the final 24 h + 8 h soak, from a cold boot, proves it.

## Rules for every stage

- The final soak starts from a **cold boot**, which Kevin does (about 30 s). A
  warm boot hides the touch fault.
- A stage passes only if it removes what it targets (checked on the timeline)
  and no scorecard row gets worse.
- Bench version per stage (`3.12.1.own1`…); a branch and PR on Kevin's go.
- Not in scope: new features, any change to the bar, skunky's base (still a
  hardware fault).
