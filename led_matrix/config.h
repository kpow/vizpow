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
#elif defined(TARGET_LCD)
  #define BOARD_ESP32S3_TOUCH_LCD
  #define DISPLAY_LCD_ONLY
  #define HIRES_ENABLED  // Enable hi-res rendering mode for LCD
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

// WiFi AP configuration (unique per board)
#if defined(TARGET_LCD)
  #define WIFI_SSID "LCD-Matrix"
#else
  #define WIFI_SSID "LED-Matrix"
#endif
#define WIFI_PASSWORD "12345678"

// Display modes
#define MODE_MOTION 0
#define MODE_AMBIENT 1
#define MODE_EMOJI 2

// Effect counts
#define NUM_MOTION_EFFECTS 12
#define NUM_AMBIENT_EFFECTS 13
#define NUM_PALETTES 15

// Emoji settings
#define MAX_EMOJI_QUEUE 16

// Shake-to-change-mode settings
#define SHAKE_THRESHOLD 2.0      // Acceleration magnitude to count as a shake (g)
#define SHAKE_COUNT 3            // Number of shakes needed to trigger mode change
#define SHAKE_WINDOW_MS 1500     // Time window to count shakes (ms)
#define SHAKE_COOLDOWN_MS 2000   // Cooldown after mode change (ms)
#define RANDOM_EMOJI_COUNT 8     // Number of random emojis to add when entering emoji mode

// XY mapping - trying NO serpentine (straight rows)
inline uint16_t XY(uint8_t x, uint8_t y) {
  return y * MATRIX_WIDTH + x;
}

#endif
