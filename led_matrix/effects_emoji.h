#ifndef EFFECTS_EMOJI_H
#define EFFECTS_EMOJI_H

#include <FastLED.h>
#include "config.h"

// External references to globals defined in main sketch
extern CRGB leds[];

// Emoji frame structure - stores 64 RGB pixels
struct EmojiFrame {
  CRGB pixels[64];
  bool active;
};

// Emoji queue storage
EmojiFrame emojiQueue[MAX_EMOJI_QUEUE];
uint8_t emojiQueueCount = 0;
uint8_t currentEmojiIndex = 0;

// Emoji settings
uint16_t emojiCycleTime = 3000;   // Time to display each emoji (ms)
uint16_t emojiFadeDuration = 500;  // Fade transition duration (ms)
bool emojiAutoCycle = true;

// Animation state
unsigned long emojiLastChange = 0;
bool emojiFading = false;

// Parse hex string to emoji frame
// Input: 192-character hex string (64 pixels * 3 bytes * 2 hex chars)
// Format: RRGGBBRRGGBB... for each pixel
bool parseHexToEmoji(const char* hexData, EmojiFrame* frame) {
  if (strlen(hexData) < 384) {  // 64 * 3 * 2 = 384 hex chars
    return false;
  }

  for (int i = 0; i < 64; i++) {
    int offset = i * 6;  // 6 hex chars per pixel (RRGGBB)

    // Parse R component
    char rHex[3] = {hexData[offset], hexData[offset + 1], 0};
    // Parse G component
    char gHex[3] = {hexData[offset + 2], hexData[offset + 3], 0};
    // Parse B component
    char bHex[3] = {hexData[offset + 4], hexData[offset + 5], 0};

    frame->pixels[i].r = strtol(rHex, NULL, 16);
    frame->pixels[i].g = strtol(gHex, NULL, 16);
    frame->pixels[i].b = strtol(bHex, NULL, 16);
  }

  frame->active = true;
  return true;
}

// Clear emoji queue
void clearEmojiQueue() {
  for (int i = 0; i < MAX_EMOJI_QUEUE; i++) {
    emojiQueue[i].active = false;
  }
  emojiQueueCount = 0;
  currentEmojiIndex = 0;
  emojiFading = false;
  emojiLastChange = millis();
}

// Add emoji to queue
bool addEmojiToQueue(const char* hexData) {
  if (emojiQueueCount >= MAX_EMOJI_QUEUE) {
    return false;
  }

  if (parseHexToEmoji(hexData, &emojiQueue[emojiQueueCount])) {
    emojiQueueCount++;
    return true;
  }
  return false;
}

// Display single emoji using XY() mapping
// Mirror horizontally to correct for matrix orientation
void displayEmoji(EmojiFrame* frame) {
  for (uint8_t y = 0; y < HEIGHT; y++) {
    for (uint8_t x = 0; x < WIDTH; x++) {
      int srcIndex = y * WIDTH + (WIDTH - 1 - x);  // Mirror X axis
      leds[XY(x, y)] = frame->pixels[srcIndex];
    }
  }
}

// Blend between two emojis for fade transition
void blendEmojis(EmojiFrame* from, EmojiFrame* to, uint8_t blendAmount) {
  for (uint8_t y = 0; y < HEIGHT; y++) {
    for (uint8_t x = 0; x < WIDTH; x++) {
      int srcIndex = y * WIDTH + (WIDTH - 1 - x);  // Mirror X axis
      leds[XY(x, y)] = blend(from->pixels[srcIndex], to->pixels[srcIndex], blendAmount);
    }
  }
}

// Main emoji effect loop function
void runEmojiEffect() {
  // Handle empty queue - show a subtle indication
  if (emojiQueueCount == 0) {
    FastLED.clear();
    // Show a dim center pixel as "waiting" indicator
    leds[XY(3, 3)] = CRGB(5, 5, 10);
    leds[XY(4, 3)] = CRGB(5, 5, 10);
    leds[XY(3, 4)] = CRGB(5, 5, 10);
    leds[XY(4, 4)] = CRGB(5, 5, 10);
    return;
  }

  // Single emoji - just display it
  if (emojiQueueCount == 1 || !emojiAutoCycle) {
    displayEmoji(&emojiQueue[currentEmojiIndex]);
    return;
  }

  // Multiple emojis with auto-cycle
  unsigned long elapsed = millis() - emojiLastChange;

  if (!emojiFading) {
    // Display phase
    if (elapsed < emojiCycleTime) {
      displayEmoji(&emojiQueue[currentEmojiIndex]);
    } else {
      // Start fade phase
      emojiFading = true;
    }
  } else {
    // Fade phase
    unsigned long fadeElapsed = elapsed - emojiCycleTime;

    if (fadeElapsed < emojiFadeDuration) {
      // Calculate blend amount (0-255)
      uint8_t blendAmount = (fadeElapsed * 255) / emojiFadeDuration;

      // Get next emoji index
      uint8_t nextIndex = (currentEmojiIndex + 1) % emojiQueueCount;

      // Blend between current and next
      blendEmojis(&emojiQueue[currentEmojiIndex], &emojiQueue[nextIndex], blendAmount);
    } else {
      // Fade complete - move to next emoji
      currentEmojiIndex = (currentEmojiIndex + 1) % emojiQueueCount;
      emojiLastChange = millis();
      emojiFading = false;
    }
  }
}

// Set emoji settings
void setEmojiSettings(uint16_t cycleTime, uint16_t fadeDuration, bool autoCycle) {
  emojiCycleTime = constrain(cycleTime, 500, 30000);
  emojiFadeDuration = constrain(fadeDuration, 100, 5000);
  emojiAutoCycle = autoCycle;
}

#endif
