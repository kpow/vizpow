#ifndef AO_CORE_H
#define AO_CORE_H

// ============================================================================
// Active Object core (docs/stackchan-target-design.md)
// ============================================================================
// vizBot is moving from a blocking superloop to the Active Object pattern
// (the actor model for embedded firmware, as in Miro Samek's QP):
//   1. one owner per resource: an active object (AO) is the only code that
//      touches its hardware;
//   2. owners talk only by events: aoPost() to one AO, aoPublish() to every
//      subscriber and to the system timeline;
//   3. never block: onEvent() handles one event and returns.
//
// Scheduling. Internal RAM is too tight for a task stack per AO (largest free
// block ~31.7 KB, the camera needs 32 KB), so AOs on core 1 run cooperatively
// on the loop thread, as in QP's QV kernel: aoDispatch() runs each AO's queued
// events to completion, in priority order, once per loop pass. Events may be
// posted from any task (web, voice, cloud): the queues are FreeRTOS queues.
//
// Stage 1 builds this core, the timeline and TouchAO. Later stages move motion,
// audio, the I2C bus, UI and networking into AOs (docs/ao-plans/).

#ifdef BOARD_HAS_STACKCHAN_BASE

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "mem.h"

// ---- Signals -----------------------------------------------------------------
// One list for the whole firmware. Names for /debug/timeline: AO_SIG_NAMES.
enum AoSig : uint16_t {
  SIG_NONE = 0,
  SIG_TICK,              // time event: delivered to an AO every tickMs
  // Disturbances (published by whoever causes them; TouchAO listens)
  SIG_MOTION_START,      // a = ms until the head is still again (move + settle)
  SIG_RAIL_SWITCH,       // a = ms until the rail has settled
  SIG_LED_PUNCH,         // a = flash length ms
  SIG_AMP_ON,
  SIG_AMP_OFF,
  SIG_MIC_START,
  SIG_MIC_STOP,
  SIG_CAMERA_START,
  SIG_CAMERA_END,
  SIG_NET_TX_START,      // a = what: 1 voice turn, 2 cloud sync, 3 wake stream
  SIG_NET_TX_END,        // a = same
  SIG_VOICE_PHASE,       // a = new phase (0 idle, 1 listen, 2 think, 3 speak)
  // TouchAO
  SIG_TOUCH_CONTACT,     // a = peaks F | M<<8 | B<<16 | ignored<<24, b = length ms
  SIG_TOUCH_TAP,         // a = zone (0 front, 2 back)
  SIG_TOUCH_HOLD,
  SIG_TOUCH_STORM,       // a = 1 burst, 2 latch
  SIG_TOUCH_REF,         // chip took a fresh reference; a = why (AoRefWhy), b = 1 ok
  SIG_TOUCH_NEED_STILL,  // a = ms: touch wants the head still to take a reference
  SIG_TOUCH_RESTART,     // ask TouchAO for a fresh reference (debug, rail)
  SIG_BOOT_READY,        // the loop has started
  // MotionAO (stage 2). Commands are posted to it; the rest it publishes.
  SIG_MOTION_MOVE,       // command: a = angle tenths, b = ms, flags = axis (bit 0) | class << 1
  SIG_MOTION_GESTURE,    // command: a = ScGesture
  SIG_MOTION_RECOVER,    // command: a = source (0 web, 1 watchdog), b = 1 home after
  SIG_MOTION_REST,       // command: a = ScRestFlags
  SIG_MOTION_WAKE,       // command
  SIG_MOTION_END,        // a = kind (ScMotionKind), b = 1 ok
  SIG_MOTION_DROPPED,    // a = kind refused, b = the state that refused it
  SIG_SERVO_DEAF,        // the watchdog gave up on the servos: recovery starts
  SIG_COUNT
};

static const char* const AO_SIG_NAMES[SIG_COUNT] = {
  "none", "tick",
  "motionStart", "railSwitch", "ledPunch", "ampOn", "ampOff", "micStart", "micStop",
  "cameraStart", "cameraEnd", "netTxStart", "netTxEnd", "voicePhase",
  "touchContact", "touchTap", "touchHold", "touchStorm", "touchRef", "touchNeedStill",
  "touchRestart", "bootReady",
  "motionMove", "motionGesture", "motionRecover", "motionRest", "motionWake",
  "motionEnd", "motionDropped", "servoDeaf",
};

// Who sent it (timeline column). Not an AO id: shims publish too.
enum AoSrc : uint8_t {
  SRC_NONE = 0, SRC_MOTION, SRC_POWER, SRC_LEDS, SRC_AUDIO, SRC_CAMERA, SRC_NET,
  SRC_VOICE, SRC_TOUCH, SRC_WEB, SRC_APP, SRC_COUNT
};
static const char* const AO_SRC_NAMES[SRC_COUNT] = {
  "-", "motion", "power", "leds", "audio", "camera", "net", "voice", "touch", "web", "app",
};

struct AoEvt {
  uint16_t sig;
  uint8_t  src;
  uint8_t  flags;
  int32_t  a;
  int32_t  b;
  uint32_t ms;
};
static_assert(sizeof(AoEvt) == 16, "AoEvt is passed by value in queues");

// ---- Active objects ------------------------------------------------------------
#define AO_MAX         8
#define AO_QUEUE_DEPTH 16

struct AoActive {
  const char*   name;
  void        (*onEvent)(const AoEvt&);
  uint16_t      tickMs;            // SIG_TICK period; 0 = every dispatch pass
  uint32_t      lastTickMs;
  QueueHandle_t q;
  StaticQueue_t qBuf;
  uint8_t       qStore[AO_QUEUE_DEPTH * sizeof(AoEvt)];
  uint32_t      handled, dropped, maxUs;
};

static AoActive* aoList[AO_MAX];
static uint8_t   aoCount = 0;
static uint32_t  aoSubs[SIG_COUNT];   // bit i = aoList[i] subscribes

// Register in priority order (first = dispatched first). Setup only.
inline uint8_t aoStart(AoActive& ao) {
  ao.q = xQueueCreateStatic(AO_QUEUE_DEPTH, sizeof(AoEvt), ao.qStore, &ao.qBuf);
  ao.lastTickMs = millis();
  aoList[aoCount] = &ao;
  return aoCount++;
}

inline void aoSubscribe(const AoActive& ao, AoSig sig) {
  for (uint8_t i = 0; i < aoCount; i++) if (aoList[i] == &ao) aoSubs[sig] |= 1u << i;
}

inline AoEvt aoEvt(AoSig sig, AoSrc src, int32_t a = 0, int32_t b = 0) {
  AoEvt e;
  e.sig = sig; e.src = src; e.flags = 0; e.a = a; e.b = b; e.ms = millis();
  return e;
}

// ---- System timeline -------------------------------------------------------------
// Every published event, in order, for /debug/timeline. PSRAM; none without it.
#define AO_TIMELINE_SIZE 512
struct AoTimeline {
  AoEvt    ring[AO_TIMELINE_SIZE];
  uint32_t count;                    // total ever; ring index = count % size
};
static AoTimeline* aoTimeline = nullptr;
static portMUX_TYPE aoMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t aoPublished = 0, aoDropped = 0;

inline void aoTimelineBegin() {
  if (aoTimeline) return;
  void* mem = memAlloc(MEM_DIAG, sizeof(AoTimeline), MEM_BIG_ONLY);
  if (mem) { aoTimeline = (AoTimeline*)mem; aoTimeline->count = 0; }
}

inline void aoTimelineAdd(const AoEvt& e) {
  if (!aoTimeline) return;
  portENTER_CRITICAL(&aoMux);
  aoTimeline->ring[aoTimeline->count % AO_TIMELINE_SIZE] = e;
  aoTimeline->count++;
  portEXIT_CRITICAL(&aoMux);
}

// ---- Posting -------------------------------------------------------------------------
// From any task. Never blocks: a full queue drops the event and counts it.
inline bool aoPost(AoActive& ao, const AoEvt& e) {
  if (!ao.q) return false;
  if (xQueueSend(ao.q, &e, 0) == pdTRUE) return true;
  ao.dropped++;
  aoDropped++;
  return false;
}

// To every subscriber, and onto the timeline.
inline void aoPublish(const AoEvt& e) {
  aoPublished++;
  aoTimelineAdd(e);
  if (e.sig >= SIG_COUNT) return;
  const uint32_t subs = aoSubs[e.sig];
  for (uint8_t i = 0; i < aoCount; i++) if (subs & (1u << i)) aoPost(*aoList[i], e);
}

inline void aoPublish(AoSig sig, AoSrc src, int32_t a = 0, int32_t b = 0) {
  aoPublish(aoEvt(sig, src, a, b));
}

// ---- Cooperative dispatch (loop thread only) ------------------------------------------
// Runs each AO's queued events to completion, then its time event when due.
inline void aoDispatch() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < aoCount; i++) {
    AoActive& ao = *aoList[i];
    AoEvt e;
    uint8_t budget = AO_QUEUE_DEPTH;   // one queue's worth per pass: no starving the others
    while (budget-- && xQueueReceive(ao.q, &e, 0) == pdTRUE) {
      const uint32_t t0 = micros();
      ao.onEvent(e);
      const uint32_t us = micros() - t0;
      if (us > ao.maxUs) ao.maxUs = us;
      ao.handled++;
    }
    if (now - ao.lastTickMs >= ao.tickMs) {
      ao.lastTickMs = now;
      const uint32_t t0 = micros();
      ao.onEvent(aoEvt(SIG_TICK, SRC_NONE));
      const uint32_t us = micros() - t0;
      if (us > ao.maxUs) ao.maxUs = us;
    }
  }
}

#endif // BOARD_HAS_STACKCHAN_BASE
#endif // AO_CORE_H
