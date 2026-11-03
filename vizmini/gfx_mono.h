#ifndef GFX_MONO_H
#define GFX_MONO_H

#include <Arduino.h>
#include <U8g2lib.h>
#include "config.h"

// ============================================================================
// GfxDevice — 1-bit adapter for the reused vizbot face renderer
// ============================================================================
// bot_eyes.h / bot_eye_model.h draw the whole face through a few primitives on
// an object named `gfx` of type `GfxDevice*`. On the LCD firmware that type is
// LovyanGFX. Here a tiny GfxDevice maps the same calls onto a monochrome U8g2
// full-frame buffer. Coordinates are OLED pixels: the face is placed and
// scaled by renderMiniFace() (vizmini.ino), the way vizbot's renderBotFaceAt()
// shrinks it, so the eye model rasterises one span per real OLED column.
//
// Color mapping: the renderer uses RGB565. Anything that isn't the background
// (0x0000) lights a pixel (draw color 1); the background / pupil color (0x0000)
// clears to 0. On a cleared buffer that "cuts" black pupils into white eyes.
// ============================================================================

// ---- The physical display object (software or hardware I2C) ----------------
#if OLED_DRIVER_SH1106
  #if OLED_USE_SW_I2C
    U8G2_SH1106_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, OLED_SCL, OLED_SDA, U8X8_PIN_NONE);
  #else
    U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
  #endif
#else
  #if OLED_USE_SW_I2C
    U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, OLED_SCL, OLED_SDA, U8X8_PIN_NONE);
  #else
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
  #endif
#endif

class GfxDevice {
public:
  void begin() {
  #if !OLED_USE_SW_I2C
    u8g2.setBusClock(OLED_I2C_HZ);   // before begin(): U8g2 starts Wire on our pins at this clock
  #endif
    u8g2.begin();
    u8g2.setContrast(DEFAULT_CONTRAST);
    u8g2.setBitmapMode(1);
  }

  // ---- frame lifecycle ----
  void clear()   { u8g2.clearBuffer(); }
  void display() { u8g2.sendBuffer(); }
  void setContrast(uint8_t c) { u8g2.setContrast(c); }

  // ---- the primitives the face renderer calls ----
  void fillEllipse(int16_t cx, int16_t cy, int16_t rx, int16_t ry, uint16_t color) {
    int16_t srx = sr(rx), sry = sr(ry);
    if (srx < 1 || sry < 1) return;
    setColor(color);
    u8g2.drawFilledEllipse(sx(cx), sy(cy), srx, sry);
  }

  void fillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
    int16_t srr = sr(r);
    if (srr < 1) { u8g2.drawPixel(sx(cx), sy(cy)); return; }
    setColor(color);
    u8g2.drawDisc(sx(cx), sy(cy), srr);
  }

  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    setColor(color);
    u8g2.drawLine(sx(x0), sy(y0), sx(x1), sy(y1));
  }

  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    int16_t sw = sr(w), sh = sr(h);
    if (sw < 1 || sh < 1) return;
    setColor(color);
    u8g2.drawBox(sx(x), sy(y), sw, sh);
  }

  void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                    int16_t x2, int16_t y2, uint16_t color) {
    setColor(color);
    u8g2.drawTriangle(sx(x0), sy(y0), sx(x1), sy(y1), sx(x2), sy(y2));
  }

  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    int16_t sh = sr(h);
    if (sh < 1) return;
    setColor(color);
    u8g2.drawVLine(sx(x), sy(y), sh);
  }

private:
  static const uint16_t BG = 0x0000;  // background / pupil color
  inline void setColor(uint16_t c) { u8g2.setDrawColor(c == BG ? 0 : 1); }

  // Screen space already (see the header comment)
  static inline int16_t sx(int16_t x) { return x; }
  static inline int16_t sy(int16_t y) { return y; }
  static inline int16_t sr(int16_t r) { return r; }
};

#endif // GFX_MONO_H
