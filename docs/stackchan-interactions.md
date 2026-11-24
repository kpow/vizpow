# StackChan vizBot: how the subsystems collide

Phase A of `/stackchan-stable`, written 2026-11-21 from a read of the `stackchan`
firmware (3.12.0 + touch-context logging) and its libraries. Nothing was changed
to write it. **[V]** = read in source. **[I]** = inferred, still to be measured.

## The short version

1. **Everything shares one I2C bus, with three locking schemes that don't cover
   each other.** The head-touch chip sits on it with 11 other chips. About 450
   transactions a second hit it, most from the base LEDs. Our lock is skipped by
   the IMU, proximity sensor, mic, speaker and backlight code. The camera uses a
   different driver that no lock covers. Most callers carry on when our lock times
   out. **[V]**
2. **The main loop does far too much and blocks.** It reads touch, renders the
   screen, drives the LEDs, polls 6 sensors and writes flash. Then a tap runs a
   1.2–1.5 s blocking nod or shake, during which touch, screen, LEDs, servo
   watchdog and command queue all stop. Recovery and battery rest block for up to
   4.6 s. Core 1 has no watchdog. **[V]**
3. **Load steps hit the touch chip with no warning.** Servo torque comes on at
   every idle move (every 2–15 s), LEDs flash at 2x brightness, the speaker amp
   toggles on every voice turn, and the mic codec resets twice per turn. Only 5 V
   rail switches ask the touch chip to re-reference, and even that lands in the
   middle of a servo glide. **[V order, I effect]**
4. **Subsystems coordinate with flags and sleeps, not owners.** For example: the
   voice task "lets the analyzer see a flag" with a 60 ms sleep. The listen beep is
   timed by polling. Web handlers on core 0 move the head, rail-cycle the servos,
   start the camera and switch the mic directly, bypassing the command queue.
   **[V]**
5. ~~The build depends on this Mac.~~ **Corrected 2026-11-22:** the M5GFX in
   the build is the stock 0.2.36 release, byte-identical to the GitHub tag. The
   build is reproducible. The one unpinned library (M5-SAM2695) is now pinned.

These feed each other. One phantom touch sets off a chain of these, as in
chain 1 below.

## Resource matrix

Rows: subsystems. Cells: how each uses the resource, and under which lock.
"—" = doesn't use it.

| Subsystem | I2C bus (100k/400k) | I2S port | Supply load step | Task (core, prio) | Internal RAM | WiFi TX |
|---|---|---|---|---|---|---|
| Head touch (Si12T 0x68) | read every frame 100k; 14-write start on boot / heal / rail switch; **reads even if lock timed out** | — | sensitive to all of them | loop (1, 1) | — | — |
| Base LEDs (PY32 0x6F) | 13 locked tx **per frame** (~400/s) 100k; lock result ignored | — | up to ~180 mA, flash 2x, beat punch, no notice | loop | — | — |
| Screen touch + buttons (FT6336, AW9523, AXP) | `M5.update()` every frame 400k; skipped if lock busy | — | — | loop | — | — |
| IMU (BMI270) | every frame 400k, **no lock** | — | — | loop | — | — |
| Proximity (LTR553) | 10 Hz 400k, **no lock** | — | — | loop | — | — |
| Display / backlight | AXP RMW on brightness change, **no lock** | — | backlight | loop, ~30 ms SPI push/frame | canvas falls back to internal | — |
| Servos (BSP motion) | VM_EN via PY32 on rail cycle | — | torque-on every move, 2–15 s; 1800 ms glides | motion_task (**any core**, 10) | 4 KB stack | — |
| Idle drift / gestures | — | — | moves | loop; nod/shake **block 1.2–1.5 s** | — | — |
| Voice turn | ES7210 RESET + 29 writes ×2, AW88298 ×2 per turn, **no lock** | mic end/begin ×2, speaker begin/end | amp on (even at volume 0 in Mac mode), MICBIAS steps | voice (0, 1) + mic/spk tasks (**any core**, 2) | DMA + 2.5–4 KB stack per begin; 8 KB tx/rx | 384 KB WAV upload, no gating vs cloud |
| Wake stream | mic begin on start/restart | mic always on | — | voice (0, 1) | DMA | 1 KB UDP / 32 ms, bursts of 32; pauses for cloud |
| Audio FX / spectrum | mic begin/end on toggle, from loop or web | `record()` restarts a stopped mic | — | loop | 5.3 KB static | — |
| Sounds / party / shutter | — | — (MIDI over UART) | — | loop | — | — |
| Camera (GC0308) | **IDF driver, outside every lock**; holds our lock up to 1 s, stretches others' waits to 3 s | — | sensor always powered | wifi_srv (0, 1) | **30–32 KB contiguous DMA block per photo**, unchecked | JPEG serve |
| Cloud sync | battery read in OTA check | — | TX bursts at 19.5 dBm | cloud (0, idle) | ~41 KB dip, unexplained; TLS in PSRAM | TLS every 60 s, ~43 KB |
| Web server | `/state` reads INA226 every poll; LED, battery, touchraw, power handlers | `/bot/audiofx` toggles the mic | — | wifi_srv (0, 1) | — | responses |
| Power / battery care | AXP + INA226 1–2 s; rail switches hold lock 200–700 ms | — | 5 V output on/off, servo rail | loop, blocking | — | — |
| Power log | INA226 / AXP / RTC 1 s | — | — | loop; **flash write every 15 s** (stalls both cores) | — | — |

## Collisions, ranked by how much they explain

1. **Touch chip vs everything electrical. [V order, I mechanism]** The chip
   measures tiny capacitance changes against a reference. These all move its
   supply or ground with no notice to it:
   - servo torque-on at every idle move
   - LED flashes and beat punches
   - the amp toggling and the mic codec resetting on every voice turn
   - WiFi TX bursts at 19.5 dBm with power save off

   Under vizBot they stack at random. In the bare chip test they don't happen.
   **Measured 2026-11-21 (bare soak, below):** the same chip on the same base,
   alone, gave 1 weak contact in 2 h 8 min and no storms, latches or heals. Under
   vizBot it gave about 10 contacts and 1.6 heals an hour. So the chip and base are
   fine alone; the storms come from what vizBot does around them. **[V]**
2. **Touch reads vs the I2C bus. [V]**
   - `scReadTouch()` reads even when our lock timed out.
   - A failed read keeps the last result, so a contact can look latched.
   - A byte read with a late failure is still delivered. A garbage `0x3F`/`0xFF`
     parses as all three pads at full strength, which is the storm signature
     **[I]**.
   - The only driver that can garble a read is the camera's, which no lock covers.
3. **Main loop blocking vs touch, servos and UI. [V]** The loop stops for these:

   | What | Blocks for |
   |---|---|
   | Nod / shake (after every tap) | 1.2–1.5 s |
   | Servo recovery | 2.1–4.6 s |
   | Battery rest / wake | 1.3–3 s |
   | 5 V learn | 1.1 s |
   | Any lock while the camera holds the bus | up to 3 s |

   Meanwhile the 8-slot command queue drops commands, the listen beep plays late
   over the voice turn, and touch reading resumes while the head is still settling.
4. **Web handlers acting on hardware from core 0. [V]**
   - A blocking nod or shake with no sound pump (`/bot/head/preset`).
   - A full servo rail cycle (`/bot/servo/reinit`). It has no re-entry guard
     against the loop's watchdog doing the same.
   - Camera start, mic start/stop, chill mode.
   - The I2C lock held through a 500 ms `delay()` (`/debug/power?ext`).
5. **Audio ownership by flags. [V]**
   - The races: FX-off kills the wake mic, and `record()` silently restarts a mic
     another task ended.
   - `micRunning` is written from 3 tasks.
   - Every mic begin/end allocates and frees DMA buffers and a task stack in
     internal RAM, next to the camera's 32 KB block and lwIP.
6. **Internal RAM and WiFi buffers. [V/I]**
   - About 137 KB of heap at boot, and no budget anywhere.
   - The camera allocates 32 KB contiguous without checking.
   - A voice upload and a cloud TLS sync can run at once over 8 static TX buffers,
     and the wake stream already hits out-of-buffers.
   - The 41 KB internal dip during cloud sync is still unexplained.
7. **Core 0 crowding. [V]**
   - wifi_srv, voice, cloud and the WiFi/lwIP tasks share core 0.
   - The mic and speaker tasks (priority 2, unpinned) can land on either core.
   - Moving tasks to idle priority (the cloud fix) is a symptom of this.
   - Flash writes (power log every 15 s, NVS, LittleFS) pause both cores.
8. **Mixed bus speeds. [V speeds, I risk]** 100 kHz (Si12T, PY32, AW9523,
   camera) and 400 kHz (everything else) transactions run back to back, with the
   clock reprogrammed each time. Whether the Si12T or PY32 tolerates 400 kHz
   traffic to other addresses is not checked.

## Chains: faults that feed each other

1. **Phantom tap → reaction → next phantom. [V path, I trigger]**
   - A phantom tap fires a nod or shake: servo torque, the loop blocked for
     1.2–1.5 s, and an LED flash.
   - Or it starts a listen: amp off, mic codec reset, MICBIAS step about 0.4 s
     later.
   - Each of those is an unannounced load step, and touch reading resumes in the
     middle of it. Seen twice on 11-21: a new blip came about 0.2 s after a
     phantom-triggered listen started.
2. **End of voice turn → wake mic restart → phantom → new listen. [V path, I
   trigger]** Every turn ends with a mic begin while idle. A blip then starts
   another turn. A blip during listening or speaking cancels the turn.
3. **Rail switch → touch re-reference during a servo glide. [V]**
   - Recovery and battery wake ask for a touch restart at +1.5 s, then block the
     loop for 2–4.6 s.
   - The restart therefore runs on the first loop after, just as the 1800 ms
     return-home glide starts.
   - So the chip takes its reference under a servo load step.
4. **Self-heal → quiet → drift → storm. [V log]** Today's cycle on mac: a heal
   resets the chip, then 15–37 minutes quiet, then blips cluster, then a 15 s
   latch, then another heal. Whatever drifts the reference keeps running.
5. **Camera photo → bus held → everything stalls → catch-up burst. [V]**
   - The loop's locks wait up to 3 s, twice per photo.
   - Afterwards LEDs, touch, and the queued commands all run at once.

## What this means for Phase B

These are the gaps to close, in the platform's normal way:

- **One owner for the I2C bus.** A bus task with a request queue that every
  device goes through, the M5 library calls and the camera included. No more
  "lock timed out, read anyway".
- **One owner for audio.** The mic stays running between turns, so there is no
  codec reset and no MICBIAS step per turn. One state machine decides mic,
  speaker or wake.
- **Nothing blocks the loop.** Gestures, recovery, rest and wake become state
  machines. Web handlers only queue commands.
- **A power and disturbance schedule.** Subsystems announce load steps on an
  event queue. Touch ignores those windows, and re-references only when the bot
  is still.
- **Budgets and a system timeline.** RAM, DMA and WiFi-buffer budgets with
  headroom, plus one timeline of every subsystem's state changes.
- **A reproducible build.** Done in stage 0: there was no local patch, and the
  last unpinned library is pinned.

## Measured: bare touch chip soak (2026-11-21)

Touch test 1.2 on mac: vizBot's own chip start and heal code, nothing else
running (no servos, LEDs, audio or camera; WiFi on, idle). Same unit, same base,
on USB.

| | Bare chip, 2 h 8 min | vizBot, 8 h 36 min before it |
|---|---|---|
| Reads | 144,767 | — |
| Contacts | **1** (0.5 / h) | 84 (9.8 / h) |
| Level-3 contacts (fire taps) | **0** | 63 taps fired |
| 15 s latches / heals | **0 / 0** | 14 heals (1.6 / h) |
| Chip restarts | 1 (boot) | — |
| Registers at end | identical to boot | — |

- The one contact: all three pads at level 1 for 5.1 s at minute 110. Too weak to
  fire a tap. It may have been a person nearby; unknown.
- vizBot's cycle (heal, 15–37 min quiet, then a storm) would have run 3 or more
  times in 2 h. It ran zero times.
- **Conclusion:** the Si12T and skunky-style base fault are ruled out for mac.
  The storms need vizBot's activity: collisions 1–3 and chains 1–4 above.

Also seen on the reboot back to vizBot: 53 KB free internal heap and a largest
block of 31.7 KB at 2.6 minutes up. The camera wants a 32 KB contiguous block,
so a photo can already fail to allocate this early. **[V numbers, I effect]**

## Measured: cold boot vs warm boot (2026-11-22)

Same firmware (3.12.0.ctx1), same settings, party on, nobody touching.

| | Cold boot (power off/on) | Warm boot (OTA restart) |
|---|---|---|
| Chip registers before our start | factory defaults `BB BB BB BB BB BB 22 05 FE 3F FE 3F` | already ours `33 33 33 33 33 BB 22 07 00…` |
| Contacts | **about 17 a minute**, 75 in 5 min | **0 in 2.3 min** |
| Heals | 7 in 2 min, and still storming | 0 |
| Party off (LED beat flashes) | dropped to about 7 a minute, did not stop | — |

- `VizStackChan::beginHardware()` (`stackchan_base.h`) starts the chip right
  after `setServoPowerEnabled(true)` and `servo_init()`, while the servos home,
  then asks for a second start 1.5 s into the loop. **[V]**
- After a warm boot the chip keeps the settled state it had before, so the boot
  start does no harm. After a cold boot the chip starts from scratch, and its
  first reference is taken under the servo, LED and WiFi start-up load. **[V order,
  I mechanism]**
- Once it storms, each heal restarts the chip in the middle of the nod or listen
  that the last phantom fired (chain 1 + chain 3), so it never gets a quiet
  reference. It does not recover by itself. **[V]**
- This explains why mac's touch looked worse after every power cut, and why bare
  tests (started from a warm OTA) looked clean.

## Still to measure

- Whether a garbage all-ones Si12T byte ever arrives: log raw bytes and read
  failures.
- How long camera init holds the bus, and how long each loop frame takes under
  load: a loop-time histogram.
- The 41 KB internal RAM dip during cloud sync: log free RAM between each stage
  of `cloudPost`.
- Si12T and PY32 datasheets: are they rated for 400 kHz bus traffic?
