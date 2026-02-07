#ifndef DISPLAY_LCD_H
#define DISPLAY_LCD_H

#include "config.h"

// Only compile LCD code if LCD display is enabled
#if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)

#include <Arduino_GFX_Library.h>

// LCD display constants
#define LCD_WIDTH 240
#define LCD_HEIGHT 280
#define PIXEL_SIZE 26
#define PIXEL_GAP 4
#define GRID_SIZE (PIXEL_SIZE * MATRIX_WIDTH + PIXEL_GAP * (MATRIX_WIDTH - 1))  // 224 pixels total
#define COLOR_BLACK 0x0000  // RGB565 black

// Calculate offsets to center the 8x8 grid on the display
#define GRID_OFFSET_X ((LCD_WIDTH - GRID_SIZE) / 2)   // (240-224)/2 = 8
#define GRID_OFFSET_Y ((LCD_HEIGHT - GRID_SIZE) / 2)  // (280-224)/2 = 28

// Arduino_GFX display objects
Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;

// Hi-res mode flag - renders effects at full LCD resolution instead of 8x8 simulation
bool hiResMode = false;
bool hiResRenderedThisFrame = false;  // Set by hi-res effects to skip 8x8 rendering

// Toggle hi-res mode
inline void toggleHiResMode() {
  hiResMode = !hiResMode;
  if (gfx != nullptr) {
    gfx->fillScreen(COLOR_BLACK);  // Clear screen when switching modes
  }
  Serial.print("Hi-Res Mode: ");
  Serial.println(hiResMode ? "ON" : "OFF");
}

// Check if hi-res mode is enabled
inline bool isHiResMode() {
  return hiResMode;
}

// Convert CRGB to RGB565 format for the LCD
inline uint16_t crgbToRgb565(CRGB color) {
  return ((color.r & 0xF8) << 8) | ((color.g & 0xFC) << 3) | (color.b >> 3);
}

// Initialize the ST7789 LCD display
void initLCD() {
  // Create SPI bus for the display
  bus = new Arduino_ESP32SPI(
    LCD_DC,   // DC pin
    LCD_CS,   // CS pin
    LCD_SCK,  // SCK pin
    LCD_MOSI, // MOSI pin
    GFX_NOT_DEFINED  // MISO not used
  );

  // Create display driver (ST7789 240x280)
  // Rotation 0 = portrait mode
  gfx = new Arduino_ST7789(
    bus,
    LCD_RST,  // RST pin
    0,        // Rotation (0 = portrait)
    true,     // IPS display
    LCD_WIDTH,
    LCD_HEIGHT,
    0,        // Column offset
    20        // Row offset (ST7789V2 may need offset for 280 height)
  );

  // Initialize display
  gfx->begin();
  gfx->fillScreen(COLOR_BLACK);

  // Turn on backlight
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  Serial.println("LCD initialized");
}

// External variable for menu visibility (defined in touch_control.h)
#if defined(TOUCH_ENABLED)
extern bool menuVisible;
#endif

// Render the leds[] buffer to the LCD display
// Each LED is drawn as a PIXEL_SIZE x PIXEL_SIZE square
void renderToLCD() {
  // Don't render LED display while touch menu is visible
  #if defined(TOUCH_ENABLED)
  if (menuVisible) return;
  #endif

  // Don't render 8x8 grid if a hi-res effect already rendered this frame
  if (hiResRenderedThisFrame) {
    hiResRenderedThisFrame = false;  // Reset for next frame
    return;
  }

  extern CRGB leds[];  // Reference the global leds array from vizpow.ino

  for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      // Get the LED color using the XY mapping
      uint16_t ledIndex = XY(x, y);
      CRGB color = leds[ledIndex];

      // Convert to RGB565
      uint16_t color565 = crgbToRgb565(color);

      // Calculate screen position for this "pixel"
      int16_t screenX = GRID_OFFSET_X + x * (PIXEL_SIZE + PIXEL_GAP);
      int16_t screenY = GRID_OFFSET_Y + y * (PIXEL_SIZE + PIXEL_GAP);

      // Draw filled rectangle
      gfx->fillRect(screenX, screenY, PIXEL_SIZE, PIXEL_SIZE, color565);
    }
  }
}

// Optional: Set LCD backlight brightness (0-255)
void setLCDBacklight(uint8_t brightness) {
  analogWrite(LCD_BL, brightness);
}

// Optional: Clear the LCD to black
void clearLCD() {
  gfx->fillScreen(COLOR_BLACK);
}

#else

// Stub functions when LCD is disabled (allows code to compile)
inline void initLCD() {}
inline void renderToLCD() {}
inline void setLCDBacklight(uint8_t brightness) {}
inline void clearLCD() {}

#endif // DISPLAY_LCD_ONLY || DISPLAY_DUAL

#endif // DISPLAY_LCD_H
