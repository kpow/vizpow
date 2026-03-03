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

// Hi-res mode flag - renders effects at full LCD resolution instead of 8x8 simulation
bool hiResMode = false;
bool hiResRenderedThisFrame = false;  // Set by hi-res effects to skip 8x8 rendering

// Forward declaration (defined in settings.h, included later)
void markSettingsDirty();

// Toggle hi-res mode
inline void toggleHiResMode() {
  hiResMode = !hiResMode;
  // gfx is valid for all targets (Arduino_GFX* or DisplayProxy*)
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
// TARGET_CORES3: M5Unified display path
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

// Thin wrapper that exposes Arduino_GFX-compatible method names over M5.Display.
// All existing gfx->fillRect() / gfx->setCursor() etc. call sites work unchanged.
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
    if (_dp_canvas && _dp_canvas_active) _dp_canvas->pushSprite(0, 0);
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
  // On PSRAM-equipped boards (Core S3 = 8MB, TARGET_LCD = 2MB), 16-bit canvas
  // goes to PSRAM — internal heap stays free for TLS and tasks.
  displayProxyInstance.preallocateCanvas();
}

// ============================================================================
// Standard Arduino_GFX path (TARGET_LCD / TARGET_LED)
// ============================================================================
#else

#include <Arduino_GFX_Library.h>

Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;

// PSRAM-aware canvas factory for TARGET_LCD — logs where the buffer landed.
// Arduino_Canvas calls ps_malloc() internally when PSRAM is available, so we
// measure free PSRAM before/after to confirm placement.
Arduino_Canvas* createPsramAwareCanvas(int16_t w, int16_t h, Arduino_GFX* output) {
  // Pre-check: largest contiguous block must fit the 16-bit framebuffer + overhead
  size_t needed = (size_t)w * h * 2;
  size_t maxBlock = psramFound()
    ? heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)
    : heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  if (maxBlock < needed + 4096) {
    DBG("Canvas: SKIP — need ");
    DBG((needed + 4096) / 1024);
    DBG("KB, largest=");
    DBG(maxBlock / 1024);
    DBGLN("KB");
    return nullptr;
  }

  size_t psramBefore = psramFound() ? ESP.getFreePsram() : 0;
  Arduino_Canvas* c = new Arduino_Canvas(w, h, output);
  c->begin();
  if (psramFound()) {
    size_t used = psramBefore - ESP.getFreePsram();
    DBG("Canvas: "); DBG(w); DBG("x"); DBG(h);
    if (used > 1024) {
      DBG(" in PSRAM ("); DBG(used / 1024); DBGLN("KB)");
    } else {
      DBGLN(" in internal heap (PSRAM not used)");
    }
  } else {
    DBGLN("Canvas: internal heap (no PSRAM)");
  }
  return c;
}

// ============================================================================
// ManagedCanvas — framebuffer lifecycle separated from Canvas object
// ============================================================================
// The raw framebuffer (134KB) is allocated FIRST at boot as one contiguous
// block, then wrapped by a lightweight Canvas object. This lets us free and
// reclaim the framebuffer cleanly for cloud TLS every 3 minutes — no
// fragmentation because the buffer was one clean malloc, not interleaved
// with display-driver allocations (which is what Arduino_Canvas::begin() does).

// ============================================================================
// Static framebuffer — in BSS, NOT on the heap
// ============================================================================
// By keeping the 134KB framebuffer in BSS instead of heap, the heap has ~100KB
// more contiguous free space. TLS (32KB) fits without needing canvas release,
// so there's zero flicker during cloud sync.
static uint16_t _lcd_static_fb[LCD_WIDTH * LCD_HEIGHT] __attribute__((aligned(16)));

// ManagedCanvas wraps a pre-existing framebuffer without allocating its own.
// begin() is overridden to skip the internal aligned_alloc + display re-init.
class ManagedCanvas : public Arduino_Canvas {
public:
  ManagedCanvas(int16_t w, int16_t h, Arduino_G *output, uint16_t* buf)
    : Arduino_Canvas(w, h, output) {
    _framebuffer = buf;  // Use externally-owned buffer (protected member)
  }
  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    return _framebuffer != nullptr;  // Skip internal alloc + output re-init
  }
  ~ManagedCanvas() {
    _framebuffer = nullptr;  // Don't free — we don't own it
  }
};

// Pre-allocated canvas for TARGET_LCD (claimed early before WiFi/tasks fragment heap)
Arduino_Canvas* _lcd_prealloc_canvas = nullptr;

void initLCD() {
  bus = new Arduino_ESP32SPI(
    LCD_DC,
    LCD_CS,
    LCD_SCK,
    LCD_MOSI,
    GFX_NOT_DEFINED  // MISO not used
  );

  gfx = new Arduino_ST7789(
    bus,
    LCD_RST,
    0,        // Rotation (0 = portrait)
    true,     // IPS display
    LCD_WIDTH,
    LCD_HEIGHT,
    0,        // Column offset
    20        // Row offset (ST7789V2 needs offset for 280 height)
  );

  gfx->begin();
  gfx->fillScreen(COLOR_BLACK);

  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  DBGLN("LCD initialized (ST7789)");

  // Try PSRAM canvas first (134KB → PSRAM, frees internal heap for TLS)
  if (psramFound()) {
    _lcd_prealloc_canvas = createPsramAwareCanvas(LCD_WIDTH, LCD_HEIGHT, gfx);
  }

  // Fallback: static BSS framebuffer (no PSRAM or allocation failed)
  if (_lcd_prealloc_canvas == nullptr) {
    _lcd_prealloc_canvas = new ManagedCanvas(LCD_WIDTH, LCD_HEIGHT, gfx, _lcd_static_fb);
    _lcd_prealloc_canvas->begin();
    DBG("Canvas: static BSS framebuffer ");
    DBG(sizeof(_lcd_static_fb) / 1024);
    DBGLN("KB");
  }
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
    analogWrite(LCD_BL, b);
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
