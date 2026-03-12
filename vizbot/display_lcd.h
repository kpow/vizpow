#ifndef DISPLAY_LCD_H
#define DISPLAY_LCD_H

#include "config.h"

// Only compile LCD code if LCD display is enabled
#if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)

// LCD display constants (LCD_WIDTH / LCD_HEIGHT come from config.h per board)
#define PIXEL_SIZE 26
#define PIXEL_GAP 4
#define GRID_SIZE (PIXEL_SIZE * MATRIX_WIDTH + PIXEL_GAP * (MATRIX_WIDTH - 1))  // 224 pixels total
#define COLOR_BLACK 0x0000  // RGB565 black

// Calculate offsets to center the 8x8 grid on the display
#define GRID_OFFSET_X ((LCD_WIDTH - GRID_SIZE) / 2)
#define GRID_OFFSET_Y ((LCD_HEIGHT - GRID_SIZE) / 2)

// Hologram mode — horizontal mirror for Pepper's ghost prism display
bool hologramMirrorLCD = false;

// Hi-res mode flag - renders effects at full LCD resolution instead of 8x8 simulation
bool hiResMode = false;
bool hiResRenderedThisFrame = false;  // Set by hi-res effects to skip 8x8 rendering

// Forward declaration (defined in settings.h, included later)
void markSettingsDirty();

// Toggle hi-res mode
inline void toggleHiResMode() {
  hiResMode = !hiResMode;
  extern void clearLCD();
  clearLCD();
  DBG("Hi-Res Mode: ");
  DBGLN(hiResMode ? "ON" : "OFF");
  markSettingsDirty();
}

// Check if hi-res mode is enabled
inline bool isHiResMode() {
  return hiResMode;
}

// Convert CRGB to RGB565 format for the LCD
inline uint16_t crgbToRgb565(CRGB color) {
  return ((color.r & 0xF8) << 8) | ((color.g & 0xFC) << 3) | (color.b >> 3);
}

// ============================================================================
// TARGET_CORES3: M5Unified display path (LovyanGFX via M5Unified)
// ============================================================================
#ifdef TARGET_CORES3

#include <M5Unified.h>

// Canvas (sprite) for double-buffered bot mode rendering — eliminates flicker.
// Allocated lazily on first use of beginCanvas(); shared across frames.
static LGFX_Sprite* _dp_canvas = nullptr;
static bool _dp_canvas_active = false;
static bool _dp_canvas_failed = false;  // Once allocation fails, stop retrying

// Dispatch macro: calls method on canvas when active, otherwise on M5.Display.
// All color args are cast to uint16_t so M5GFX treats them as RGB565, not RGB888
// (M5GFX interprets uint32_t > 0xFFFF as RGB888; casting fixes cyan-instead-of-white).
#define DP(method, ...) (_dp_canvas_active ? _dp_canvas->method(__VA_ARGS__) : M5.Display.method(__VA_ARGS__))

// DisplayProxy wraps M5.Display + LGFX_Sprite for unified API.
// Both TARGET_LCD and TARGET_CORES3 use this same pattern:
// beginCanvas() activates double-buffering, flushCanvas() pushes to display.
struct DisplayProxy {
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)  { DP(fillRect, x, y, w, h, (uint16_t)color); }
  void fillScreen(uint32_t color)                                              { DP(fillScreen, (uint16_t)color); }
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)  { DP(drawRect, x, y, w, h, (uint16_t)color); }
  void drawPixel(int32_t x, int32_t y, uint32_t color)                        { DP(drawPixel, x, y, (uint16_t)color); }
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color)         { DP(drawFastHLine, x, y, w, (uint16_t)color); }
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color)         { DP(drawFastVLine, x, y, h, (uint16_t)color); }
  void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t color)            { DP(fillCircle, x, y, r, (uint16_t)color); }
  void setCursor(int32_t x, int32_t y)        { DP(setCursor, x, y); }
  void setTextColor(uint32_t fg)              { DP(setTextColor, (uint16_t)fg); }
  void setTextColor(uint32_t fg, uint32_t bg) { DP(setTextColor, (uint16_t)fg, (uint16_t)bg); }
  void setTextSize(float size)                { DP(setTextSize, size); }
  void print(const char* s)                   { DP(print, s); }
  void print(int v)                           { DP(print, v); }
  void print(unsigned int v)                  { DP(print, v); }
  void print(long v)                          { DP(print, v); }
  void print(unsigned long v)                 { DP(print, v); }
  void print(IPAddress ip)                    { DP(print, ip.toString().c_str()); }
  void println(const char* s)                 { DP(println, s); }
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) { DP(drawLine, x0, y0, x1, y1, (uint16_t)color); }
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) { DP(drawRoundRect, x, y, w, h, r, (uint16_t)color); }
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) { DP(fillRoundRect, x, y, w, h, r, (uint16_t)color); }
  void fillEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, uint32_t color) { DP(fillEllipse, x, y, rx, ry, (uint16_t)color); }
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color) { DP(fillTriangle, x0, y0, x1, y1, x2, y2, (uint16_t)color); }
  void setFont(const lgfx::IFont* font) { DP(setFont, font); }
  int16_t textWidth(const char* s) { return _dp_canvas_active ? (int16_t)_dp_canvas->textWidth(s) : (int16_t)M5.Display.textWidth(s); }
  void begin() {}  // no-op: M5.begin() handles display init
  int16_t width()  { return (int16_t)M5.Display.width(); }
  int16_t height() { return (int16_t)M5.Display.height(); }

  // Double-buffer support for bot mode flicker elimination.
  // beginCanvas() allocates the sprite once (lazy) and redirects all drawing to it.
  // flushCanvas() pushes the completed frame to the display atomically then restores.
  void beginCanvas() {
    if (_dp_canvas_failed) return;
    if (!_dp_canvas) {
      DBG("Canvas: free heap=");
      DBG(ESP.getFreeHeap());
      DBG(" max block=");
      DBGLN(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

      _dp_canvas = new LGFX_Sprite(&M5.Display);

      // PSRAM first — Core S3 has 8MB QSPI PSRAM, keeps internal heap free
      // for TLS and tasks.  M5Unified's display driver handles cache coherency.
      _dp_canvas->setColorDepth(16);
      _dp_canvas->setPsram(true);
      if (_dp_canvas->createSprite(LCD_WIDTH, LCD_HEIGHT)) {
        DBGLN("Canvas: 16-bit PSRAM");
        goto canvas_ok;
      }

      // 16-bit internal fallback
      DBGLN("Canvas: PSRAM failed, trying 16-bit internal");
      _dp_canvas->setColorDepth(16);
      _dp_canvas->setPsram(false);
      if (_dp_canvas->createSprite(LCD_WIDTH, LCD_HEIGHT)) {
        DBGLN("Canvas: 16-bit internal");
        goto canvas_ok;
      }

      // 8-bit internal fallback (half the RAM)
      DBGLN("Canvas: 16-bit internal failed, trying 8-bit");
      _dp_canvas->setColorDepth(8);
      _dp_canvas->setPsram(false);
      if (_dp_canvas->createSprite(LCD_WIDTH, LCD_HEIGHT)) goto canvas_ok;

      DBGLN("Canvas: allocation failed");

      // All attempts failed
      delete _dp_canvas;
      _dp_canvas = nullptr;
      _dp_canvas_failed = true;
      DBGLN("Canvas: ALL allocations failed — direct render (expect flicker)");
      return;

    canvas_ok:
      DBG("Canvas: OK ");
      DBG(_dp_canvas->getColorDepth());
      DBG("bpp ");
      DBG(_dp_canvas->bufferLength());
      DBGLN(" bytes");
    }
    _dp_canvas_active = (_dp_canvas != nullptr);
  }
  void flushCanvas() {
    if (_dp_canvas && _dp_canvas_active) {
      _dp_canvas->pushSprite(0, 0);
    }
    _dp_canvas_active = false;
  }
  // Pre-allocate the canvas early (before WiFi/tasks fragment the heap).
  // Called once from setup(). Does NOT activate — just reserves the memory.
  void preallocateCanvas() {
    beginCanvas();   // allocates sprite
    flushCanvas();   // deactivates (pushes empty frame, restores direct mode)
  }
} displayProxyInstance;

DisplayProxy *gfx = &displayProxyInstance;

void initLCD() {
  // M5.begin() is called before initLCD() in vizbot.ino setup().
  // It handles the AW9523 I2C expander, backlight, RST, and ILI9342C init.
  M5.Display.fillScreen(COLOR_BLACK);
  DBGLN("LCD initialized via M5Unified (Core S3, 320x240)");

  // Pre-allocate canvas early (before WiFi/tasks fragment the heap).
  // On PSRAM-equipped boards (Core S3 = 8MB), 16-bit canvas
  // goes to PSRAM — internal heap stays free for TLS and tasks.
  displayProxyInstance.preallocateCanvas();
}

// ============================================================================
// TARGET_LCD: LovyanGFX display path (ST7789V2 via custom LGFX class)
// ============================================================================
#else

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// Custom LGFX class for ESP32-S3-Touch-LCD-1.69 (ST7789V2, SPI with DMA)
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;

public:
  LGFX(void) {
    { // SPI bus configuration
      auto cfg = _bus_instance.config();
      cfg.spi_host    = SPI2_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;    // 40MHz SPI clock
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = true;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;  // DMA for async transfers
      cfg.pin_sclk    = LCD_SCK;     // GPIO 6
      cfg.pin_mosi    = LCD_MOSI;    // GPIO 7
      cfg.pin_miso    = -1;          // Not used
      cfg.pin_dc      = LCD_DC;      // GPIO 4
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { // Panel configuration
      auto cfg = _panel_instance.config();
      cfg.pin_cs      = LCD_CS;      // GPIO 5
      cfg.pin_rst     = LCD_RST;     // GPIO 8
      cfg.pin_busy    = -1;
      cfg.panel_width  = LCD_WIDTH;  // 240
      cfg.panel_height = LCD_HEIGHT; // 280
      cfg.offset_x    = 0;
      cfg.offset_y    = 20;          // ST7789V2 row offset for 280px height
      cfg.offset_rotation = 0;
      cfg.invert      = true;        // IPS panel needs inversion
      cfg.rgb_order   = false;
      cfg.bus_shared  = false;       // SPI bus not shared with other devices
      _panel_instance.config(cfg);
    }

    { // PWM backlight configuration
      auto cfg = _light_instance.config();
      cfg.pin_bl      = LCD_BL;      // GPIO 15
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    setPanel(&_panel_instance);
  }
};

// Hardware display instance
static LGFX _lcd_display;

// Canvas (sprite) for double-buffered rendering — same pattern as Core S3.
static LGFX_Sprite* _dp_canvas = nullptr;
static bool _dp_canvas_active = false;
static bool _dp_canvas_failed = false;

// Dispatch macro: calls method on canvas when active, otherwise on hardware display.
#define DP(method, ...) (_dp_canvas_active ? _dp_canvas->method(__VA_ARGS__) : _lcd_display.method(__VA_ARGS__))

// DisplayProxy wraps LGFX + LGFX_Sprite for unified API.
// Same pattern as TARGET_CORES3 — all code uses gfx->method() identically.
// All color args are cast to uint16_t so LovyanGFX treats them as RGB565, not RGB888
// (LovyanGFX interprets uint32_t > 0xFFFF as RGB888; casting fixes cyan-instead-of-white).
struct DisplayProxy {
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)  { DP(fillRect, x, y, w, h, (uint16_t)color); }
  void fillScreen(uint32_t color)                                              { DP(fillScreen, (uint16_t)color); }
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)  { DP(drawRect, x, y, w, h, (uint16_t)color); }
  void drawPixel(int32_t x, int32_t y, uint32_t color)                        { DP(drawPixel, x, y, (uint16_t)color); }
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color)         { DP(drawFastHLine, x, y, w, (uint16_t)color); }
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color)         { DP(drawFastVLine, x, y, h, (uint16_t)color); }
  void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t color)            { DP(fillCircle, x, y, r, (uint16_t)color); }
  void setCursor(int32_t x, int32_t y)        { DP(setCursor, x, y); }
  void setTextColor(uint32_t fg)              { DP(setTextColor, (uint16_t)fg); }
  void setTextColor(uint32_t fg, uint32_t bg) { DP(setTextColor, (uint16_t)fg, (uint16_t)bg); }
  void setTextSize(float size)                { DP(setTextSize, size); }
  void print(const char* s)                   { DP(print, s); }
  void print(int v)                           { DP(print, v); }
  void print(unsigned int v)                  { DP(print, v); }
  void print(long v)                          { DP(print, v); }
  void print(unsigned long v)                 { DP(print, v); }
  void print(IPAddress ip)                    { DP(print, ip.toString().c_str()); }
  void println(const char* s)                 { DP(println, s); }
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) { DP(drawLine, x0, y0, x1, y1, (uint16_t)color); }
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) { DP(drawRoundRect, x, y, w, h, r, (uint16_t)color); }
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) { DP(fillRoundRect, x, y, w, h, r, (uint16_t)color); }
  void fillEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, uint32_t color) { DP(fillEllipse, x, y, rx, ry, (uint16_t)color); }
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color) { DP(fillTriangle, x0, y0, x1, y1, x2, y2, (uint16_t)color); }
  void setFont(const lgfx::IFont* font) { DP(setFont, font); }
  int16_t textWidth(const char* s) { return _dp_canvas_active ? (int16_t)_dp_canvas->textWidth(s) : (int16_t)_lcd_display.textWidth(s); }
  void begin() {}  // no-op: initLCD() handles display init
  int16_t width()  { return (int16_t)_lcd_display.width(); }
  int16_t height() { return (int16_t)_lcd_display.height(); }

  // Double-buffer support — same API as Core S3 path.
  // beginCanvas() allocates the sprite once (lazy) and redirects all drawing to it.
  // flushCanvas() pushes the completed frame to the display atomically via DMA.
  void beginCanvas() {
    if (_dp_canvas_failed) return;
    if (!_dp_canvas) {
      DBG("Canvas: free heap=");
      DBG(ESP.getFreeHeap());
      DBG(" max block=");
      DBGLN(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

      _dp_canvas = new LGFX_Sprite(&_lcd_display);

      // Try 16-bit in PSRAM first (best quality, doesn't compete with internal heap)
      _dp_canvas->setColorDepth(16);
      _dp_canvas->setPsram(true);
      if (_dp_canvas->createSprite(LCD_WIDTH, LCD_HEIGHT)) {
        DBGLN("Canvas: 16-bit PSRAM");
        goto canvas_ok;
      }

      // 16-bit internal fallback for boards without PSRAM
      if (!psramFound()) {
        _dp_canvas->setPsram(false);
        if (_dp_canvas->createSprite(LCD_WIDTH, LCD_HEIGHT)) {
          DBGLN("Canvas: 16-bit internal (no PSRAM)");
          goto canvas_ok;
        }
      }

      // 8-bit internal fallback
      DBGLN("Canvas: 16-bit failed, trying 8-bit internal");
      _dp_canvas->setColorDepth(8);
      _dp_canvas->setPsram(false);
      if (_dp_canvas->createSprite(LCD_WIDTH, LCD_HEIGHT)) goto canvas_ok;
      DBGLN("Canvas: allocation failed");

      // All attempts failed
      delete _dp_canvas;
      _dp_canvas = nullptr;
      _dp_canvas_failed = true;
      DBGLN("Canvas: ALL allocations failed — direct render (expect flicker)");
      return;

    canvas_ok:
      DBG("Canvas: OK ");
      DBG(_dp_canvas->getColorDepth());
      DBG("bpp ");
      DBG(_dp_canvas->bufferLength());
      DBGLN(" bytes");
    }
    _dp_canvas_active = (_dp_canvas != nullptr);
  }
  void flushCanvas() {
    if (_dp_canvas && _dp_canvas_active) {
      if (hologramMirrorLCD) {
        // Horizontal flip in-place: swap pixels left↔right per row
        uint16_t w = _dp_canvas->width();
        uint16_t h = _dp_canvas->height();
        uint16_t half = w >> 1;
        if (_dp_canvas->getColorDepth() > 8) {
          uint16_t* buf = (uint16_t*)_dp_canvas->getBuffer();
          for (uint16_t y = 0; y < h; y++) {
            uint16_t* row = buf + y * w;
            for (uint16_t x = 0; x < half; x++) {
              uint16_t tmp = row[x];
              row[x] = row[w - 1 - x];
              row[w - 1 - x] = tmp;
            }
          }
        } else {
          uint8_t* buf = (uint8_t*)_dp_canvas->getBuffer();
          for (uint16_t y = 0; y < h; y++) {
            uint8_t* row = buf + y * w;
            for (uint16_t x = 0; x < half; x++) {
              uint8_t tmp = row[x];
              row[x] = row[w - 1 - x];
              row[w - 1 - x] = tmp;
            }
          }
        }
      }
      _dp_canvas->pushSprite(0, 0);
    }
    _dp_canvas_active = false;
  }
  // Pre-allocate the canvas early (before WiFi/tasks fragment the heap).
  void preallocateCanvas() {
    beginCanvas();   // allocates sprite
    flushCanvas();   // deactivates (pushes empty frame, restores direct mode)
  }
} displayProxyInstance;

DisplayProxy *gfx = &displayProxyInstance;

void initLCD() {
  _lcd_display.init();
  _lcd_display.fillScreen(COLOR_BLACK);

  DBGLN("LCD initialized (LovyanGFX ST7789V2, DMA SPI)");

  // Pre-allocate canvas early (before WiFi/tasks fragment the heap).
  // On PSRAM-equipped boards (8MB OPI), 16-bit canvas goes to PSRAM.
  displayProxyInstance.preallocateCanvas();
}

#endif // TARGET_CORES3

// ============================================================================
// Common LCD functions (work for both targets via gfx pointer)
// ============================================================================

// External variable for menu visibility (defined in touch_control.h)
#if defined(TOUCH_ENABLED)
extern bool menuVisible;
#endif

// External mode variable for bot mode check
extern uint8_t currentMode;

// Render the leds[] buffer to the LCD display as an 8x8 grid
void renderToLCD() {
  // Don't render LED display while touch menu is visible
  #if defined(TOUCH_ENABLED)
  if (menuVisible) return;
  #endif

  // Don't render 8x8 grid when Bot Mode is active (it renders directly)
  if (currentMode == MODE_BOT) return;

  // Don't render 8x8 grid if a hi-res effect already rendered this frame
  if (hiResRenderedThisFrame) {
    hiResRenderedThisFrame = false;
    return;
  }

  extern CRGB leds[];

  for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      uint16_t ledIndex = XY(x, y);
      CRGB color = leds[ledIndex];
      uint16_t color565 = crgbToRgb565(color);
      int16_t screenX = GRID_OFFSET_X + x * (PIXEL_SIZE + PIXEL_GAP);
      int16_t screenY = GRID_OFFSET_Y + y * (PIXEL_SIZE + PIXEL_GAP);
      gfx->fillRect(screenX, screenY, PIXEL_SIZE, PIXEL_SIZE, color565);
    }
  }
}

// Set LCD backlight brightness (0-255)
void setLCDBacklight(uint8_t b) {
  #ifdef TARGET_CORES3
    M5.Display.setBrightness(b);
  #else
    _lcd_display.setBrightness(b);
  #endif
}

// Clear the LCD to black
void clearLCD() {
  gfx->fillScreen(COLOR_BLACK);
}

#else

// Stub functions when LCD is disabled (allows code to compile for LED-only targets)
inline void initLCD() {}
inline void renderToLCD() {}
inline void setLCDBacklight(uint8_t brightness) {}
inline void clearLCD() {}

#endif // DISPLAY_LCD_ONLY || DISPLAY_DUAL

#endif // DISPLAY_LCD_H
