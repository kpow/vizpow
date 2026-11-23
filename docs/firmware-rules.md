# vizBot firmware rules

These keep StackChan vizBot on the Active Object design
([stackchan-target-design.md](stackchan-target-design.md)) instead of drifting back into
the blocking superloop it replaced. Every change follows them. The core is `vizbot/ao_core.h`.

1. **Hardware only through its owner.** Servos belong to MotionAO, the I2C bus to BusAO
   (`i2cMutex`), the mic, speaker and amp to AudioAO, and touch to TouchAO. To use one, post
   an event with `aoPost(scMotionAO, aoEvt(SIG_MOTION_GESTURE, …))`. Never call `scChan.*`,
   `M5.Mic` or `M5.Speaker`, or write to the bus, from anywhere else.
2. **Never block.** Don't call `delay()`, wait in a loop or poll a flag in `loop()`, in an AO
   or in a web handler. A handler does one event and returns. Anything slow is split into
   states or timer events.
3. **Bus: lock or skip.** Take the bus with `i2cTry(0)`. If the lock fails, skip this frame
   or re-post the event, and never touch the bus without the lock.
4. **Anything that draws power publishes it.** Servo torque, LEDs, the amp, the mic, the
   camera and the power rails each `aoPublish` their start and end signal, so touch can
   ignore the disturbance. A new load gets a new signal in `AoSig`, and its name goes in
   `AO_SIG_NAMES` so it shows on `/debug/timeline`.
5. **One feature per branch and PR.** Before it merges, `tools/soak/interactions.py --touch`
   passes on mac, started from a cold boot. Bench builds use a dot version suffix
   (`3.12.1.name1`). A version with a suffix is never committed.

New subsystem? It becomes an owner (an AO or a cooperative tick on the loop thread) with its
own events, not a global flag that other code reads.
