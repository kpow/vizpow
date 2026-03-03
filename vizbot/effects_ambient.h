#ifndef EFFECTS_AMBIENT_H
#define EFFECTS_AMBIENT_H

#include <FastLED.h>
#include "config.h"
#include "layout.h"

// External references to globals defined in main sketch
extern CRGB leds[];
extern CRGBPalette16 currentPalette;

// Hi-res mode support for LCD display
#if defined(HIRES_ENABLED)
#ifndef TARGET_CORES3
#include <Arduino_GFX_Library.h>
#endif
extern Arduino_GFX *gfx;
extern bool hiResMode;
extern bool hiResRenderedThisFrame;
#if defined(TOUCH_ENABLED)
extern bool menuVisible;
#else
static bool menuVisible = false;
#endif

// Helper: Convert CRGB to RGB565
inline uint16_t toRGB565(CRGB color) {
  return ((color.r & 0xF8) << 8) | ((color.g & 0xFC) << 3) | (color.b >> 3);
}

// Grid dimensions derived from LCD size (compile-time constants)
#define HIRES_COLS (LCD_WIDTH / 8)
#define HIRES_ROWS (LCD_HEIGHT / 8)

// Shared buffer for hi-res effects (saves ~8KB RAM)
// Only one effect runs at a time, so they can share
static uint16_t hiResBuffer[HIRES_COLS][HIRES_ROWS];

// Hi-res Plasma - overlapping sine waves
void ambientPlasmaHiRes() {
  static uint16_t t = 0;
  t += 4;

  for (int16_t x = 0; x < LCD_WIDTH; x += 8) {
    for (int16_t y = 0; y < LCD_HEIGHT; y += 8) {
      uint8_t value = sin8(x + t) + sin8(y + t) + sin8((x + y) / 2 + t);
      CRGB color = ColorFromPalette(currentPalette, value);
      gfx->fillRect(x, y, 8, 8, toRGB565(color));
    }
  }
  hiResRenderedThisFrame = true;
}

// Hi-res Rainbow - smooth diagonal gradient
void ambientRainbowHiRes() {
  static uint8_t hue = 0;
  hue += 2;

  for (int16_t x = 0; x < LCD_WIDTH; x += 8) {
    for (int16_t y = 0; y < LCD_HEIGHT; y += 8) {
      uint8_t h = hue + (x / 4) + (y / 4);
      CRGB color = ColorFromPalette(currentPalette, h);
      gfx->fillRect(x, y, 8, 8, toRGB565(color));
    }
  }
  hiResRenderedThisFrame = true;
}

// Hi-res Fire - heat rises from bottom
void ambientFireHiRes() {
  static uint8_t heat[HIRES_COLS][HIRES_ROWS];  // keep separate

  // Cool down
  for (int x = 0; x < HIRES_COLS; x++) {
    for (int y = 0; y < HIRES_ROWS; y++) {
      heat[x][y] = qsub8(heat[x][y], random8(0, 12));
    }
  }

  // Spark at bottom
  for (int x = 0; x < HIRES_COLS; x++) {
    if (random8() < 180) {
      heat[x][HIRES_ROWS - 1] = qadd8(heat[x][HIRES_ROWS - 1], random8(160, 255));
    }
  }

  // Heat rises
  for (int y = 0; y < HIRES_ROWS - 1; y++) {
    for (int x = 0; x < HIRES_COLS; x++) {
      heat[x][y] = (heat[x][y] + heat[x][y + 1] + heat[x][y + 1]) / 3;
    }
  }

  // Render
  for (int16_t x = 0; x < LCD_WIDTH; x += 8) {
    for (int16_t y = 0; y < LCD_HEIGHT; y += 8) {
      CRGB color = ColorFromPalette(currentPalette, heat[x / 8][y / 8]);
      gfx->fillRect(x, y, 8, 8, toRGB565(color));
    }
  }
  hiResRenderedThisFrame = true;
}

// Hi-res Ocean - Perlin noise waves
void ambientOceanHiRes() {
  static uint16_t t = 0;
  t += 8;

  for (int16_t x = 0; x < LCD_WIDTH; x += 8) {
    for (int16_t y = 0; y < LCD_HEIGHT; y += 8) {
      uint8_t n = inoise8(x * 3, y * 3, t);
      CRGB color = ColorFromPalette(currentPalette, n);
      gfx->fillRect(x, y, 8, 8, toRGB565(color));
    }
  }
  hiResRenderedThisFrame = true;
}

// Hi-res Matrix - falling code rain
void ambientMatrixHiRes() {
  static uint8_t drops[HIRES_COLS];      // Drop Y positions
  static uint8_t speeds[HIRES_COLS];     // Drop speeds
  static bool init = false;

  if (!init) {
    for (int i = 0; i < HIRES_COLS; i++) {
      drops[i] = random8(HIRES_ROWS);
      speeds[i] = random8(1, 4);
    }
    init = true;
  }

  // Fade screen (uses shared hiResBuffer)
  for (int x = 0; x < HIRES_COLS; x++) {
    for (int y = 0; y < HIRES_ROWS; y++) {
      uint16_t c = hiResBuffer[x][y];
      uint8_t r = ((c >> 11) & 0x1F);
      uint8_t g = ((c >> 5) & 0x3F);
      uint8_t b = (c & 0x1F);
      if (r > 0) r--;
      if (g > 2) g -= 3;
      if (b > 0) b--;
      hiResBuffer[x][y] = (r << 11) | (g << 5) | b;
    }
  }

  // Update drops
  for (int x = 0; x < HIRES_COLS; x++) {
    drops[x] += speeds[x];
    if (drops[x] >= HIRES_ROWS + random8(10)) {
      drops[x] = 0;
      speeds[x] = random8(1, 4);
    }
    if (drops[x] < HIRES_ROWS) {
      CRGB color = ColorFromPalette(currentPalette, x * 8, 255);
      hiResBuffer[x][drops[x]] = toRGB565(color);
    }
  }

  // Render
  for (int16_t x = 0; x < LCD_WIDTH; x += 8) {
    for (int16_t y = 0; y < LCD_HEIGHT; y += 8) {
      gfx->fillRect(x, y, 8, 8, hiResBuffer[x / 8][y / 8]);
    }
  }
  hiResRenderedThisFrame = true;
}

// Hi-res Lava - slower, blobby noise
void ambientLavaHiRes() {
  static uint16_t t = 0;
  t += 5;

  for (int16_t x = 0; x < LCD_WIDTH; x += 8) {
    for (int16_t y = 0; y < LCD_HEIGHT; y += 8) {
      uint8_t n = inoise8(x * 4, y * 4, t);
      CRGB color = ColorFromPalette(currentPalette, n);
      gfx->fillRect(x, y, 8, 8, toRGB565(color));
    }
  }
  hiResRenderedThisFrame = true;
}

// Hi-res Aurora - horizontal flowing curtains
void ambientAuroraHiRes() {
  static uint16_t t = 0;
  t += 4;

  for (int16_t x = 0; x < LCD_WIDTH; x += 8) {
    for (int16_t y = 0; y < LCD_HEIGHT; y += 8) {
      uint8_t n = inoise8(x * 2, y * 2 + t, t / 2);
      CRGB color = ColorFromPalette(currentPalette, n);
      gfx->fillRect(x, y, 8, 8, toRGB565(color));
    }
  }
  hiResRenderedThisFrame = true;
}

// Hi-res Confetti - random colored pops
void ambientConfettiHiRes() {
  // Fade (uses shared hiResBuffer)
  for (int x = 0; x < HIRES_COLS; x++) {
    for (int y = 0; y < HIRES_ROWS; y++) {
      uint16_t c = hiResBuffer[x][y];
      uint8_t r = ((c >> 11) & 0x1F);
      uint8_t g = ((c >> 5) & 0x3F);
      uint8_t b = (c & 0x1F);
      if (r > 0) r--;
      if (g > 1) g -= 2;
      if (b > 0) b--;
      hiResBuffer[x][y] = (r << 11) | (g << 5) | b;
    }
  }

  // Add confetti
  for (int i = 0; i < 2; i++) {
    int x = random8(HIRES_COLS);
    int y = random8(HIRES_ROWS);
    CRGB color = ColorFromPalette(currentPalette, random8(64) + millis() / 50, 255);
    hiResBuffer[x][y] = toRGB565(color);
  }

  // Render
  for (int16_t x = 0; x < LCD_WIDTH; x += 8) {
    for (int16_t y = 0; y < LCD_HEIGHT; y += 8) {
      gfx->fillRect(x, y, 8, 8, hiResBuffer[x / 8][y / 8]);
    }
  }
  hiResRenderedThisFrame = true;
}

// Hi-res Galaxy - spinning spiral
void ambientGalaxyHiRes() {
  static uint16_t t = 0;
  t += 4;

  const float centerX = LCD_WIDTH / 2.0f;
  const float centerY = LCD_HEIGHT / 2.0f;
  const float maxDist = (LCD_WIDTH < LCD_HEIGHT ? LCD_WIDTH : LCD_HEIGHT) * 0.65f;

  for (int16_t x = 0; x < LCD_WIDTH; x += 8) {
    for (int16_t y = 0; y < LCD_HEIGHT; y += 8) {
      float dx = x - centerX;
      float dy = y - centerY;
      float angle = atan2(dy, dx);
      float dist = sqrt(dx * dx + dy * dy);
      uint8_t hue = (uint8_t)((angle * 40.0) + (dist * 0.5) + t);
      uint8_t val = (dist < maxDist) ? 255 - (dist * 1.5) : 0;
      CRGB color = ColorFromPalette(currentPalette, hue, val);
      gfx->fillRect(x, y, 8, 8, toRGB565(color));
    }
  }
  hiResRenderedThisFrame = true;
}

// Hi-res Heart - large pulsing heart
void ambientHeartHiRes() {
  static uint8_t t = 0;
  t++;

  // Heartbeat brightness
  uint8_t beat = sin8(t * 4);
  uint8_t bright = 80 + (beat >> 1);
  if ((t % 60) < 5 || ((t % 60) > 10 && (t % 60) < 15)) {
    bright = 255;
  }

  CRGB heartColor = ColorFromPalette(currentPalette, t, bright);
  uint16_t hc = toRGB565(heartColor);

  gfx->fillScreen(0x0000);  // Black background

  // Draw heart using parametric equation, scaled to LCD
  const float centerX = LCD_WIDTH / 2.0f;
  const float centerY = LCD_HEIGHT / 2.0f;
  const float scale = 7.0;

  for (float a = 0; a < 6.28; a += 0.02) {
    float hx = 16 * pow(sin(a), 3);
    float hy = -(13 * cos(a) - 5 * cos(2*a) - 2 * cos(3*a) - cos(4*a));
    int px = centerX + hx * scale;
    int py = centerY + hy * scale;
    // Fill from center to edge for solid heart
    for (int r = 0; r < scale * 16; r++) {
      int fx = centerX + (hx * scale * r) / (scale * 16);
      int fy = centerY + (hy * scale * r) / (scale * 16);
      if (fx >= 0 && fx < LCD_WIDTH && fy >= 0 && fy < LCD_HEIGHT) {
        gfx->fillRect(fx, fy, 4, 4, hc);
      }
    }
  }
  hiResRenderedThisFrame = true;
}

// Hi-res Donut — pixel-only effect, pick a random hi-res effect instead
void ambientDonutHiRes() {
  static uint8_t pick = 255;
  if (pick == 255) pick = random8(10);
  switch (pick) {
    case 0: ambientPlasmaHiRes(); break;
    case 1: ambientRainbowHiRes(); break;
    case 2: ambientFireHiRes(); break;
    case 3: ambientOceanHiRes(); break;
    case 4: ambientMatrixHiRes(); break;
    case 5: ambientLavaHiRes(); break;
    case 6: ambientAuroraHiRes(); break;
    case 7: ambientConfettiHiRes(); break;
    case 8: ambientGalaxyHiRes(); break;
    case 9: ambientHeartHiRes(); break;
  }
}

#endif // HIRES_ENABLED

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
      leds[XY(x, drops[x])] = ColorFromPalette(currentPalette, x * 32, 255);
      if (drops[x] > 0) {
        leds[XY(x, drops[x] - 1)] = ColorFromPalette(currentPalette, x * 32, 150);
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

// Function pointer tables for ambient effects
typedef void (*AmbientFunc)();

const AmbientFunc ambientLedFuncs[NUM_AMBIENT_EFFECTS] = {
  ambientPlasma, ambientRainbow, ambientFire, ambientOcean,
  ambientMatrix, ambientLava, ambientAurora, ambientConfetti,
  ambientGalaxy, ambientHeart, ambientDonut
};

#if defined(HIRES_ENABLED)
const AmbientFunc ambientHiResFuncs[NUM_AMBIENT_EFFECTS] = {
  ambientPlasmaHiRes, ambientRainbowHiRes, ambientFireHiRes, ambientOceanHiRes,
  ambientMatrixHiRes, ambientLavaHiRes, ambientAuroraHiRes,
  ambientConfettiHiRes, ambientGalaxyHiRes, ambientHeartHiRes,
  ambientDonutHiRes
};
#endif

// Run ambient effect by index
void runAmbientEffect(uint8_t index) {
  if (index >= NUM_AMBIENT_EFFECTS) return;
  #if defined(HIRES_ENABLED)
  if (hiResMode && !menuVisible && gfx != nullptr) {
    ambientHiResFuncs[index]();
    return;
  }
  #endif
  ambientLedFuncs[index]();
}

#endif
