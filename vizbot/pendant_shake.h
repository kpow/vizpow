#ifndef PENDANT_SHAKE_H
#define PENDANT_SHAKE_H

#include "config.h"
#ifdef HAS_LOW_POWER_MODE

#include <Arduino.h>
#include "bot_faces.h"
#include "fx_palettes.h"

// ============================================================================
// Pendant shake reactions — low power mode only
// ============================================================================
// A sustained shake normally toggles info mode (the weather page). In low power
// mode the radio never starts, so there is no weather to show: the gesture used
// to land you on an empty page. And a 1.69 worn as a pendant shakes all day on
// its own, so it landed there over and over.
//
// With the radio off the shake earns a reaction instead, picked at random:
//
//   dizzy meter   keep shaking and it winds up through three stages, then
//                 settles once you stop
//   fortune       a magic-8-ball answer in the speech bubble
//   shuffle       rerolls the palette and the LED ring mode
//
// Reactions run on a budget so the pendant does not put on a show every thirty
// seconds while you walk around: PENDANT_SHAKE_GAP_MS of quiet between them,
// PENDANT_SHAKE_BUDGET per PENDANT_SHAKE_WINDOW_MS, then a full
// PENDANT_SHAKE_COOLOFF_MS where shakes are ignored outright.
// ============================================================================

extern bool    lowPowerMode;        // vizbot.ino
extern uint8_t paletteIndex;        // vizbot.ino
extern uint8_t effectIndex;         // vizbot.ino
extern bool    autoCycle;           // vizbot.ino
extern CRGBPalette16 currentPalette;
extern CRGBPalette16 palettes[];
void markSettingsDirty();           // settings.h
uint8_t getBotBackgroundStyle();    // bot_mode.h

// ── The three reactions ──────────────────────────────────────────────────────
// Weights, not an even roll: dizzy is the cheap one and reads as a reaction,
// shuffle changes saved settings so it stays the rare one.
enum PendantReaction : uint8_t { PSR_DIZZY = 0, PSR_FORTUNE, PSR_SHUFFLE };
static const uint8_t PENDANT_REACTION_WEIGHTS[] = { 5, 3, 2 };
#define PENDANT_WEIGHT_TOTAL 10

// Dizzy meter stages — each one pulls a worse face, a louder word and a ring
// flash in that expression's mood colour. atMs is time since the shake started.
struct PendantMeterStage {
  uint16_t    atMs;
  uint8_t     expr;
  const char* say;
};
static const PendantMeterStage PENDANT_METER_STAGES[] = {
  {  600, EXPR_SURPRISED, "Whoa"     },
  { 1800, EXPR_DIZZY,     "Dizzy"    },
  { 3500, EXPR_GLITCHING, "SPINNING" },
};
#define PENDANT_METER_COUNT 3

// Fortunes live here, not in bot_sayings.h: that file is a pool of single words
// sized for the WLED grid, and these are phrases the speech bubble wraps.
static const char* const PENDANT_FORTUNES[] = {
  "Yes",            "No",              "Obviously",       "Absolutely not",
  "Ask again later","Signs point to no","Without a doubt", "Doubtful",
  "Probably",       "Not a chance",     "The stars say go","Hard to say",
  "Trust it",       "Walk away",        "Soon",            "Already happened",
};
#define PENDANT_FORTUNE_COUNT 16

// Ring modes worth landing on. OFF, MOOD and AUDIO are left out: off is not a
// reaction, and mood/audio look identical to what the ring was already doing.
#ifdef HAS_BASE_LEDS
static const uint8_t PENDANT_SHUFFLE_RING[] = {
  SC_LED_MODE_RAINBOW, SC_LED_MODE_CHASE, SC_LED_MODE_FIRE,
  SC_LED_MODE_TWINKLE, SC_LED_MODE_PULSE, SC_LED_MODE_AURORA,
  SC_LED_MODE_SCREEN,
};
#define PENDANT_SHUFFLE_RING_COUNT 7
#endif

// ── State ────────────────────────────────────────────────────────────────────
struct PendantShakeState {
  // Budget
  uint8_t  fires;             // reactions inside the window that is open now
  uint32_t windowStartMs;     // 0 = no window open
  uint32_t lastFireMs;        // 0 = nothing has fired yet
  uint32_t quietUntilMs;      // 0 = not cooling off

  // Dizzy meter — the one reaction that follows the shaking as it happens
  bool     meterActive;
  uint32_t meterStartMs;
  uint32_t meterLastShakeMs;
  uint8_t  meterStage;        // stages applied so far, 0..PENDANT_METER_COUNT

  void init() {
    fires = 0; windowStartMs = 0; lastFireMs = 0; quietUntilMs = 0;
    meterActive = false; meterStartMs = 0; meterLastShakeMs = 0; meterStage = 0;
  }

  // Signed compares throughout so a millis() rollover does not strand the
  // pendant in a cool-off that never ends.
  bool budgetAllows(uint32_t now) const {
    if (quietUntilMs && (int32_t)(now - quietUntilMs) < 0) return false;
    if (lastFireMs   && (int32_t)(now - lastFireMs) < (int32_t)PENDANT_SHAKE_GAP_MS) return false;
    return true;
  }

  void chargeBudget(uint32_t now) {
    if (!windowStartMs || (int32_t)(now - windowStartMs) > (int32_t)PENDANT_SHAKE_WINDOW_MS) {
      windowStartMs = now;   // the old window aged out — start a fresh one
      fires = 0;
    }
    lastFireMs = now;
    if (++fires >= PENDANT_SHAKE_BUDGET) {
      quietUntilMs  = now + PENDANT_SHAKE_COOLOFF_MS;
      windowStartMs = 0;
      fires = 0;
      DBGLN("Pendant shake: budget spent, quiet for " +
            String(PENDANT_SHAKE_COOLOFF_MS / 1000) + "s");
    }
  }
};

static PendantShakeState pendantShake;

// ── Reactions ────────────────────────────────────────────────────────────────

// Hold the face put while a reaction runs. bot_mode.h's shakeReacting timer is
// what returns it to neutral, so every reaction pushes that timer out.
static inline void pendantHoldFace(uint32_t untilMs) {
  botMode.shakeReacting = true;
  botMode.shakeReactEnd = untilMs;
}

static void pendantApplyMeterStage(uint8_t stage, uint32_t now) {
  const PendantMeterStage& s = PENDANT_METER_STAGES[stage];
  botMode.face.transitionTo(s.expr, 150);
  botMode.speechBubble.show(s.say, 1800);
  #ifdef HAS_SOUND
  botSounds.play(SEQ_SHAKE_RATTLE);
  #endif
  #ifdef HAS_BASE_LEDS
  // Flash lasts until the next stage could land, so the ring never drops back
  // to its idle effect mid-wind-up.
  scFlashMood(s.expr, 1200);
  #endif
  // Far enough out that the face holds through the gap to the next stage.
  pendantHoldFace(now + 2500);
}

static void pendantStartDizzyMeter(uint32_t now) {
  pendantShake.meterActive      = true;
  pendantShake.meterStartMs     = now;
  pendantShake.meterLastShakeMs = now;
  pendantShake.meterStage       = 0;
  pendantApplyMeterStage(0, now);
  pendantShake.meterStage = 1;
}

static void pendantTellFortune(uint32_t now) {
  const char* f = PENDANT_FORTUNES[random(0, PENDANT_FORTUNE_COUNT)];
  botMode.face.transitionTo(EXPR_THINKING, 200);
  botMode.speechBubble.show(f, 3200);
  #ifdef HAS_SOUND
  botSounds.play(SEQ_MYSTERY);
  #endif
  #ifdef HAS_BASE_LEDS
  scFlashMood(EXPR_THINKING, 3000);
  #endif
  pendantHoldFace(now + 3400);
  DBGLN("Pendant shake: fortune \"" + String(f) + "\"");
}

static void pendantShuffleLook(uint32_t now) {
  paletteIndex   = random(0, NUM_PALETTES);
  currentPalette = palettes[paletteIndex];
  autoCycle      = false;   // a hand-rolled look should not get cycled away

  // The ambient effect only moves when the background is already ambient.
  // Anything else and the new effect would be invisible, so rerolling it would
  // be a silent settings change rather than a reaction.
  if (getBotBackgroundStyle() == 4) effectIndex = random(0, NUM_AMBIENT_EFFECTS);

  #ifdef HAS_BASE_LEDS
  scLeds.mode = PENDANT_SHUFFLE_RING[random(0, PENDANT_SHUFFLE_RING_COUNT)];
  #endif
  markSettingsDirty();

  botMode.face.transitionTo(EXPR_MISCHIEF, 150);
  botMode.speechBubble.show(FX_PALETTE_NAMES[paletteIndex % NUM_PALETTES], 2200);
  #ifdef HAS_SOUND
  botSounds.play(SEQ_SWIPE);
  #endif
  pendantHoldFace(now + 2400);
  DBGLN("Pendant shake: shuffled to palette " + String(paletteIndex));
}

// ── Entry points ─────────────────────────────────────────────────────────────

// Called where the sustained shake used to toggle info mode. Returns true if a
// reaction fired; false means the budget swallowed it, and the caller should
// stay quiet rather than fall through to the one-off dizzy.
inline bool pendantShakeSustained() {
  uint32_t now = millis();

  // Already winding up: more shaking feeds the meter, it does not re-roll.
  if (pendantShake.meterActive) {
    pendantShake.meterLastShakeMs = now;
    return true;
  }
  if (!pendantShake.budgetAllows(now)) return false;

  uint8_t roll = random(0, PENDANT_WEIGHT_TOTAL);
  uint8_t pick = PSR_DIZZY;
  for (uint8_t i = 0, acc = 0; i < 3; i++) {
    acc += PENDANT_REACTION_WEIGHTS[i];
    if (roll < acc) { pick = i; break; }
  }

  pendantShake.chargeBudget(now);
  botMode.registerInteraction();

  switch (pick) {
    case PSR_FORTUNE: pendantTellFortune(now);   break;
    case PSR_SHUFFLE: pendantShuffleLook(now);   break;
    default:          pendantStartDizzyMeter(now); break;
  }
  return true;
}

// Per-frame, from loop(). `shaking` is this frame's accel magnitude against
// SHAKE_SUSTAIN_THRESHOLD — the same test the sustained detector uses.
inline void pendantShakeUpdate(bool shaking) {
  if (!pendantShake.meterActive) return;
  uint32_t now = millis();
  if (shaking) pendantShake.meterLastShakeMs = now;

  // Still being shaken? Escalate when the next stage's time has passed.
  if (pendantShake.meterStage < PENDANT_METER_COUNT &&
      (int32_t)(now - pendantShake.meterStartMs) >=
          (int32_t)PENDANT_METER_STAGES[pendantShake.meterStage].atMs) {
    pendantApplyMeterStage(pendantShake.meterStage, now);
    pendantShake.meterStage++;
  }

  // At the top stage there is nothing left to escalate to, so keep re-arming
  // the hold — otherwise bot_mode.h eases the face back to neutral while the
  // pendant is still being shaken.
  if (pendantShake.meterStage >= PENDANT_METER_COUNT && shaking) {
    pendantHoldFace(now + 1500);
  }

  // Shaking stopped for long enough — settle back to neutral.
  if ((int32_t)(now - pendantShake.meterLastShakeMs) >= (int32_t)PENDANT_METER_RELEASE_MS) {
    pendantShake.meterActive = false;
    pendantHoldFace(now + 600);   // lets bot_mode.h ease it back
    DBGLN("Pendant shake: dizzy meter settled at stage " + String(pendantShake.meterStage));
  }
}

// True while a shake is being ignored — the pendant is just jostling.
inline bool pendantShakeCoolingOff() {
  return !pendantShake.budgetAllows(millis());
}

#endif // HAS_LOW_POWER_MODE
#endif // PENDANT_SHAKE_H
