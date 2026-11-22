# Stage 2 plan: MotionAO

**Status 2026-11-22: built (fw 3.12.1.ao3), bench-checked on mac.** MotionAO runs on the loop
thread (cooperative, like TouchAO), not as its own task: internal RAM can't spare the stack.
Done: steps 1–5 (moves, gestures, idle arbitration, watchdog + rail cycle, rest/wake/power-off by
battery care), `/state` angles from targets. Timeline check: web nod/shake/reinit answer in
20–100 ms (were 1.2–4.6 s), nod 1.5 s, a move mid-gesture and a second reinit are dropped and
published, the touch reference lands after the rail cycle ends. Left: `/debug/servo` still reads
the servo bus from the web task (every 5 min); web/UI power-off still cut the rail directly
(terminal path); dead `scFirePetReaction` / `scFireRecenter` kept until Kevin says.

Owner of the head servos, the BSP `Motion` object and the servo rail (VM_EN). Written 2026-11-22
from source (read-only). **[V]** = read in source, **[I]** = inferred, **[?]** = not verified.
Line numbers are against the working tree (3.12.0 + uncommitted touch-context logging). GitNexus
was 11 commits behind HEAD, so every caller below was confirmed with grep.

## 1. Call sites today

Loop = Arduino `loopTask`, core 1, prio 1. Web = `wifi_srv`, core 0, prio 1. BSP `motion_task`
(prio 10, `xTaskCreate`, unpinned: either core) runs `Motion::update()` every 20 ms under the
Motion mutex and does the actual UART writes.

| Site | Caller | Task / core | Does | Blocks |
|---|---|---|---|---|
| `stackchan_base.h:171-173` `beginHardware` | setup | loopTask 1 | rail on, `delay(500)`, `servo_init()` | 0.5 s, boot only |
| `stackchan_base.h:322/328` `scMoveYaw/Pitch` | everything below | caller's | Motion mutex + `moveYaw/Pitch`; writes `scHeadBusyUntilMs` | mutex wait + 1 bus read (auto angle sync) [V] |
| `stackchan_base.h:338` `scGoHome` | chill, recenter, reinit, wake, touch UI | caller's | two moves | as above |
| `stackchan_base.h:786/798` `scNodGesture/scShakeGesture` | `scFireNod/Shake`, web preset | caller's | 6 moves with `scGestureDelay` busy-wait | **1.5 s / 1.2 s** |
| `stackchan_touch.h:383/396` `scFireNod/Shake` | `vizbot.ino:633/638` (tap), `touch_ui.h:1553-1554` | loop 1 | face, LEDs, sound, then the gesture | **1.5 / 1.2 s** |
| `stackchan_touch.h:457` `scFireChillMode` | `vizbot.ino:642`, `touch_ui.h:1546`, `web_server.h:2406` | loop 1 **or web 0** | `scGoHome(800)` | short |
| `stackchan_touch.h:309/413` `scFirePetReaction`, `scFireRecenter` | **no callers** [V grep] | — | moves; recenter has `delay()` | dead code |
| `stackchan_idle.h:140,189,213` drift + sweep | `vizbot.ino:661` | loop 1 | moves every 2–15 s | short |
| `stackchan_idle.h:221` `headStillMoving` | idle | loop 1 | `Motion.isMoving()`: bus read once the anim is done | ms |
| `bot_mode.h:812-813` party code mode `headReq` | loop render | loop 1 | moves | short |
| `touch_ui.h:1555-1557` look up/down, center | screen rows | loop 1 | moves | short |
| `stackchan_base.h:488` `scServoWatchdogTick` | `vizbot.ino:655` (skipped in voice turn) | loop 1 | 2 pings/s under `ScBusLock` (Motion mutex) | ~4 ms; deaf ~70 ms; then recovery |
| `stackchan_base.h:371` `scRecoverServos` | watchdog, `web_server.h:2358` reinit | **loop 1 and web 0, no re-entry guard** | dip, `delay(1300)`, rail off (I2C lock), `delay(300)`, on, `delay(500)`, pings ≤2.5 s, glide | **2.2–4.6 s** |
| `stackchan_base.h:626/645` `scRestServos/scWakeServos` | `scBatteryCareTick` 719/726/733 | loop 1 | park + `delay(1300)`, rail + 5 V ext off; wake: on, `delay(500)`, pings, home | **1.3–1.8 s / 0.6–3.5 s** |
| `stackchan_base.h:722` battery flat power-off | battery tick | loop 1 | rest + `delay(300)` + `powerOff` | ~1.6 s, terminal |
| `stackchan_base.h:303` `scSetServoPower(false)` | `web_server.h:2428` poweroff, `touch_ui.h:1950` | web 0 / loop 1 | rail off before `powerOff()` | terminal |
| `web_server.h:2293` `/bot/head/set_angles` | HTTP | web 0 | moves | short |
| `web_server.h:2318` `/bot/head/preset` | HTTP, soak `head` action | web 0 | nod/shake **without** `shakeReacting`, so idle drift on core 1 can retarget mid-gesture | **1.5 / 1.2 s** |
| `web_server.h:2347` `/bot/head/recenter` | HTTP | web 0 | `scGoHome` | short |
| `web_server.h:2357` `/bot/servo/reinit` | HTTP | web 0 | `scRecoverServos` + `scGoHome` | **2.2–4.6 s** |
| `web_server.h:2815` `/debug/servo` | HTTP, scorecard every 5 min | web 0 | ~13 register reads per servo **holding the Motion mutex** (stalls `motion_task`) | ~30–80 ms [I] |
| `web_server.h:1418-1419` `/state` angles | HTTP poll | web 0 | `scReadServoPos` | 2 bus reads under the mutex |

**Finding [V]:** `scReadServoPos` and the `/state` comment say "cached animation value, not a bus
read". It is a bus read: `ScsServo::getCurrentAngle()` (BSP `M5StackChan.cpp:185`) overrides the
virtual and calls `ReadPos`. So `/state` polls the servo UART from core 0. Every move also reads
(auto angle sync teleports from `getCurrentAngle()`), and the 50 Hz auto torque release reads
`TorqueEnable` once the head rests.

## 2. MotionAO

`vizbot/ao_motion.h`, core 0, prio 3 (below `motion_task` 10, above wifi_srv 1), queue 24. Only
MotionAO calls `scChan.Motion.*`, `ScBusLock`, `scProbe()` and `setServoPowerEnabled`. One tick timer:
20 ms while a sequence runs, 1 s otherwise (watchdog pings).

**Events in** (new `AoSig` values; payload in `a`/`b`):
- `MOTION_MOVE` a = yaw | pitch packed as two int16 (`NO_AXIS` = skip), b = timeMs | class<<16
- `MOTION_HOME` b = timeMs; `MOTION_GESTURE` a = NOD / SHAKE / LOOKUP / LOOKDOWN / LEFT / RIGHT
- `MOTION_RECOVER` (src = watchdog or web), `MOTION_REST`, `MOTION_WAKE`, `MOTION_RAIL_OFF` (power-off)
- `MOTION_IDLE_GATE` a = bit (CHILL, VOICE, PARTY_CODE, REACTING), b = set/clear;
  `MOTION_PERSONALITY` a = index; `MOTION_PROBE` (refresh the `/debug/servo` snapshot); `SIG_TICK`

**Events out** (`aoPublish`, all go to the timeline):
- `SIG_MOTION_START` a = kind (idle, web, gesture, recover, rest, wake, boot), b = expected ms
- `SIG_MOTION_END` a = kind, sent after BSP `isMoving()` is false (checked on ticks)
- `SIG_TORQUE_ON` at a move start when the head has been still > 200 ms. **[I]** BSP releases torque
  200 ms after rest and `WritePos` re-enables it; the AO infers this, it does not observe it
- `SIG_RAIL_SWITCH` a = 0/1, b = reason (recover, rest, wake, poweroff)
- New, timeline only: `SIG_SERVO_DEAF`, `SIG_SERVO_RECOVERED` (b = ok), `SIG_MOTION_DROPPED`

**States** (each step is a `SIG_TICK` deadline; nothing calls `delay()`):

| State | On entry / per tick | Leaves on |
|---|---|---|
| `BOOT_SETTLE` | ping every 100 ms (≤ 2.5 s) | answer → publish END(boot) → `STILL` |
| `STILL` | 1 s watchdog ping (gated off by VOICE); idle drift timer | MOVE / HOME / GESTURE / idle due → `MOVING`; 3 deaf → `RECOVER`; REST → `REST` |
| `MOVING` (sub: idle / web / gesture step k of n) | gesture = step table `{axis, angle, ms}`; next step at its deadline | last step + `!isMoving()` → END → `STILL` |
| `RECOVER` | DIP 1300 → RAIL_OFF 300 → RAIL_BOOT 500 → WAIT_ANSWER (100 ms pings, ≤ 2500) → GLIDE 1800 | ok → `STILL`; fail → `STILL` + backoff (15 s doubling to 5 min, as today) |
| `REST` | PARK 1300 → RAIL_OFF (+ tells battery care) → `RESTED` (moves dropped, watchdog off) | WAKE → RAIL_ON → 500 → WAIT_ANSWER → HOME 1800 → `STILL` |
| `OFF` | rail off, then it ignores everything | terminal |

**Arbitration:** safety (recover, rest, wake, off) beats gesture and web, which beat idle. Idle
asked during anything else is skipped. A gesture or web move arriving during a gesture or a safety
state is dropped and counted (today the blocked loop never read those taps). A `RECOVER` while
recovering is ignored: this is the missing re-entry guard. The AO is the only writer of
`scHeadBusyUntilMs` and `scServosResting`. Old readers keep working: camera, touch context bits
5–6, party code mode, `/state`.

**Telemetry:** `/state` angles come from AO-held atomics (last target plus last read), so the web
task makes no bus reads. `/debug/servo` posts `MOTION_PROBE` and returns the last snapshot with
`ageMs`. `reboots` stays live for the scorecard.

## 3. Migration steps (each builds all envs and works)

Before every bench build, bump `FIRMWARE_VERSION` (`3.12.1.ownN`, dot suffix only). Build
`pio run -e stackchan -e m5cores3 -e faces -e lcd-169 -e lcd-169-dev -e lcd-13`.

1. **Skeleton + move shims.** Add `ao_motion.h` (whole file inside `#ifdef BOARD_HAS_STACKCHAN_BASE`;
   signals go in the unconditional `AoSig` enum). Rename the bodies to `scRawMoveYaw/Pitch`, called
   only by the AO. `scMoveYaw/Pitch/GoHome` keep their signatures and post `MOTION_MOVE/HOME`. The
   old blocking sequences (gestures, recover, rest) still `delay()` between posts, so their timing is
   about the same. START/END/TORQUE_ON go live here. No other files change.
2. **Gestures.** `scNodGesture/scShakeGesture` post `MOTION_GESTURE` and return. `scFireNod/Shake`
   keep face, bubble, LEDs and sound on the caller's task. Drop `scGesturePumpSound`, since the loop
   now keeps pumping. The web preset posts too, with class = web. Keep **HTTP 200**: the scorecard
   counts only `status == 200`.
3. **Idle drift into the AO.** Move the `ScIdleServo` logic into the AO's `STILL` timer.
   `vizbot.ino:661` becomes edge-triggered `MOTION_IDLE_GATE` posts (chill, voice turn, party code,
   `shakeReacting`) plus `MOTION_PERSONALITY` on change. `headStillMoving` uses the AO state.
4. **Watchdog + recovery.** `scServoWatchdogTick` moves into the AO tick. `scRecoverServos()` and
   `/bot/servo/reinit` post `MOTION_RECOVER`; reinit answers `{"ok":true,"queued":true}`, a change
   in its body. Rail I2C: `ScI2cLock` with a 20 ms try, retried on the next tick, never a long wait
   (BusAO in stage 4 replaces it). Publish `SIG_RAIL_SWITCH`. Drop the `scTouchAskRestart` calls on
   this path **only if** stage-1 TouchAO consumes `SIG_RAIL_SWITCH` + `SIG_MOTION_END`.
5. **Rest / wake / power-off.** Battery care keeps the decisions and the 5 V ext output. It posts
   `MOTION_REST`/`WAKE` and acts on `SIG_RAIL_SWITCH` replies, keeping today's order: servo rail off,
   then 5 V off. Power-off paths (`web_server.h:2428`, `touch_ui.h:1950`, `stackchan_base.h:722`)
   post `MOTION_RAIL_OFF` and wait for the reply (≤ 300 ms), then call `powerOff()`.
6. **Telemetry + lock audit.** `/state` and `/debug/servo` read AO snapshots. In debug builds,
   assert in `scRaw*`/`ScBusLock` that the caller is the MotionAO task. Expose a counter of foreign
   callers (should be 0).
7. **Cleanup.** Delete `scGestureDelay`, the blocking bodies and the wrong "cached" comments. Ask
   Kevin before deleting the dead `scFirePetReaction` / `scFireRecenter`.

Other boards: only `stackchan` defines `BOARD_HAS_STACKCHAN_BASE`; `m5cores3` and `faces` are CoreS3
without the base. Every touched file is already inside that guard, so they only need to keep
compiling the shared `AoSig` enum.

## 4. Risks and unknowns

1. **Unblocking moves the collision onto touch [I].** Today the loop doesn't read touch, run LEDs
   or render during a nod. After this stage it does all three while the servos load the rail. Unless
   TouchAO ignores reads between `SIG_MOTION_START` and END + settle, chain 1 (phantom → nod →
   phantom) can get worse. Hard dependency on stage 1.
2. **BSP Motion is not a passive actuator [V code, I timing].** `motion_task` (prio 10, unpinned,
   can't be pinned without patching BSP [?]) still shares the mutex and the UART with the AO, and
   does its own torque reads and releases. Moves, `isMoving` and angle reads all touch the bus: with a
   deaf servo each costs ≥ 10 ms × retries, so one AO event can take tens of ms. `SIG_TORQUE_ON` is
   inferred, not seen. Later option: `setAutoTorqueReleaseEnabled(false)` with torque owned by the AO
   (exact events, but a heat/current risk, so it needs measuring).
3. **The rail is on the I2C bus until stage 4 [V].** VM_EN is a PY32 write under `ScI2cLock`, which
   the camera can hold for 1–3 s. A long wait would stall MotionAO, and a short try delays recovery or
   rest. Rest/wake also splits one locked block (VM_EN + 5 V) between two owners, so the ordering
   must be kept.

Other risks:
- Web semantics change: preset and reinit now return before the motion ends. The on-device UI
  ignores the bodies [V]. No other client found in ~/projects [V grep].
- Queue overflow under a heavy soak burst.
- Never call `Motion.*` while holding `ScBusLock`: it is a non-recursive `std::mutex`.

Not verified [?]:
- Whether SCS `WritePos` re-enables torque.
- Whether `servo_init()` physically homes the head.
- Whether `vTaskCoreAffinitySet` exists in this Arduino-ESP32 / IDF.
- The exact stage-1 API names: only `ao-plans/README.md` exists, with no TouchAO plan.
- Real `/debug/servo` and deaf-ping timings.

## 5. What proves it (timeline + 4 h soak, cold boot by Kevin)

- No loop gap > 100 ms tied to motion. Touch reads keep coming during a tap reaction.
  Before → after: 1.2–4.6 s stalls → 0.
- Every `SIG_MOTION_START` has an END. Nod ≈ 1.5 s, shake ≈ 1.2–1.45 s. No idle move inside a web
  gesture.
- Every `SIG_RAIL_SWITCH` is inside RECOVER, REST or OFF. Never two recoveries overlapping, never a
  rail switch < 300 ms after another from a different source.
- TouchAO's references land only after `SIG_MOTION_END` + settle, never inside a glide (chain 3
  gone). The boot reference comes after END(boot).
- Counters: foreign Motion callers 0, `SIG_MOTION_DROPPED` and queue-full ≈ 0 on normal load, AO
  max event time < 50 ms (healthy servos), AO stack headroom > 1 KB.
- Scorecard: servo reboots 0, reboots 0, head hits all 200 on heavy, phantom taps/holds no worse than
  stage 1.
