# Environmental Awareness Sensors for StackChan VizBot — Discovery

## Context

Today, vizBot reacts to onboard signals only: head touch (Si12T), motion (IMU), battery (INA226), audio FFT from the ES7210 mic. It has no awareness of the world *around* it — whether someone is at the desk, whether they're moving, whether music is playing in the room. The goal is to start sequencing sounds, expressions, and servo behavior to those external cues, but only if the input signals are **clean and trustworthy** (no flickery PIR false-positives, no jittery distance values). The user wants to stay in the M5Stack ecosystem so the bot remains coherent as a build.

Scope decisions made in discovery:
- **Gaze deferred** — M5Stack catalog has no multi-zone ToF and no first-party vision unit that fits the StackChan I2C bus. Revisit only if the constraint is relaxed.
- **Whole-room coverage** — the room is ~2m total, so "desk range" and "room range" collapse into one problem. Treat the ToF's 0.05–2.5m usable band as covering the whole environment, split into close / desk / far zones.
- **Discrete events, not continuous streams** — the bot consumes things like `onPresenceEnter` / `onMotionStart` / `onMusicStart`, not raw distance graphs.
- **Discovery only this turn** — no implementation; this plan documents the path.

## Existing infrastructure already covering part of this

The bot is closer to "environmentally aware" than it looks from the outside:

| Signal | Source already in tree | File |
|---|---|---|
| Close presence (0–30cm) | LTR-553 proximity (onboard Core S3, M5.In_I2C addr 0x23) | [proximity_light.h](vizbot/src/proximity_light.h) |
| Ambient lux | LTR-553 light channel | [proximity_light.h](vizbot/src/proximity_light.h) |
| Music spectrum + beat | ES7210 mic → 512-point FFT, `bass/mid/treble/rms/beatEnv` exposed | [audio_spectrum.h:54](vizbot/src/audio_spectrum.h) |
| Touch (head) | Si12T 3-zone | [stackchan_base.h:184](vizbot/src/stackchan_base.h) |

What's missing for the desk-range use case:
- **Desk-range presence** (0.5–2m, beyond LTR-553's reach): need ToF.
- **Stationary-human detection** (sitting still at the desk): cheap PIR goes blind; need a thermopile-class part.
- **A music-state event** (start/stop) derived from the existing audio fields.

## Recommended sensor set

Two new Grove units, both M5Stack catalog, no I2C address collisions:

### 1. M5Stack **Unit ToF4M** (VL53L1X) — $8.95
- I2C addr `0x29`, FOV ~15–27°, range 4–400cm, mm-precision, >30Hz capable.
- Library: [M5Unit-ToF4M](https://github.com/m5stack/M5Unit-ToF4M).
- Role: precise distance in a forward-facing cone, split into three zones (see below). Drives the zone-transition events.
- Why it's reliable: narrow FOV + millimeter-level depth means simple threshold-with-hysteresis won't flap. Median-of-3 on raw samples kills the rare outlier.

### 2. M5Stack **TMOS PIR Unit** (STHS34PF80) — $6.50
- I2C addr `0x5A`, ~80° FOV, ~2m range, programmable 0.25–30Hz output rate.
- Library: ST's [STHS34PF80 Arduino library](https://github.com/stm32duino/STHS34PF80) (also wrapped by Sparkfun).
- Role: discriminates **moving warm body** vs. **stationary warm body** vs. **empty** — the AS312-class units cannot do this. Drives `onMotionStart` / `onMotionStop` / `onStillPresence`.
- Why this part specifically: the *only* M5Stack catalog PIR that won't go blind on a user sitting still. The headline feature is built-in presence/motion algorithms with separate thresholds.

### Skipped on purpose
- **DLight (BH1750)** — redundant with the onboard LTR-553 light channel already exposed in `proximity_light.h`.
- **NCIR2** — single-point thermopile, overpriced ($22) vs. the multi-purpose TMOS PIR.
- **mmWave / LD2412** — best stationary-presence option in the world, but M5Stack doesn't sell one. **Flagged as a future option if the "M5Stack only" rule is ever relaxed for stationary detection at >2m.**

**Total cost: ~$15.50.** Both units fit on the existing StackChan I2C bus (GPIO 12/11, mutex already in place per [task_manager.h:26](vizbot/src/task_manager.h)) — no Grove hub needed.

### Bus / pin checklist
- I2C bus has room: existing addrs 0x6F (PY32), 0x68 (Si12T), 0x41 (INA226). New addrs 0x29 + 0x5A do not collide.
- Grove Port A is internal M5Unified. Port C is owned by MIDI synth. Port B is the candidate for either a single Grove → 2× breakout, or the user can run both units on a PaHub if a daisy cable isn't acceptable cosmetically. (PaHub not strictly needed — addresses don't conflict.)
- Mount both units **forward-facing on the K151-R base shell** so they look out from under the StackChan body. Same axis as the head's home pose (yaw=0, pitch=50 already aimed up at user per [stackchan_base.h](vizbot/src/stackchan_base.h)).

## Software architecture — keep it boring and reliable

Follow the established sensor pattern (rate-limited `update()` called from Core 1's main loop, no new FreeRTOS task — see [vizbot.ino:503](vizbot/src/vizbot.ino) for `proxLight.update()` / `audioSpectrum.update()` as templates).

### Distance zones (the "far away" event lives here)

Since the room is ~2m end-to-end, split the ToF reading into four states with hysteresis on every boundary:

| Zone | Enter when | Leave when | Meaning |
|---|---|---|---|
| `ABSENT` | no return for 1.5s OR `>2200mm` for 1.5s | any valid `<2000mm` | Nobody in the cone |
| `FAR`    | `1200–2000mm` for 600ms | `<1100mm` or back to ABSENT | Person across the room |
| `DESK`   | `500–1100mm` for 400ms | crosses out for 400ms | Person at the desk |
| `CLOSE`  | `<450mm` for 300ms | `>550mm` for 400ms | Leaning in / right next to bot |

Each *transition* fires a one-shot event. The natural set:

- `onPresenceEnter`  (ABSENT → any populated zone)
- `onPresenceLeave`  (any populated zone → ABSENT)
- `onFarPresence`    (entered FAR — "someone's in the room")
- `onApproach`       (FAR → DESK, or DESK → CLOSE)
- `onWithdraw`       (CLOSE → DESK, or DESK → FAR)
- `onLeanIn`         (entered CLOSE specifically — different vibe from generic approach)

Zone thresholds live in NVS so they're tunable per-room without reflashing.

### New module: `sensor_awareness.h`

Single header owning both new drivers + the event derivation. Public surface (rough sketch, not final code):

```
enum AwarenessZone { ZONE_ABSENT, ZONE_FAR, ZONE_DESK, ZONE_CLOSE };

struct AwarenessState {
  // Raw, filtered values (continuous, available if needed)
  uint16_t tof_mm;            // 0 = no target
  float    tof_mm_smooth;     // EMA-filtered
  AwarenessZone zone;         // current zone after hysteresis + dwell
  AwarenessZone prevZone;     // last different zone (for "approach vs withdraw" direction)
  bool     pir_motion;        // STHS34PF80 motion flag
  bool     pir_presence;      // STHS34PF80 presence flag (stationary warm body)
  bool     music_active;      // derived from audio_spectrum.rms + dwell time

  // Edge-triggered events (latch one frame, consumer reads + clears)
  bool onPresenceEnter, onPresenceLeave;
  bool onFarPresence;
  bool onApproach, onWithdraw;
  bool onLeanIn;
  bool onMotionStart, onMotionStop;
  bool onStillPresence;       // arrived and now holding still
  bool onMusicStart, onMusicStop;
};

extern AwarenessState awareness;
void awarenessInit();
void awarenessUpdate();       // called from main loop, rate-limits internally
```

Consumers (bot mode, MIDI sequencer, etc.) just poll `awareness.onPresenceEnter` between frames — same shape as how the command queue is already drained. No pub/sub framework needed.

### Reliability strategy — the "not half-assed" part

This is where the user's bar matters most. Each rule has a concrete defense against a known failure mode:

1. **Per-sensor poll cadence is low and fixed.** ToF at 10 Hz, TMOS at 4 Hz. Higher rates buy nothing for human-timescale events and burn I2C contention with the render loop.
2. **All I2C reads through `i2cAcquire` / `i2cRelease`** ([task_manager.h:36](vizbot/src/task_manager.h)). Non-negotiable — concurrent reads with Si12T touch will silently corrupt otherwise.
3. **ToF: median-of-3 + EMA (α≈0.3) on raw distance.** Kills VL53L1X's occasional outlier spikes that no threshold would survive.
4. **All zone transitions use hysteresis + dwell** (see the zone table above). Enter-dwells are short (300–600ms) so the bot feels responsive; the ABSENT-enter dwell is longer (1.5s) so a brief out-of-cone moment doesn't read as "person left."
5. **TMOS uses ST's built-in presence/motion algorithm**, not raw thermopile differential. Configure the embedded thresholds per datasheet; do NOT roll your own — ST tuned theirs against humans.
6. **TMOS needs a warm-up + ambient zeroing window** (~10s after boot). During that window, treat both flags as `false` and don't emit events. Boot sequence already has a stage system ([boot_sequence.h](vizbot/src/boot_sequence.h)) — add a "warming sensors" stage.
7. **Music event derivation: `audio_spectrum.rms > 0.08` for 1.5s → `onMusicStart`. Drop below 0.05 for 3s → `onMusicStop`.** Wider hysteresis on the audio side because RMS naturally dips during quiet passages. Numbers are starting points — tunable in NVS.
8. **Every event also exposes a `lastFiredMs` field** so consumers can skip stale events on first read after a long render hitch.
9. **Failure isolation.** If either sensor's `begin()` returns failure, `awareness.tof_ok` / `awareness.pir_ok` go false and no events of that class are ever emitted. The other sensor continues working. Same shape as the existing `SystemStatus` ready flags in [system_status.h](vizbot/src/system_status.h).
10. **A "raw stream" debug endpoint** (`/api/awareness/stream` returning the last 30s of raw + filtered values as JSON) so when something flaps in the wild, you can verify the filter is doing its job before tweaking thresholds.

### Wiring into bot behavior — minimal first cut

Don't try to wire every event to every behavior on day one. The plan recommends only three initial hooks (each user-toggleable in NVS):

| Event | Initial reaction |
|---|---|
| `onFarPresence` | Subtle "I see you" cue — small LED pulse on the base ring; no servo move yet |
| `onApproach` (FAR→DESK) | Bot face transitions to "noticing" expression; servo glance toward target |
| `onLeanIn` | Curious/surprised expression; play a short sound from `bot_sounds.h` |
| `onMusicStart` | Existing audio-reactive ambient effect auto-enables; head does a small idle bob |
| `onStillPresence` (user sitting still for >30s) | Bot drops to idle-quiet behavior (slower sweeps, dimmer LEDs) — "respectful companion" mode |
| `onPresenceLeave` | Bot returns to idle home pose; LEDs to ambient mode |

That's it. Anything else gets sequenced from the web UI by the user once they're confident the events fire cleanly.

## Files this would touch (representative, not exhaustive)

New:
- `vizbot/src/sensor_awareness.h` — drivers + event derivation
- `vizbot/src/audio_events.h` (or extend `audio_spectrum.h`) — `onMusicStart` / `onMusicStop` derivation from existing fields

Modified:
- `vizbot/src/config.h` — sensor I2C addrs, poll intervals, default thresholds
- `vizbot/src/system_status.h` — `tofReady`, `pirReady` flags
- `vizbot/src/settings.h` — NVS-persisted enables + threshold overrides (follow the existing `audioSpectrumEnabled` shape at ~settings.h:65)
- `vizbot/src/web_server.h` — add `awareness` block to state JSON, debug stream endpoint, settings UI cards (follow the audio-spectrum toggle pattern around `/api/audio/...`)
- `vizbot/src/vizbot.ino` — call `awarenessInit()` and `awarenessUpdate()` in Core 1 loop alongside other sensor pollers
- `vizbot/src/boot_sequence.h` — add a "sensor warmup" stage gated on TMOS settling
- `vizbot/src/bot_mode.h` — three initial event hooks listed above
- `vizbot/platformio.ini` — `lib_deps`: `m5stack/M5Unit-ToF4M`, `stm32duino/STHS34PF80` (verify Arduino-ESP32 compatibility before committing to the second one)

All new code gated behind `#ifdef BOARD_HAS_STACKCHAN_BASE`, same as the rest of the K151-R stack.

## Verification approach (when implementation happens)

1. **Bench bring-up, sensors off the bot.** Wire each Grove unit through a USB-C M5Stack into a simple sketch that prints values — sanity-check the libraries before touching firmware.
2. **`/api/awareness/stream` test page.** Sit at the desk; walk in; walk out; play music; stop music. Watch the raw + filtered + event flags scroll. The goal is zero false events over a 10-minute "ambient" period with nothing happening.
3. **Threshold tuning loop.** Adjust NVS thresholds from the web UI without reflashing. Re-run the 10-minute test until clean.
4. **Concurrency check.** Run with the render loop maxed (a heavy hi-res ambient effect) and confirm I2C reads still return successfully — verifies the mutex usage is correct under load.
5. **Power test.** Confirm the bot still boots cleanly off battery with both sensors connected (~15mA additional draw expected — well within budget).

## Open question for later

The TMOS PIR's stationary-presence algorithm is rated to ~2m and the room is ~2m, so the headline use case (user sitting still at the desk) sits right at the edge of the part's range. If real-world testing shows the "still user" flag dropping out at the back wall, the answer is a mmWave radar (LD2412, ~$10, UART) — but that breaks the M5Stack-only constraint. Don't bake this in yet; flag it only if the testing pass actually shows it.

---

## Addendum: HLK-LD2412 mmWave radar

User decision: relax the "M5Stack only" rule for this one part because the 24GHz mmWave radar is the strongest match for the room and use case. The user will Grove-pigtail and 3D-print a case. This section *amends* the plan above — it does not throw it out.

### Why LD2412 over LD2410

Both are 24GHz FMCW radar modules from the same vendor and speak the same UART protocol, so library-wise they're interchangeable. LD2412 is the right pick because:

| | LD2410B | LD2412 |
|---|---|---|
| Distance gates | 9 (~75cm each) | 14 (~43cm each) — usable for FAR/DESK/CLOSE zones in a 2m room |
| Stationary-target tuning | Generic | Tuned firmware for low-RCS targets (seated humans) |
| Max range (moving / stationary) | 6m / 5m | 6m / 6m |
| Update rate | ~10 Hz | ~10 Hz |
| Library maturity (Arduino) | More mature (`ncmreynolds/ld2410` is well-trodden) | Adequate (`iavorvel/MyLD2410` supports it; LD2410 libs often work with minor tweaks) |
| Power | ~70 mA active | ~70 mA active |

Cost is similar (~$8–$12 either way on AliExpress; mainline distributors stock both). If LD2412 firmware is unstable in practice, swapping in an LD2410B is a one-line library change — same wiring, same protocol shape.

### What it brings (capabilities)

- **Stationary presence at up to 6m** with thoracic-motion detection (breathing). Solves the "user sitting still at the desk" case that was the open risk on the TMOS PIR.
- **Per-gate motion and energy** (14 gates × 2 channels = "moving energy" and "stationary energy" per ~43cm slice). The bot can know not just *whether* someone is there but *how far* and *what they're doing*.
- **Coverage cone ~60° H × 30° V**, narrower than PIR (~80°) but wide enough for desk seating + room entry from front.
- **Indifferent to clothing, ambient temperature, lighting, and HVAC airflow** — the failure modes that make PIR flaky.

### What changes in the sensor set

Revised loadout for the "not half-assed" bar:

| Sensor | Status | Job |
|---|---|---|
| **HLK-LD2412 mmWave** | **ADD (primary)** | Presence (moving + stationary), per-gate distance, motion intensity. Drives `onPresenceEnter` / `onFarPresence` / `onApproach` / `onWithdraw` / `onMotionStart` / `onMotionStop` / `onStillPresence` |
| **M5Stack Unit ToF4M** | **KEEP** | Mm-precise distance in narrow forward cone. Drives `onLeanIn`. Also cross-validates the radar's CLOSE-zone reading via a different physical principle (light vs RF) — if both agree on "person at <50cm" the event is much harder to spoof by RF noise from a USB charger or a fan blade |
| **M5Stack TMOS PIR** | **DROP** | Radar covers everything the TMOS does, with better stationary range and zero IR-cross-talk failures. Two presence sensors of the same class would just compete on edge cases |
| **Onboard LTR-553** | Keep (already wired) | Very-close proximity + ambient lux |
| **Onboard ES7210 mic + FFT** | Keep (already wired) | Music detection via `audio_spectrum` |

**Revised BOM:** LD2412 (~$10) + ToF4M ($9) ≈ **$19**. PIR drops out. Slightly more $$ than the original two-sensor plan, but materially better for the stationary-user case.

### Wiring — exact answer for Core S3 + StackChan

After verifying against M5Stack's Core S3 docs and the LD2412 datasheet, here is the concrete plan (this replaces my earlier "TBD pins, 256000 baud" hand-wave):

**Core S3 Grove port availability**
- Port A (G1, G2) — **I2C SDA/SCL, taken** (M5Unified, FT6336 touch, etc.)
- Port B (G8, G9) — **FREE.** No current StackChan code uses these. Confirmed against the existing pinmap.
- Port C (G17, G18) — **UART, taken by MIDI synth.**

**→ Use Port B with one Grove cable.** Single 4-pin connector, no pigtail splicing if you can solder Grove pads inside the 3D-printed case.

**LD2412 pinout (6 pins on module)**
| Module pin | Wire to | Notes |
|---|---|---|
| `5V` | Grove `5V` | Use 5V supply, not 3.3V — better static-target sensitivity. Datasheet warns: do NOT power both `5V` and `3V3` simultaneously |
| `GND` | Grove `GND` | |
| `TX` (radar TX, 3.3V logic) | Core S3 **G9** (will be `Serial1` RX) | No level shifter needed |
| `RX` (radar RX) | Core S3 **G8** (will be `Serial1` TX) | No level shifter needed |
| `OUT` | not connected (v1) | Optional fast-path presence interrupt; route to a free GPIO later if useful |
| `3V3` | leave open | Only used as alternate supply |

**Firmware side**
```
#define RADAR_UART_NUM    1            // Serial1 on Core S3
#define RADAR_UART_RX     9            // Port B G9 ← radar TX
#define RADAR_UART_TX     8            // Port B G8 → radar RX
#define RADAR_UART_BAUD   115200       // LD2412 factory default — NOT 256000 like LD2410
Serial1.begin(RADAR_UART_BAUD, SERIAL_8N1, RADAR_UART_RX, RADAR_UART_TX);
```

**Baud rate correction.** The LD2412 factory default is **115200**, not 256000 (which is the LD2410 default). 115200 is actually easier on the UART — no need to consider lower-baud reprovisioning. Caveat: do NOT change the radar's baud rate via the configuration command unless you store the new value and reconfigure it on every boot — the README of the Arduino library explicitly warns about lockouts on baud changes.

**Library choice — flagged risk.** There are three candidate Arduino libraries:
- `ginkel/LD2412` — the repo name says LD2412 but the README still talks about "HLK-LD2410x". This is the most likely correct choice, but **needs bench validation** to confirm it actually parses LD2412's 14-gate frames (vs. LD2410's 9 gates). MIT licensed, ESP32-S3 examples included.
- `ncmreynolds/ld2410` — most mature, but written for LD2410's 9-gate frame. Will likely work for basic presence/motion but won't expose LD2412's extra 5 gates.
- `jacque99/ld2412` — ESP-IDF driver (not Arduino). Useful as a reference even if not used directly.

**Recommendation:** Bench-test `ginkel/LD2412` first. If it doesn't actually support 14 gates, the protocol is small enough (~10 frame types) to hand-roll a parser using the official Hi-Link protocol doc — this is the same vendor-thin-driver path the project already takes for the SCS0009 servos.

**Mechanical**
- 3D-print a case that places the LD2412 PCB **upright, facing forward**, antenna side unobstructed (no metal or carbon-fiber within ~3cm of the antenna). Plastic, fabric, and glass are radar-transparent.
- Mount on top of the K151-R base shell, in front of the StackChan body, at roughly head height of a seated user.
- Grove cable runs back to Port B on the Core S3. Keep the cable < 30cm to avoid signal integrity issues at 115200 on bare twisted pair.

### Software architecture deltas

The `sensor_awareness.h` shape from above stays the same, with these additions:

```
// New extern state — exposes radar's per-gate data for advanced uses
struct RadarState {
  uint16_t target_distance_cm;   // primary detected target distance
  uint8_t  moving_energy[14];    // per-gate moving energy
  uint8_t  static_energy[14];    // per-gate stationary energy
  bool     moving_target;        // any moving target in any gate
  bool     stationary_target;    // any stationary target in any gate
  uint32_t lastFrameMs;          // for staleness detection
};
extern RadarState radar;
```

Zone resolution comes primarily from radar `target_distance_cm` (no need to median-filter; the LD2412 firmware already filters internally). The ToF4M's reading is used for two things only: (a) the `onLeanIn` event (precise `<450mm` threshold), and (b) optional cross-validation — refuse to fire `onLeanIn` unless the radar also reports a stationary or moving target in gate 0 or 1. This dual-confirmation pattern is what makes the system "not half-assed" against single-sensor failure modes.

### New reliability notes (in addition to the original 10)

11. **Radar warmup is ~5s.** During warmup, `radar.lastFrameMs == 0` and all radar-derived events suppress. Same shape as the TMOS warmup window already in the plan — the boot sequence stage is shared.
12. **Radar baud rate is configurable from firmware on first boot.** Default is 256000; for noise immunity on a long pigtail to a 3D-printed case, consider configuring down to 115200 once and persisting in radar NVM. One-time configuration command from the firmware, not in the hot path.
13. **Per-gate sensitivity thresholds are persisted in the LD2412's own NVM.** Default ones are conservative for false-positive avoidance, which means real humans can fall below threshold in furthest gates. Plan to expose a "tune sensitivity" web UI page that writes thresholds to the radar — same pattern as audio-spectrum threshold tuning, just talking to the radar's UART config protocol instead of NVS.
14. **Frame parsing failure isolation.** UART frame CRC errors get counted in a debug field; if the error rate exceeds 10% over 30s, the radar is treated as offline (`radar_ok = false`) and all radar events suppress. The ToF4M continues providing close-range awareness in that mode — graceful degradation.

### Reaction table update

`onStillPresence` becomes much more reliable, so add one more reaction:

| Event | Initial reaction |
|---|---|
| `onStillPresence` AND `music_active` | Bot does a slow head-bob synced to `audio_spectrum.beatEnv` — "vibing with you" mode |

### Files this would touch (delta from the original list)

Add to "New":
- `vizbot/src/ld2412_driver.h` — UART framing, command/ACK protocol, frame parser. Wrap `iavorvel/MyLD2410` or `ncmreynolds/ld2410` if it covers the LD2412 frame extensions; otherwise hand-roll the parser (the protocol is straightforward).

Remove from "Modified":
- TMOS PIR–related changes are dropped.

Add to `platformio.ini`:
- `lib_deps`: pick the LD2412-compatible library after a quick compatibility test on bench hardware. Worst case, vendor a small one in-tree — the protocol is small enough to maintain.

### Verification approach (delta)

Add to the original verification list:

- **Radar bring-up on bench, no other sensors connected.** Use HLK's official PC tool (`HLKRadarTool`) over a USB-UART adapter first to confirm the radar is healthy and see what good values look like before writing any ESP32 code.
- **Sit-still test.** User sits motionless at desk for 5 minutes. Radar must report `stationary_target = true` continuously with no flapping. This is the headline test — if it fails here, no amount of firmware tuning will save it.
- **Walk-through test.** Person walks across the room front-to-back; expect a clean `onPresenceEnter` → `onFarPresence` → `onApproach` → `onLeanIn` (or `onApproach` only) → reverse-direction `onWithdraw` → `onPresenceLeave`. Should be reproducible 10/10 times with no spurious events.

### Bottom line

LD2412 is the right tool for the stationary-presence-in-a-small-room job and is worth breaking the M5Stack-only constraint for. ToF4M earns its keep as a cross-validator and lean-in detector. PIR drops out. Total new hardware is two sensors, one I2C and one UART, no bus conflicts.
