#ifndef EFFECTS_AMBIENT_H
#define EFFECTS_AMBIENT_H

#include <FastLED.h>
#include "config.h"

// External references to globals defined in main sketch
extern CRGB leds[];
extern CRGBPalette16 currentPalette;

// ============ Standard 8x8 LED Effects ============

void ambientPlasma() {
  static uint16_t t = 0;
  t += 2;

  for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      uint8_t value = sin8(x * 32 + t) + sin8(y * 32 + t) + sin8((x + y) * 16 + t);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, value);
    }
  }
}

void ambientRainbow() {
  static uint8_t hue = 0;
  hue++;

  for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      leds[XY(x, y)] = ColorFromPalette(currentPalette, hue + (x * 8) + (y * 8));
    }
  }
}

void ambientFire() {
  static uint8_t heat[64];

  for (int i = 0; i < NUM_LEDS; i++) {
    heat[i] = qsub8(heat[i], random8(0, 20));
  }

  for (int x = 0; x < MATRIX_WIDTH; x++) {
    if (random8() < 180) {
      heat[XY(x, MATRIX_HEIGHT - 1)] = qadd8(heat[XY(x, MATRIX_HEIGHT - 1)], random8(150, 255));
    }
  }

  for (int y = 0; y < MATRIX_HEIGHT - 1; y++) {
    for (int x = 0; x < MATRIX_WIDTH; x++) {
      heat[XY(x, y)] = (heat[XY(x, y)] + heat[XY(x, y + 1)] + heat[XY(x, y + 1)]) / 3;
    }
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = ColorFromPalette(currentPalette, heat[i]);
  }
}

void ambientOcean() {
  static uint16_t t = 0;
  t += 3;

  for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      uint8_t n = inoise8(x * 50, y * 50, t);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, n);
    }
  }
}

void ambientSparkle() {
  fadeToBlackBy(leds, NUM_LEDS, 20);
  int pos = random16(NUM_LEDS);
  leds[pos] = ColorFromPalette(currentPalette, random8(), 255);
}


void ambientMatrix() {
  static uint8_t drops[MATRIX_WIDTH];
  static bool init = false;

  if (!init) {
    for (int i = 0; i < MATRIX_WIDTH; i++) drops[i] = random8(MATRIX_HEIGHT);
    init = true;
  }

  fadeToBlackBy(leds, NUM_LEDS, 40);

  for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
    drops[x] = (drops[x] + 1) % (MATRIX_HEIGHT + random8(3));
    if (drops[x] < MATRIX_HEIGHT) {
      leds[XY(x, drops[x])] = ColorFromPalette(currentPalette, 100, 255);
      if (drops[x] > 0) {
        leds[XY(x, drops[x] - 1)] = ColorFromPalette(currentPalette, 100, 150);
      }
    }
  }
}

void ambientLava() {
  static uint16_t t = 0;
  t += 3;

  for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      uint8_t n = inoise8(x * 60, y * 60, t);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, n);
    }
  }
}

void ambientAurora() {
  static uint16_t t = 0;
  t += 2;

  for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      uint8_t n = inoise8(x * 40, y * 30 + t, t / 2);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, n);
    }
  }
}

void ambientConfetti() {
  fadeToBlackBy(leds, NUM_LEDS, 10);
  int pos = random16(NUM_LEDS);
  leds[pos] += ColorFromPalette(currentPalette, random8(64) + millis() / 50, 255);
}

void ambientComet() {
  static uint8_t pos = 0;
  static uint8_t hue = 0;

  fadeToBlackBy(leds, NUM_LEDS, 40);

  EVERY_N_MILLISECONDS(50) {
    pos = (pos + 1) % NUM_LEDS;
    hue++;
  }

  leds[pos] = ColorFromPalette(currentPalette, hue);
}

void ambientGalaxy() {
  static uint16_t t = 0;
  t++;

  for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      float dx = x - 3.5;
      float dy = y - 3.5;
      float angle = atan2(dy, dx);
      float dist = sqrt(dx * dx + dy * dy);
      uint8_t hue = (angle * 40) + (dist * 20) + t;
      uint8_t val = 255 - dist * 20;
      leds[XY(x, y)] = ColorFromPalette(currentPalette, hue, val);
    }
  }
}

void ambientHeart() {
  static uint8_t t = 0;
  t++;

  const uint8_t heart[] = {
    0b01100110,
    0b11111111,
    0b11111111,
    0b11111111,
    0b01111110,
    0b00111100,
    0b00011000,
    0b00000000
  };

  uint8_t beat = sin8(t * 4);
  uint8_t bright = 80 + (beat >> 1);
  if ((t % 60) < 5 || ((t % 60) > 10 && (t % 60) < 15)) {
    bright = 255;
  }

  FastLED.clear();
  for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      if (heart[y] & (1 << (7 - x))) {
        leds[XY(x, y)] = ColorFromPalette(currentPalette, t, bright);
      }
    }
  }
}

void ambientDonut() {
  static uint8_t t = 0;
  t++;

  const uint8_t donut[] = {
    0b00000000,
    0b00111100,
    0b01000010,
    0b01000010,
    0b01000010,
    0b01000010,
    0b00111100,
    0b00000000
  };

  bool phase = (t / 50) % 2;
  CRGB bgColor = phase ? CRGB::Blue : CRGB::Red;
  CRGB donutColor = phase ? CRGB::Red : CRGB::Blue;

  for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      if (donut[y] & (1 << (7 - x))) {
        leds[XY(x, y)] = donutColor;
      } else {
        leds[XY(x, y)] = bgColor;
      }
    }
  }
}

// Run ambient effect by index
// 13 effects: Plasma, Rainbow, Fire, Ocean, Sparkle, Matrix, Lava, Aurora, Confetti, Comet, Galaxy, Heart, Donut
void runAmbientEffect(uint8_t index) {
  switch (index) {
    case 0: ambientPlasma(); break;
    case 1: ambientRainbow(); break;
    case 2: ambientFire(); break;
    case 3: ambientOcean(); break;
    case 4: ambientSparkle(); break;
    case 5: ambientMatrix(); break;
    case 6: ambientLava(); break;
    case 7: ambientAurora(); break;
    case 8: ambientConfetti(); break;
    case 9: ambientComet(); break;
    case 10: ambientGalaxy(); break;
    case 11: ambientHeart(); break;
    case 12: ambientDonut(); break;
  }
}

#endif
