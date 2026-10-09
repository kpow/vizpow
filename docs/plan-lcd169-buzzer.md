# Sprint: 1.69 buzzer

Waveshare ESP32-S3-Touch-LCD-1.69 has a **passive buzzer** on the board. Goal: drive it
from the existing `BotSounds` sequencer so the 1.69 chirps on a few interactions.

## Hardware facts

| Item | New board rev | Old board rev |
|---|---|---|
| Buzzer | GPIO42 | GPIO33 |
| SYS_EN / SYS_OUT | GPIO41 / GPIO40 | GPIO35 / GPIO36 |
| RTC_INT | GPIO39 | GPIO41 |

- Our `lcd-169` env uses `qio_opi` (octal PSRAM), which reserves GPIO33–37, so our board
  is almost certainly the **new rev → GPIO42**. Confirm: new rev has the model name
  printed on the PCB.
- Passive = needs a square wave (LEDC `ledcWriteTone`), not just HIGH.
- Waveshare FAQ: leaving the buzzer pin floating keeps the buzzer drawing current,
  loading the LDO and heating the board. **Firmware never touches GPIO42 today**, so it
  floated before v3.7.1. Ruled out as the cause of the 19.5 dBm WiFi failure (step 5).

Source: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.69

## What already exists

- `bot_sounds.h`: timeline sequencer (`play(SEQ_*)`, `playTone`, volume, enabled),
  with MIDI and an M5.Speaker sine fallback. Whole file is `#ifdef TARGET_CORES3`.
- Call sites (`bot_mode.h` wake/tap/shake/boot, `touch_ui.h` `uiSound`,
  `task_manager.h` web tone/seq/volume, `settings.h` persistence) are all gated on
  `TARGET_CORES3`. `touch_ui.h:403` literally says "the 1.69 stays silent".

## Plan

1. **Bring-up** — at boot, `pinMode(42, OUTPUT); digitalWrite(42, LOW)`. Add a test tone
   via the existing web `playTone` path. Flash, cold-boot, confirm it beeps and the board
   runs cooler. (~30 min)
2. **Buzzer backend** — add `HAS_SOUND` flag (CoreS3 + 1.69). Split `bot_sounds.h` so the
   sequencer compiles without M5Unified; 1.69 backend = LEDC tone on GPIO42, monophonic
   (highest note wins per offset). Volume = PWM duty. Pin back LOW when idle. (~2 h)
3. **Wire interactions** — swap `TARGET_CORES3` guards for `HAS_SOUND` at call sites;
   tune which sequences sound OK on a piezo (short, high, few notes). (~1 h)
4. **Settings** — expose Sound on/off + Volume rows in the 1.69 touch UI; default on, mid volume.
   (~30 min)
5. **Verify** — WiFi TX power retest at 19.5 dBm, heat check, no glitches in render loop.

## Candidate interactions

Tone: "subtle but not too subtle" (Kevin, 2026-10-08).

Do-now: tap boop, touch-UI swipe/dismiss/toggle/confirm, wake chime, boot chime.
**Idle chirp**: if nobody has interacted for 5+ min, the bot occasionally makes a small
sound on its own (paired with an expression change). Randomized, not on a fixed timer.
Later: notifications / cloud command alert, timer/alarm, shake rattle.

## Status

- [x] Step 1 code: `buzzer.h`, `BUZZER_PIN 42`, pin LOW at boot, `/bot/sound?freq=&dur=`
      on 1.69. v3.7.1, builds for lcd-169 + stackchan. Branch `feat/lcd169-buzzer`.
- [x] Step 1 hardware test: beeps (2700 Hz). Heat check still open.
- [x] Step 2 code: `HAS_SOUND` flag; `bot_sounds.h` runs on the piezo (lead channel
      only, top note of chords, lifted to >= G5 via `BUZZER_MIN_NOTE`, volume = squared
      PWM duty). `/bot/sound?seq=`, `/bot/sequences`, `/bot/volume`, saved sndOn/sndVol
      and the web Sounds card now work on the 1.69. v3.7.2 flashed.
- [x] Step 2 ear test: "sounds good" — no tuning needed.
- [x] Step 3 code: tap boop, wake, shake, boot chime, touch-UI swipe/dismiss/toggle/confirm
      now sound on the 1.69 (`HAS_SOUND` guards). Idle chirp (1.69 only): after 5 min with
      no touch, first chirp within 0-3 min, then every 4-12 min; random sound + matching
      face; silent 22:00-08:00 local. Any touch-UI touch counts as interaction. v3.7.3.
- [x] Step 3 test: tap/swipe sounds heard. Idle chirp not yet heard in the wild.
- [x] Step 4: Settings > Light tile on the 1.69 now opens a "Light" page with Screen (->
      brightness sheet), Sound toggle, Volume slider (previews a boop while dragging). Dock
      Light tile still goes straight to brightness. Verified on device via /debug/touch +
      /debug/screen; toggle and slider persist through /state. v3.7.5.
- [x] Step 5 WiFi: 19.5 dBm with buzzer pin held LOW = 0/6 connects (same silent
      no-associate -> AP fallback as before); 15 dBm = 5/5. The floating buzzer pin is
      NOT the 19.5 dBm cause. 15 dBm stays.
- [x] Step 5 stress: 6 songs + 21 injected gestures + 12 volume changes in ~45 s — no
      panic/reset on serial, bot responsive after.
- [ ] Step 5 heat: compare board temperature by touch vs. pre-3.7.1 (needs Kevin).
- [ ] Idle chirp heard in the wild (quiet hours 22:00-08:00, so daytime).
