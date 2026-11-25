# Sleep — plan

Every bot (StackChan, 1.69, vizMini) goes to sleep at a set time and wakes at another.
Kevin's decisions, 2026-11-24:

| Question | Decision |
|---|---|
| Default schedule | **On**, sleep **23:00**, wake **08:00** |
| Wake by hand | **Hold to wake.** StackChan: hold head 2 s. 1.69: hold the screen. Mini: long-press. A tap only gets a sleepy peek. |
| After a hand wake | **Nods off again after 10 quiet minutes** while still inside the window |
| StackChan depth | **Deep rest**: head parks, servo power off, base LEDs off, mic off, screen dim |

## Behaviour (all bots)

- **Window.** Asleep from the sleep time to the wake time. A window may cross midnight.
  Sleep time == wake time means no window.
- **No clock, no schedule.** A bot that has not got the time never falls asleep by itself.
  The 1.69 in pendant mode (radio off) therefore sleeps only when told to.
- **Bedtime waits for play.** If someone touched the bot in the last nap span when the window
  opens, it waits until they have stopped for that long.
- **Hand wake.** Hold wakes it. Inside the window it goes back to sleep after the nap span
  (default 10 min) without interaction. Nap 0 = stays up until the wake time.
- **Sleep now.** Web/touch-UI "sleep now" works any time. It lasts until a hold-wake, or
  until the next scheduled wake time passes.
- **Morning.** At the wake time every override is cleared and the bot wakes with the wake chime.
- **While asleep:** eyes shut + Zzz, no sayings, no chirps, no random expressions, no party visits.

## Shape

- `sleep_schedule.h` — pure C++ (no Arduino), one identical copy in `vizbot/` and `vizmini/`
  (same rule as `bot_eye_model.h`). Holds the config, the window maths and the state machine.
  Takes minute-of-day (or "unknown") and `millis()`; returns FellAsleep / WokeUp.
- `tools/sleep-test/` — host unit tests (`-Wall -Wextra -Werror`, ASan + UBSan) and a check
  that both copies are byte-identical. Run with `tools/sleep-test/run.sh`.
- Each firmware owns a thin adapter: reads local time safely, applies effects, persists config.

### Settings (NVS)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `slpOn` | bool | true | schedule enabled |
| `slpAt` | u16 | 1380 | sleep minute-of-day (23:00) |
| `slpWake` | u16 | 480 | wake minute-of-day (08:00) |
| `slpNap` | u8 | 10 | minutes awake before nodding off again (0 = stay up) |

Web: `GET /bot/sleep` → state JSON; `?on=0|1&at=HH:MM&wake=HH:MM&nap=0..120` (any subset)
sets them; `?do=sleep` / `?do=wake` sleeps or wakes now. One bad argument → 400 and nothing
changes. `/state` carries `sleep:{on,at,wake,nap,asleep,cause,clock}`.
Touch UI (1.69): Info page rows Sleep / Bedtime / Wake up (30-minute steps) / Sleep now.

## Rollout — one PR per bot

1. **PR 1 — core + 1.69** (`feat/sleep-169`). Re-enables `BOT_SLEEPING` in `bot_mode.h`.
   Asleep: backlight to `SLEEP_BL_PERCENT`, ring LEDs off, buzzer silent. Hold on the screen wakes.
2. **PR 2 — vizMini** (`feat/sleep-mini`). Uses its compile-time zone (`TZ_POSIX`, US
   Eastern); a runtime timezone is a separate change. Asleep: eyes shut, OLED contrast eased
   to `SLEEP_CONTRAST`, strip off (without writing the user's LED setting), no party visits,
   moves, sayings or info screens. Long-press wakes; a press peeks.
3. **PR 3 — StackChan** (`feat/sleep-stackchan`). New `scSleepAO` owns the schedule and posts
   `SIG_MOTION_REST` / `SIG_MOTION_WAKE` (rule 1). Publishes `SIG_SLEEP_START/END` (rule 4).
   Battery care must not wake a sleeping head: `SIG_MOTION_REST/WAKE` carry a reason and
   MotionAO wakes only when none is left. Mic: wake stream and FX paused. `SIG_TOUCH_HOLD`
   (or a screen hold) wakes. Interaction suite passes from a cold boot (rule 5); it switches
   the schedule off for the run and has a sleep test.

Not in scope now: voice "go to sleep" through vizbrain, per-weekday schedules.
