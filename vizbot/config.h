#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// Board Selection - JUST CHANGE THIS ONE LINE
// ============================================================================
// Uncomment ONE of these:
// #define TARGET_LED    // Waveshare ESP32-S3-Matrix (LED only)
#define TARGET_LCD       // ESP32-S3-Touch-LCD-1.69 (LCD + Touch)

// ============================================================================
// Auto-configuration based on target
// ============================================================================
#if defined(TARGET_LED)
  #define BOARD_ESP32S3_MATRIX
  #define DISPLAY_LED_ONLY
  // Power-saving profile for battery-powered LED matrix
  #define POWER_SAVE_ENABLED
  #define AUTO_WIFI_USB_DETECT       // WiFi ON when USB host detected, OFF on battery
  #define USB_DETECT_DELAY_MS 1500   // Time to wait for USB host enumeration at boot
  #define DEFAULT_BRIGHTNESS 10
  #define MAX_LED_POWER_MA 200       // FastLED auto-scales to this limit
  #define WIFI_TX_POWER WIFI_POWER_8_5dBm  // Reduced TX - phone is nearby
  #define FRAME_DELAY_EMOJI_STATIC 100     // 10 FPS - static image
  #define FRAME_DELAY_EMOJI_FADING 50      // 20 FPS - smooth crossfade
  #define FRAME_DELAY_AMBIENT_MIN 40       // 25 FPS cap for ambient effects
  #define INTRO_DURATION_MS 1000
  #define INTRO_FADE_RATE 40
  #define INTRO_SPARKLE_BRIGHTNESS 180
#elif defined(TARGET_LCD)
  #define BOARD_ESP32S3_TOUCH_LCD
  #define DISPLAY_LCD_ONLY
  #define HIRES_ENABLED  // Hi-res ambient for bot background overlay
  // Full power profile for USB-powered LCD board
  #define DEFAULT_BRIGHTNESS 15
  #define INTRO_DURATION_MS 2000
  #define INTRO_FADE_RATE 20
  #define INTRO_SPARKLE_BRIGHTNESS 255
#else
  #error "Please define TARGET_LED or TARGET_LCD"
#endif

// Manual override: uncomment to enable both displays (if hardware supports)
// #define DISPLAY_DUAL

// ============================================================================
// Hardware Configuration - Board Specific
// ============================================================================

#if defined(BOARD_ESP32S3_MATRIX)
  // Waveshare ESP32-S3-Matrix board pins
  #define DATA_PIN 14
  #define I2C_SDA 11
  #define I2C_SCL 12

#elif defined(BOARD_ESP32S3_TOUCH_LCD)
  // ESP32-S3-Touch-LCD-1.69 board pins
  #define DATA_PIN 14              // External LED matrix data pin (if used)
  #define I2C_SDA 11               // IMU/Touch I2C SDA
  #define I2C_SCL 10               // IMU/Touch I2C SCL

  // LCD pins (ST7789V2) - corrected from TFT_eSPI working config
  #define LCD_SCK 6
  #define LCD_MOSI 7
  #define LCD_CS 5
  #define LCD_DC 4
  #define LCD_RST 8
  #define LCD_BL 15

  // Touch controller (CST816T) - shares I2C bus with IMU
  #define TOUCH_I2C_ADDR 0x15
  #define TOUCH_ENABLED

#else
  #error "Please define a board: BOARD_ESP32S3_MATRIX or BOARD_ESP32S3_TOUCH_LCD"
#endif

// ============================================================================
// Common Configuration
// ============================================================================
#define NUM_LEDS 64
#define MATRIX_WIDTH 8
#define MATRIX_HEIGHT 8

// WiFi AP configuration
#define WIFI_SSID "vizBot"
#define WIFI_PASSWORD "12345678"

// Display mode
#define MODE_BOT 0

// Ambient effect count (used for bot background overlay)
#define NUM_AMBIENT_EFFECTS 13
#define NUM_PALETTES 15

// Shake detection threshold (for bot reactions)
#define SHAKE_THRESHOLD 2.0      // Acceleration magnitude to count as a shake (g)

// Debug serial output (comment out to save ~700 bytes of flash)
#define DEBUG_SERIAL
#ifdef DEBUG_SERIAL
  #define DBG(...) Serial.print(__VA_ARGS__)
  #define DBGLN(...) Serial.println(__VA_ARGS__)
#else
  #define DBG(...)
  #define DBGLN(...)
#endif

// XY mapping - trying NO serpentine (straight rows)
inline uint16_t XY(uint8_t x, uint8_t y) {
  return y * MATRIX_WIDTH + x;
}

#endif
