#ifndef TOUCH_CONTROL_H
#define TOUCH_CONTROL_H

#include <Wire.h>
#include <FastLED.h>
#include "config.h"

// Only compile touch code if enabled
#if defined(TOUCH_ENABLED)

#include <Arduino_GFX_Library.h>

// LCD dimensions (must match display_lcd.h)
#ifndef LCD_WIDTH
#define LCD_WIDTH 240
#define LCD_HEIGHT 280
#endif

// CST816T Touch Controller
#define TOUCH_I2C_ADDR 0x15
#define TOUCH_RST_PIN 21

// CST816T Register addresses
#define TOUCH_REG_GESTURE 0x01
#define TOUCH_REG_FINGER_NUM 0x02
#define TOUCH_REG_XPOS_H 0x03
#define TOUCH_REG_XPOS_L 0x04
#define TOUCH_REG_YPOS_H 0x05
#define TOUCH_REG_YPOS_L 0x06

// Touch timing
#define TOUCH_COOLDOWN_MS 400
#define MENU_TIMEOUT_MS 8000  // Hide menu after 8 seconds of no touch

// Full screen menu layout
#define BTN_WIDTH 110        // Button width (2 per row with gap)
#define BTN_HEIGHT 42        // Button height
#define BTN_GAP 8            // Gap between buttons
#define BTN_START_Y 66       // Y start for buttons (after header)

// External variables
extern uint8_t effectIndex;
extern uint8_t paletteIndex;
extern uint8_t brightness;
extern uint8_t currentMode;
extern bool autoCycle;
extern unsigned long lastChange;
extern CRGBPalette16 currentPalette;
extern CRGB leds[];
extern void resetEffectShuffle();
extern CRGBPalette16 palettes[];

// External GFX object from display_lcd.h
extern Arduino_GFX *gfx;

// Hi-res mode support
#if defined(HIRES_ENABLED)
extern bool hiResMode;
extern void toggleHiResMode();
#endif

// Touch state
static bool touchInitialized = false;
static uint8_t touchI2CAddr = TOUCH_I2C_ADDR;
static unsigned long lastTouchTime = 0;
static unsigned long lastActionTime = 0;
static bool waitingForLift = false;
static unsigned long touchStartTime = 0;
static bool longPressTriggered = false;
bool menuVisible = false;  // Not static - accessed from display_lcd.h
static uint8_t menuPage = 0;

// Long press timing
#define LONG_PRESS_MS 600

// LCD backlight brightness (0-255)
static uint8_t lcdBrightness = 200;

// Speed control (external from main sketch)
extern uint8_t speed;

// Bot background style control (defined in bot_mode.h)
extern void setBotBackgroundStyle(uint8_t style);
extern uint8_t getBotBackgroundStyle();

// Ambient effect names (for background overlay control)
const char* ambientEffectNames[] = {
  "Plasma", "Rainbow", "Fire", "Ocean", "Sparkle",
  "Matrix", "Lava", "Aurora", "Confetti", "Comet",
  "Galaxy", "Heart", "Donut"
};

// Palette names (must match order in palettes.h)
const char* paletteNames[] = {
  "Rainbow", "Ocean", "Lava", "Forest", "Party",
  "Heat", "Cloud", "Sunset", "Cyber", "Toxic",
  "Ice", "Blood", "Vaporwave", "DeepForest", "Gold"
};

// Reset touch controller
void resetTouch() {
  pinMode(TOUCH_RST_PIN, OUTPUT);
  digitalWrite(TOUCH_RST_PIN, LOW);
  delay(10);
  digitalWrite(TOUCH_RST_PIN, HIGH);
  delay(50);
}

// Read register from touch controller
uint8_t touchReadRegister(uint8_t reg) {
  Wire.beginTransmission(touchI2CAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return 0;
  }
  Wire.requestFrom(touchI2CAddr, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

// Initialize touch controller
bool initTouch() {
  resetTouch();
  delay(100);

  uint8_t addresses[] = {0x15, 0x5A, 0x38};
  for (int i = 0; i < 3; i++) {
    Wire.beginTransmission(addresses[i]);
    if (Wire.endTransmission() == 0) {
      touchI2CAddr = addresses[i];
      touchInitialized = true;
      DBG("Touch OK at 0x");
      DBGLN(touchI2CAddr, HEX);
      return true;
    }
  }

  uint8_t rstPins[] = {16, 9, 13};
  for (int p = 0; p < 3; p++) {
    pinMode(rstPins[p], OUTPUT);
    digitalWrite(rstPins[p], LOW);
    delay(10);
    digitalWrite(rstPins[p], HIGH);
    delay(100);

    for (int i = 0; i < 3; i++) {
      Wire.beginTransmission(addresses[i]);
      if (Wire.endTransmission() == 0) {
        touchI2CAddr = addresses[i];
        touchInitialized = true;
        DBG("Touch OK at 0x");
        DBG(touchI2CAddr, HEX);
        DBG(" (RST pin ");
        DBG(rstPins[p]);
        DBGLN(")");
        return true;
      }
    }
  }

  DBGLN("Touch not found");
  return false;
}

// Read touch coordinates
bool readTouch(uint16_t &x, uint16_t &y) {
  if (!touchInitialized) return false;

  uint8_t fingerNum = touchReadRegister(TOUCH_REG_FINGER_NUM);
  if (fingerNum == 0) return false;

  uint8_t xh = touchReadRegister(TOUCH_REG_XPOS_H);
  uint8_t xl = touchReadRegister(TOUCH_REG_XPOS_L);
  uint8_t yh = touchReadRegister(TOUCH_REG_YPOS_H);
  uint8_t yl = touchReadRegister(TOUCH_REG_YPOS_L);

  x = ((xh & 0x0F) << 8) | xl;
  y = ((yh & 0x0F) << 8) | yl;

  return true;
}

// Draw a solid button with centered text
void drawButton(int16_t x, int16_t y, int16_t w, int16_t h, const char* label, uint16_t bgColor, bool selected = false) {
  gfx->fillRect(x, y, w, h, bgColor);
  uint16_t borderColor = selected ? 0x07E0 : 0xFFFF;
  gfx->drawRect(x, y, w, h, borderColor);
  gfx->drawRect(x + 1, y + 1, w - 2, h - 2, borderColor);
  int16_t textW = strlen(label) * 12;
  int16_t textX = x + (w - textW) / 2;
  int16_t textY = y + (h - 16) / 2;
  gfx->setCursor(textX, textY);
  gfx->setTextColor(0xFFFF);
  gfx->setTextSize(2);
  gfx->print(label);
}

// Draw the header
void drawMenuHeader() {
  gfx->setTextSize(2);

  gfx->setCursor(10, 6);
  gfx->setTextColor(0x07FF);  // Cyan
  #if defined(HIRES_ENABLED)
  gfx->print(hiResMode ? "Bot HR" : "Bot PX");
  #else
  gfx->print("Bot");
  #endif

  // Current background effect
  gfx->setCursor(10, 24);
  gfx->setTextColor(0xFFFF);
  if (getBotBackgroundStyle() == 4) {
    gfx->print(ambientEffectNames[effectIndex % NUM_AMBIENT_EFFECTS]);
  } else {
    gfx->print("Black");
  }

  // Palette name
  gfx->setCursor(10, 42);
  gfx->setTextColor(0xF81F);  // Magenta
  gfx->print(paletteNames[paletteIndex % NUM_PALETTES]);

  // Right side info
  gfx->setCursor(LCD_WIDTH - 58, 6);
  gfx->setTextColor(0xFFE0);  // Yellow
  gfx->print(lcdBrightness * 100 / 255);
  gfx->print("%");

  gfx->setCursor(LCD_WIDTH - 58, 24);
  gfx->setTextColor(autoCycle ? 0x07E0 : 0xF800);
  gfx->print(autoCycle ? "AUTO" : "MAN");
}

// Draw full-screen menu
void drawMenu() {
  if (gfx == nullptr) return;

  gfx->fillScreen(0x0000);
  drawMenuHeader();

  int col1X = BTN_GAP;
  int col2X = BTN_GAP + BTN_WIDTH + BTN_GAP;
  int rowY = BTN_START_Y;

  if (menuPage == 0) {
    // === MAIN PAGE ===
    bool bgOn = (getBotBackgroundStyle() == 4);

    // Row 1: Background on/off | Cycle effect
    drawButton(col1X, rowY, BTN_WIDTH, BTN_HEIGHT, bgOn ? "BG: ON" : "BG: OFF", bgOn ? 0x0400 : 0x4000);
    drawButton(col2X, rowY, BTN_WIDTH, BTN_HEIGHT, "EFF >", 0x001F);
    rowY += BTN_HEIGHT + BTN_GAP;

    // Row 2: Palette | Hi-Res toggle
    drawButton(col1X, rowY, BTN_WIDTH, BTN_HEIGHT, "PALETTE", 0x4008);
    #if defined(HIRES_ENABLED)
    drawButton(col2X, rowY, BTN_WIDTH, BTN_HEIGHT, hiResMode ? "HI-RES" : "PIXEL", hiResMode ? 0x0400 : 0x4008);
    #else
    drawButton(col2X, rowY, BTN_WIDTH, BTN_HEIGHT, "PIXEL", 0x2104);
    #endif
    rowY += BTN_HEIGHT + BTN_GAP;

    // Row 3: Brightness
    drawButton(col1X, rowY, BTN_WIDTH, BTN_HEIGHT, "DIM", 0x4000);
    drawButton(col2X, rowY, BTN_WIDTH, BTN_HEIGHT, "BRIGHT", 0x0400);
    rowY += BTN_HEIGHT + BTN_GAP;

    // Row 4: Auto cycle | Close
    drawButton(col1X, rowY, BTN_WIDTH, BTN_HEIGHT, "AUTO", autoCycle ? 0x0400 : 0x4000);
    drawButton(col2X, rowY, BTN_WIDTH, BTN_HEIGHT, "CLOSE", 0x7800);
  }

  menuVisible = true;
}

// Hide menu and return to bot display
void hideMenu() {
  if (gfx == nullptr) return;
  DBGLN("Closing menu");
  gfx->fillScreen(0x0000);
  menuVisible = false;
  menuPage = 0;
}

// Action functions
void touchNextEffect() {
  effectIndex = (effectIndex + 1) % NUM_AMBIENT_EFFECTS;
  lastChange = millis();
}

void touchPrevEffect() {
  effectIndex = (effectIndex == 0) ? NUM_AMBIENT_EFFECTS - 1 : effectIndex - 1;
  lastChange = millis();
}

void touchBrightnessUp() {
  if (lcdBrightness < 255) {
    lcdBrightness += 25;
    if (lcdBrightness > 255) lcdBrightness = 255;
    analogWrite(LCD_BL, lcdBrightness);
  }
}

void touchBrightnessDown() {
  if (lcdBrightness > 25) {
    lcdBrightness -= 25;
    if (lcdBrightness < 25) lcdBrightness = 25;
    analogWrite(LCD_BL, lcdBrightness);
  }
}

void touchNextPalette() {
  paletteIndex = (paletteIndex + 1) % NUM_PALETTES;
  currentPalette = palettes[paletteIndex];
}

void touchToggleAutoCycle() {
  autoCycle = !autoCycle;
}

// Process touch on full-screen menu
bool processMenuTouch(uint16_t x, uint16_t y) {
  if (y < BTN_START_Y) return false;

  int row = (y - BTN_START_Y) / (BTN_HEIGHT + BTN_GAP);
  if (row > 3) row = 3;
  int col = (x < LCD_WIDTH / 2) ? 0 : 1;

  switch (row) {
    case 0:  // Background on/off | Cycle effect
      if (col == 0) {
        // Toggle ambient background on/off
        setBotBackgroundStyle(getBotBackgroundStyle() == 4 ? 0 : 4);
      } else {
        touchNextEffect();
      }
      break;
    case 1:  // Palette / Hi-Res toggle
      if (col == 0) touchNextPalette();
      #if defined(HIRES_ENABLED)
      else toggleHiResMode();
      #endif
      break;
    case 2:  // Brightness
      if (col == 0) touchBrightnessDown();
      else touchBrightnessUp();
      break;
    case 3:  // Auto / Close
      if (col == 0) touchToggleAutoCycle();
      else return true;
      break;
  }
  return false;
}

// Main touch handler - call in loop()
void handleTouch() {
  if (!touchInitialized) return;

  unsigned long now = millis();
  uint16_t x, y;

  bool touching = readTouch(x, y);

  // If waiting for finger lift, check if lifted
  if (waitingForLift) {
    if (!touching) {
      waitingForLift = false;
      touchStartTime = 0;
      longPressTriggered = false;
    }
    return;
  }

  if (touching) {
    lastTouchTime = now;

    // Menu not visible - check for long press to open
    if (!menuVisible) {
      if (touchStartTime == 0) {
        touchStartTime = now;
        longPressTriggered = false;
      }

      if (!longPressTriggered && (now - touchStartTime >= LONG_PRESS_MS)) {
        DBGLN("Long press - opening menu");
        longPressTriggered = true;
        lastActionTime = now;
        drawMenu();
        waitingForLift = true;
      }
      return;
    }

    // Menu is visible - handle button presses
    if (now - lastActionTime < TOUCH_COOLDOWN_MS) {
      return;
    }
    lastActionTime = now;

    bool shouldClose = processMenuTouch(x, y);

    if (shouldClose) {
      hideMenu();
      waitingForLift = true;
    } else {
      drawMenu();
      waitingForLift = true;
    }
  } else {
    // Finger lifted - short tap triggers bot reaction
    if (touchStartTime > 0 && !longPressTriggered && !menuVisible &&
        (now - touchStartTime < LONG_PRESS_MS)) {
      botMode.onTap();
      DBGLN("Bot tap reaction");
    }

    touchStartTime = 0;
    longPressTriggered = false;
  }

  // Hide menu after timeout
  if (menuVisible && !touching && lastTouchTime > 0) {
    if (now - lastTouchTime > MENU_TIMEOUT_MS) {
      hideMenu();
    }
  }
}

#else

// Stubs when touch is disabled
inline bool initTouch() { return false; }
inline void handleTouch() {}

#endif // TOUCH_ENABLED

#endif // TOUCH_CONTROL_H
