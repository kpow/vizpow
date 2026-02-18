#ifndef WEATHER_ICONS_H
#define WEATHER_ICONS_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// ============================================================================
// Procedural Weather Icons — drawn with TFT primitives
// ============================================================================
// No bitmaps — just circles, lines, and triangles.
// Each icon is drawn centered at (cx, cy) with a given size.
// ============================================================================

extern Arduino_GFX *gfx;

// Colors (RGB565)
#define ICON_SUN_YELLOW   0xFFE0  // Bright yellow
#define ICON_SUN_ORANGE   0xFBE0  // Warm orange
#define ICON_CLOUD_LIGHT  0xC618  // Light gray
#define ICON_CLOUD_DARK   0x8410  // Darker gray
#define ICON_RAIN_BLUE    0x03BF  // Rain blue
#define ICON_SNOW_WHITE   0xFFFF  // White
#define ICON_BOLT_YELLOW  0xFFE0  // Lightning yellow
#define ICON_FOG_GRAY     0x7BEF  // Medium gray

// ============================================================================
// Individual icon drawing functions
// ============================================================================

// Sun: filled circle + 8 radiating lines
void drawSunIcon(int16_t cx, int16_t cy, int16_t size) {
  int16_t r = size / 3;
  int16_t rayLen = size / 2;

  // Rays
  for (int i = 0; i < 8; i++) {
    float angle = i * PI / 4.0f;
    int16_t x0 = cx + (int16_t)(cosf(angle) * (r + 2));
    int16_t y0 = cy + (int16_t)(sinf(angle) * (r + 2));
    int16_t x1 = cx + (int16_t)(cosf(angle) * rayLen);
    int16_t y1 = cy + (int16_t)(sinf(angle) * rayLen);
    gfx->drawLine(x0, y0, x1, y1, ICON_SUN_YELLOW);
  }

  // Center circle
  gfx->fillCircle(cx, cy, r, ICON_SUN_YELLOW);
}

// Cloud: 3 overlapping filled circles
void drawCloudIcon(int16_t cx, int16_t cy, int16_t size, uint16_t color) {
  int16_t r1 = size / 3;      // Main body
  int16_t r2 = size / 4;      // Side bumps

  gfx->fillCircle(cx - r1 / 2, cy, r2, color);       // Left bump
  gfx->fillCircle(cx, cy - r2 / 2, r1, color);       // Top center (bigger)
  gfx->fillCircle(cx + r1 / 2, cy, r2, color);       // Right bump
  // Fill bottom gap
  gfx->fillRect(cx - r1, cy, r1 * 2, r2, color);
}

// Partly cloudy: sun peeking out from behind a cloud
void drawPartlyCloudyIcon(int16_t cx, int16_t cy, int16_t size) {
  // Sun offset to upper-left
  drawSunIcon(cx - size / 5, cy - size / 5, size * 2 / 3);
  // Cloud overlapping lower-right
  drawCloudIcon(cx + size / 6, cy + size / 6, size * 2 / 3, ICON_CLOUD_LIGHT);
}

// Overcast: just a big cloud
void drawOvercastIcon(int16_t cx, int16_t cy, int16_t size) {
  drawCloudIcon(cx, cy, size, ICON_CLOUD_LIGHT);
}

// Rain: cloud + diagonal rain lines
void drawRainIcon(int16_t cx, int16_t cy, int16_t size) {
  // Cloud in upper portion
  drawCloudIcon(cx, cy - size / 5, size * 2 / 3, ICON_CLOUD_DARK);

  // 3 rain drops (diagonal lines below cloud)
  int16_t rainY = cy + size / 4;
  int16_t spacing = size / 4;
  for (int i = -1; i <= 1; i++) {
    int16_t rx = cx + i * spacing;
    gfx->drawLine(rx, rainY, rx - 2, rainY + size / 4, ICON_RAIN_BLUE);
    gfx->drawLine(rx + 1, rainY, rx - 1, rainY + size / 4, ICON_RAIN_BLUE);
  }
}

// Drizzle: cloud + small dots
void drawDrizzleIcon(int16_t cx, int16_t cy, int16_t size) {
  drawCloudIcon(cx, cy - size / 5, size * 2 / 3, ICON_CLOUD_LIGHT);

  // Small dots below cloud
  int16_t rainY = cy + size / 4;
  int16_t spacing = size / 4;
  for (int i = -1; i <= 1; i++) {
    gfx->fillCircle(cx + i * spacing, rainY, 1, ICON_RAIN_BLUE);
    gfx->fillCircle(cx + i * spacing - 2, rainY + size / 6, 1, ICON_RAIN_BLUE);
  }
}

// Snow: cloud + snowflake dots
void drawSnowIcon(int16_t cx, int16_t cy, int16_t size) {
  drawCloudIcon(cx, cy - size / 5, size * 2 / 3, ICON_CLOUD_LIGHT);

  // Snowflakes (small stars/asterisks)
  int16_t snowY = cy + size / 4;
  int16_t spacing = size / 4;
  for (int i = -1; i <= 1; i++) {
    int16_t sx = cx + i * spacing;
    int16_t sy = snowY + (i == 0 ? size / 6 : 0);  // Stagger
    // Small cross pattern
    gfx->fillCircle(sx, sy, 2, ICON_SNOW_WHITE);
    gfx->drawLine(sx - 2, sy, sx + 2, sy, ICON_SNOW_WHITE);
    gfx->drawLine(sx, sy - 2, sx, sy + 2, ICON_SNOW_WHITE);
  }
}

// Thunder: cloud + lightning bolt
void drawThunderIcon(int16_t cx, int16_t cy, int16_t size) {
  drawCloudIcon(cx, cy - size / 4, size * 2 / 3, ICON_CLOUD_DARK);

  // Lightning bolt (zigzag)
  int16_t boltX = cx;
  int16_t boltY = cy + size / 8;
  int16_t step = size / 5;

  gfx->drawLine(boltX + 2, boltY, boltX - 3, boltY + step, ICON_BOLT_YELLOW);
  gfx->drawLine(boltX - 3, boltY + step, boltX + 3, boltY + step, ICON_BOLT_YELLOW);
  gfx->drawLine(boltX + 3, boltY + step, boltX - 2, boltY + step * 2, ICON_BOLT_YELLOW);

  // Double up for thickness
  gfx->drawLine(boltX + 3, boltY, boltX - 2, boltY + step, ICON_BOLT_YELLOW);
  gfx->drawLine(boltX + 4, boltY + step, boltX - 1, boltY + step * 2, ICON_BOLT_YELLOW);
}

// Fog: horizontal dashed lines
void drawFogIcon(int16_t cx, int16_t cy, int16_t size) {
  int16_t lineW = size * 2 / 3;
  int16_t spacing = size / 5;

  for (int i = -1; i <= 1; i++) {
    int16_t ly = cy + i * spacing;
    int16_t lx = cx - lineW / 2 + (i == 0 ? 0 : (i * 3));  // Slight stagger
    // Dashed line
    for (int j = 0; j < lineW; j += 4) {
      gfx->drawLine(lx + j, ly, lx + j + 2, ly, ICON_FOG_GRAY);
    }
  }
}

// ============================================================================
// Main dispatcher
// ============================================================================

void drawWeatherIcon(int16_t cx, int16_t cy, int16_t size, uint8_t iconType) {
  switch (iconType) {
    case WEATHER_ICON_CLEAR:         drawSunIcon(cx, cy, size); break;
    case WEATHER_ICON_PARTLY_CLOUDY: drawPartlyCloudyIcon(cx, cy, size); break;
    case WEATHER_ICON_CLOUDY:        drawOvercastIcon(cx, cy, size); break;
    case WEATHER_ICON_FOG:           drawFogIcon(cx, cy, size); break;
    case WEATHER_ICON_DRIZZLE:       drawDrizzleIcon(cx, cy, size); break;
    case WEATHER_ICON_RAIN:          drawRainIcon(cx, cy, size); break;
    case WEATHER_ICON_SNOW:          drawSnowIcon(cx, cy, size); break;
    case WEATHER_ICON_THUNDER:       drawThunderIcon(cx, cy, size); break;
    default:                         drawPartlyCloudyIcon(cx, cy, size); break;
  }
}

#endif // WEATHER_ICONS_H
