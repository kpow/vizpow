#ifndef WLED_EMOJI_H
#define WLED_EMOJI_H

// ============================================================================
// WLED Emoji Display — 3-sprite sequential animation on 32x8 matrix
// ============================================================================
// Shows 3 sprites at a time with 4px gaps between them.
// Sequential per-slot fade-in on entrance, per-slot crossfade on transition.
//
// Layout: [8px sprite] 4px gap [8px sprite] 4px gap [8px sprite]
//          slot 0                slot 1                slot 2
//          x=0..7               x=12..19              x=24..31
//
// Requires: emoji_sprites.h, wled_display.h functions (wledPixelClear,
//           wledSendDDP, wledCaptureState, wledHttpPost, wledData)
// Must be #included inside wled_display.h after those functions are defined.
// ============================================================================

#define WLED_EMOJI_SLOTS 3

enum WledEmojiPhase : uint8_t {
  WLED_EMOJI_ENTER = 0,
  WLED_EMOJI_HOLD,
  WLED_EMOJI_TRANSITION
};

// X position of each slot on the 32px-wide matrix
static const uint8_t slotXPos[WLED_EMOJI_SLOTS] = { 0, 12, 24 };

struct WledEmojiState {
  bool active;
  uint8_t queue[28];
  uint8_t queueCount;
  uint16_t cycleTimeMs;           // hold duration (default 4000)
  uint16_t fadeTimeMs;            // per-slot fade time: out+in (default 800)

  // Per-slot display
  int8_t slotSprites[WLED_EMOJI_SLOTS];   // sprite index per slot (-1 = empty)
  uint8_t slotAlpha[WLED_EMOJI_SLOTS];    // current alpha per slot

  // Animation state
  WledEmojiPhase phase;
  uint8_t currentSlot;            // which slot (0-2) is animating
  bool fadingIn;                  // true=fade-in, false=fade-out
  unsigned long fadeStartMs;
  unsigned long holdStartMs;
  unsigned long lastSendMs;

  // Queue cursor
  uint8_t groupStart;             // start index of current group in queue
};

static WledEmojiState wledEmoji = {
  false, {}, 0, 4000, 800,
  {-1, -1, -1}, {0, 0, 0},
  WLED_EMOJI_ENTER, 0, true, 0, 0, 0,
  0
};

// ============================================================================
// Queue management
// ============================================================================

void wledEmojiAdd(uint8_t spriteIndex) {
  if (spriteIndex >= ICON_COUNT) return;
  if (wledEmoji.queueCount >= 28) return;
  // Don't add duplicates
  for (uint8_t i = 0; i < wledEmoji.queueCount; i++) {
    if (wledEmoji.queue[i] == spriteIndex) return;
  }
  wledEmoji.queue[wledEmoji.queueCount++] = spriteIndex;
}

void wledEmojiRemove(uint8_t position) {
  if (position >= wledEmoji.queueCount) return;
  for (uint8_t i = position; i < wledEmoji.queueCount - 1; i++) {
    wledEmoji.queue[i] = wledEmoji.queue[i + 1];
  }
  wledEmoji.queueCount--;
  if (wledEmoji.groupStart >= wledEmoji.queueCount && wledEmoji.queueCount > 0) {
    wledEmoji.groupStart = 0;
  }
  if (wledEmoji.queueCount == 0) {
    wledEmoji.active = false;
  }
}

void wledEmojiClear() {
  wledEmoji.queueCount = 0;
  wledEmoji.groupStart = 0;
}

// ============================================================================
// Rendering — per-slot, decode from PROGMEM directly into pixelBuffer
// ============================================================================

// Render one sprite at a slot position, scaled by alpha, ADDed to pixelBuffer.
static void wledEmojiRenderSpriteScaled(uint8_t slot, uint8_t spriteIdx, uint8_t alpha) {
  if (slot >= WLED_EMOJI_SLOTS || spriteIdx >= ICON_COUNT || alpha == 0) return;

  const uint8_t* sprite = (const uint8_t*)pgm_read_ptr(&ALL_ICONS[spriteIdx]);
  uint8_t baseX = slotXPos[slot];

  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t x = 0; x < 8; x++) {
      uint8_t px = baseX + x;
      if (px >= WLED_DISPLAY_WIDTH) continue;

      uint8_t palIdx = pgm_read_byte(&sprite[y * 8 + x]);
      if (palIdx >= ICON_PALETTE_SIZE) continue;  // black — skip

      CRGB color;
      memcpy_P(&color, &iconPalette[palIdx], sizeof(CRGB));

      uint16_t offset = (y * WLED_DISPLAY_WIDTH + px) * 3;
      if (alpha == 255) {
        wledData.pixelBuffer[offset]     = color.r;
        wledData.pixelBuffer[offset + 1] = color.g;
        wledData.pixelBuffer[offset + 2] = color.b;
      } else {
        wledData.pixelBuffer[offset]     = qadd8(wledData.pixelBuffer[offset],     scale8(color.r, alpha));
        wledData.pixelBuffer[offset + 1] = qadd8(wledData.pixelBuffer[offset + 1], scale8(color.g, alpha));
        wledData.pixelBuffer[offset + 2] = qadd8(wledData.pixelBuffer[offset + 2], scale8(color.b, alpha));
      }
    }
  }
}

// Clear buffer and render all 3 slots at their current alpha.
static void wledEmojiRenderFrame() {
  wledPixelClear();
  for (uint8_t s = 0; s < WLED_EMOJI_SLOTS; s++) {
    if (wledEmoji.slotSprites[s] < 0 || wledEmoji.slotAlpha[s] == 0) continue;
    wledEmojiRenderSpriteScaled(s, (uint8_t)wledEmoji.slotSprites[s], wledEmoji.slotAlpha[s]);
  }
}

// ============================================================================
// Group helpers
// ============================================================================

// How many sprites in the current group (up to 3).
static uint8_t wledEmojiGroupCount() {
  uint8_t remaining = wledEmoji.queueCount - wledEmoji.groupStart;
  return (remaining > WLED_EMOJI_SLOTS) ? WLED_EMOJI_SLOTS : remaining;
}

// Get sprite index at position within current group.
static int8_t wledEmojiGroupSprite(uint8_t slot) {
  if (slot >= wledEmojiGroupCount()) return -1;
  return wledEmoji.queue[wledEmoji.groupStart + slot];
}

// Advance groupStart to the next group (wraps).
static void wledEmojiAdvanceGroup() {
  wledEmoji.groupStart += WLED_EMOJI_SLOTS;
  if (wledEmoji.groupStart >= wledEmoji.queueCount) {
    wledEmoji.groupStart = 0;
  }
}

// True if there are multiple groups worth cycling through.
static bool wledEmojiHasMultipleGroups() {
  return wledEmoji.queueCount > WLED_EMOJI_SLOTS;
}

// Half-fade duration: time for one fade direction (in or out).
static uint16_t wledEmojiHalfFade() {
  return wledEmoji.fadeTimeMs / 2;
}

// ============================================================================
// Start / Stop
// ============================================================================

void wledEmojiStart() {
  if (wledEmoji.queueCount == 0) return;

  wledEmoji.active = true;
  wledEmoji.groupStart = 0;

  // Assign first group to slots
  for (uint8_t s = 0; s < WLED_EMOJI_SLOTS; s++) {
    wledEmoji.slotSprites[s] = wledEmojiGroupSprite(s);
    wledEmoji.slotAlpha[s] = 0;
  }

  // Begin enter sequence
  wledEmoji.phase = WLED_EMOJI_ENTER;
  wledEmoji.currentSlot = 0;
  wledEmoji.fadingIn = true;
  wledEmoji.fadeStartMs = millis();

  // Capture WLED state for restore (if not already captured)
  if (!wledData.hasSavedState) {
    wledCaptureState();
  }

  // Render initial frame (all black since alpha=0)
  wledEmojiRenderFrame();
  wledSendDDP();
  wledEmoji.lastSendMs = millis();

  WLED_DBGLN("WLED emoji: started (3-slot sequential)");
}

void wledEmojiStop() {
  wledEmoji.active = false;

  // Clear slot state
  for (uint8_t s = 0; s < WLED_EMOJI_SLOTS; s++) {
    wledEmoji.slotSprites[s] = -1;
    wledEmoji.slotAlpha[s] = 0;
  }

  // Restore WLED's normal effect
  if (wledData.hasSavedState) {
    char body[160];
    snprintf(body, sizeof(body),
      "{\"on\":true,\"live\":false,\"transition\":0,\"seg\":{\"id\":%d,\"fx\":%d,\"sx\":%d,\"ix\":%d,\"pal\":%d}}",
      WLED_SEGMENT_ID,
      wledData.savedFx, wledData.savedSx,
      wledData.savedIx, wledData.savedPal);
    wledHttpPost(body);
  }

  WLED_DBGLN("WLED emoji: stopped, restored");
}

// ============================================================================
// Main update — called from pollWledDisplay() on Core 0
// ============================================================================

// Saved next-group sprites during TRANSITION phase (so we can load them
// before advancing groupStart).
static int8_t wledEmojiNextSprites[WLED_EMOJI_SLOTS];

void wledEmojiUpdate() {
  if (!wledEmoji.active) return;
  if (wledEmoji.queueCount == 0) {
    wledEmoji.active = false;
    return;
  }

  unsigned long now = millis();
  bool needSend = false;
  uint16_t halfFade = wledEmojiHalfFade();

  switch (wledEmoji.phase) {

  // ------------------------------------------------------------------
  // ENTER: fade in each slot sequentially
  // ------------------------------------------------------------------
  case WLED_EMOJI_ENTER: {
    uint8_t cs = wledEmoji.currentSlot;

    // Skip empty slots (queue has fewer than 3 sprites)
    if (wledEmoji.slotSprites[cs] < 0) {
      // Done with enter
      wledEmoji.phase = WLED_EMOJI_HOLD;
      wledEmoji.holdStartMs = now;
      break;
    }

    unsigned long elapsed = now - wledEmoji.fadeStartMs;

    if (elapsed >= halfFade) {
      // This slot is fully visible
      wledEmoji.slotAlpha[cs] = 255;

      // Move to next slot
      cs++;
      if (cs >= WLED_EMOJI_SLOTS || wledEmoji.slotSprites[cs] < 0) {
        // All slots entered — go to HOLD
        wledEmoji.phase = WLED_EMOJI_HOLD;
        wledEmoji.holdStartMs = now;
      } else {
        wledEmoji.currentSlot = cs;
        wledEmoji.fadeStartMs = now;
      }
      needSend = true;
    } else if (now - wledEmoji.lastSendMs >= 33) {
      // ~30fps animation
      wledEmoji.slotAlpha[cs] = (uint8_t)((elapsed * 255) / halfFade);
      needSend = true;
    }
    break;
  }

  // ------------------------------------------------------------------
  // HOLD: all slots fully visible, keepalive, wait for cycle
  // ------------------------------------------------------------------
  case WLED_EMOJI_HOLD: {
    // Keepalive — re-send every 2s to prevent WLED's 2.5s timeout
    if (now - wledEmoji.lastSendMs >= 2000) {
      needSend = true;
    }

    // Start transition if we have multiple groups and hold time elapsed
    if (wledEmojiHasMultipleGroups() &&
        (now - wledEmoji.holdStartMs >= wledEmoji.cycleTimeMs)) {
      // Snapshot the next group's sprites before we start fading
      uint8_t nextStart = wledEmoji.groupStart + WLED_EMOJI_SLOTS;
      if (nextStart >= wledEmoji.queueCount) nextStart = 0;
      for (uint8_t s = 0; s < WLED_EMOJI_SLOTS; s++) {
        uint8_t qi = nextStart + s;
        wledEmojiNextSprites[s] = (qi < wledEmoji.queueCount) ? wledEmoji.queue[qi] : -1;
      }

      wledEmoji.phase = WLED_EMOJI_TRANSITION;
      wledEmoji.currentSlot = 0;
      wledEmoji.fadingIn = false;  // start by fading out
      wledEmoji.fadeStartMs = now;
    }
    break;
  }

  // ------------------------------------------------------------------
  // TRANSITION: per-slot fade out old, fade in new, sequentially
  // ------------------------------------------------------------------
  case WLED_EMOJI_TRANSITION: {
    uint8_t cs = wledEmoji.currentSlot;
    unsigned long elapsed = now - wledEmoji.fadeStartMs;

    if (!wledEmoji.fadingIn) {
      // --- FADE OUT current sprite ---
      if (elapsed >= halfFade) {
        // Fade-out complete — swap to new sprite, start fade-in
        wledEmoji.slotAlpha[cs] = 0;
        wledEmoji.slotSprites[cs] = wledEmojiNextSprites[cs];
        wledEmoji.fadingIn = true;
        wledEmoji.fadeStartMs = now;
        needSend = true;
      } else if (now - wledEmoji.lastSendMs >= 33) {
        wledEmoji.slotAlpha[cs] = 255 - (uint8_t)((elapsed * 255) / halfFade);
        needSend = true;
      }
    } else {
      // --- FADE IN new sprite ---
      if (wledEmoji.slotSprites[cs] < 0) {
        // No new sprite for this slot — skip to next slot
        wledEmoji.slotAlpha[cs] = 0;
        elapsed = halfFade; // force advance
      }

      if (elapsed >= halfFade) {
        // Fade-in complete
        if (wledEmoji.slotSprites[cs] >= 0) {
          wledEmoji.slotAlpha[cs] = 255;
        }

        // Move to next slot
        cs++;
        if (cs >= WLED_EMOJI_SLOTS) {
          // All slots transitioned — advance group, go to HOLD
          wledEmojiAdvanceGroup();
          wledEmoji.phase = WLED_EMOJI_HOLD;
          wledEmoji.holdStartMs = now;
        } else {
          wledEmoji.currentSlot = cs;
          wledEmoji.fadingIn = false;  // next slot starts with fade-out
          wledEmoji.fadeStartMs = now;
        }
        needSend = true;
      } else if (now - wledEmoji.lastSendMs >= 33) {
        wledEmoji.slotAlpha[cs] = (uint8_t)((elapsed * 255) / halfFade);
        needSend = true;
      }
    }
    break;
  }
  } // switch

  if (needSend) {
    wledEmojiRenderFrame();
    wledSendDDP();
    wledEmoji.lastSendMs = now;
  }
}

// ============================================================================
// JSON state for /state endpoint
// ============================================================================

String getWledEmojiJson() {
  String json = "{\"active\":";
  json += wledEmoji.active ? "true" : "false";
  json += ",\"queue\":[";
  for (uint8_t i = 0; i < wledEmoji.queueCount; i++) {
    if (i > 0) json += ",";
    json += wledEmoji.queue[i];
  }
  json += "],\"cycleTime\":";
  json += wledEmoji.cycleTimeMs;
  json += ",\"fadeTime\":";
  json += wledEmoji.fadeTimeMs;
  json += "}";
  return json;
}

#endif // WLED_EMOJI_H
