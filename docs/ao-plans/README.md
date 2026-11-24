# Active Object port plans

One plan per owner, written before its stage starts. The design is
[../stackchan-target-design.md](../stackchan-target-design.md).

## Core conventions (stage 1 builds these; plans assume them)

- **Files:** `vizbot/ao_core.h` (event type, bus, base), `vizbot/ao_timeline.h`
  (ring buffer, `/debug/timeline`). One header per AO: `vizbot/ao_<name>.h`.
- **Event:** `struct AoEvt { uint16_t sig; uint8_t src; uint8_t flags; int32_t a;
  int32_t b; uint32_t ms; }`, 16 bytes, passed by value. Signals are one `enum
  AoSig` in `ao_core.h`. Large payloads live in an owner-held buffer, never in the
  event.
- **Active object:** a FreeRTOS task pinned to a core, with a queue (depth
  16–32), an `onEvent(const AoEvt&)` state machine, and a `tick` timer event for
  time-based steps. It never blocks inside `onEvent`: no `delay()`, no waits on
  locks or other AOs.
- **API:**
  - `aoPost(ao, evt)` sends to one AO; it never blocks the sender, and a full
    queue is counted and logged.
  - `aoPublish(evt)` goes to every subscriber and to the timeline.
  - `aoSubscribe(ao, sig)` adds a subscriber.
  - Callable from any task and from web handlers.
- **Disturbance signals** (published by owners, consumed by TouchAO):
  `SIG_MOTION_START/END`, `SIG_TORQUE_ON`, `SIG_RAIL_SWITCH`, `SIG_LED_PUNCH`,
  `SIG_AMP_ON/OFF`, `SIG_MIC_START/STOP`, `SIG_NET_TX_START/END`,
  `SIG_CAMERA_START/END`.
- **Strangler rule:** each step leaves the firmware building and working. Old
  entry points become thin shims that post events, until nothing calls them.
- **Boards:** `stackchan` is the target, but every change must keep the other
  envs in `vizbot/platformio.ini` building (guard with the existing board
  macros).
