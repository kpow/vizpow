#ifndef EFFECTS_EMOJI_H
#define EFFECTS_EMOJI_H

#include <FastLED.h>
#include "config.h"
#include "emoji_sprites.h"

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
// Input: 384-character hex string (64 pixels * 3 bytes * 2 hex chars)
// Format: RRGGBBRRGGBB... for each pixel
bool parseHexToEmoji(const char* hexData, EmojiFrame* frame) {
  if (strlen(hexData) < 384) {
    return false;
  }

  for (int i = 0; i < 64; i++) {
    int offset = i * 6;

    char rHex[3] = {hexData[offset], hexData[offset + 1], 0};
    char gHex[3] = {hexData[offset + 2], hexData[offset + 3], 0};
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

// Add emoji to queue from hex data (legacy method)
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

// Add emoji to queue from sprite index
bool addEmojiByIndex(uint8_t spriteIndex) {
  if (emojiQueueCount >= MAX_EMOJI_QUEUE) {
    return false;
  }
  if (spriteIndex >= NUM_EMOJI_SPRITES) {
    return false;
  }

  // Get pointer to sprite data from PROGMEM and copy to queue
  const CRGB* spritePtr = (const CRGB*)pgm_read_ptr(&emojiSprites[spriteIndex]);
  for (int i = 0; i < 64; i++) {
    memcpy_P(&emojiQueue[emojiQueueCount].pixels[i], &spritePtr[i], sizeof(CRGB));
  }
  emojiQueue[emojiQueueCount].active = true;
  emojiQueueCount++;
  return true;
}

// Display single emoji using XY() mapping
void displayEmoji(EmojiFrame* frame) {
  for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      leds[XY(x, y)] = frame->pixels[y * MATRIX_WIDTH + x];
    }
  }
}

// Blend between two emojis for fade transition
void blendEmojis(EmojiFrame* from, EmojiFrame* to, uint8_t blendAmount) {
  for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      int srcIndex = y * MATRIX_WIDTH + x;
      leds[XY(x, y)] = blend(from->pixels[srcIndex], to->pixels[srcIndex], blendAmount);
    }
  }
}

// Main emoji effect loop function
void runEmojiEffect() {
  // Handle empty queue - show a subtle indication
  if (emojiQueueCount == 0) {
    FastLED.clear();
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
    if (elapsed < emojiCycleTime) {
      displayEmoji(&emojiQueue[currentEmojiIndex]);
    } else {
      emojiFading = true;
    }
  } else {
    unsigned long fadeElapsed = elapsed - emojiCycleTime;

    if (fadeElapsed < emojiFadeDuration) {
      uint8_t blendAmount = (fadeElapsed * 255) / emojiFadeDuration;
      uint8_t nextIndex = (currentEmojiIndex + 1) % emojiQueueCount;
      blendEmojis(&emojiQueue[currentEmojiIndex], &emojiQueue[nextIndex], blendAmount);
    } else {
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

// Add random emojis to queue
uint8_t addRandomEmojis(uint8_t count) {
  currentEmojiIndex = 0;
  emojiFading = false;
  emojiLastChange = millis();
  emojiAutoCycle = true;

  bool usedSprites[NUM_EMOJI_SPRITES] = {false};

  uint8_t added = 0;
  uint8_t attempts = 0;
  uint8_t maxAttempts = count * 3;

  while (added < count && attempts < maxAttempts && emojiQueueCount < MAX_EMOJI_QUEUE) {
    uint8_t randomIndex = random(NUM_EMOJI_SPRITES);

    if (!usedSprites[randomIndex]) {
      usedSprites[randomIndex] = true;
      if (addEmojiByIndex(randomIndex)) {
        added++;
      }
    }
    attempts++;
  }

  return added;
}

#endif
