#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// ESP8266 LED Matrix Configuration
// Bare ESP8266EX module driving 8x8 WS2812B grid via GPIO2
// ============================================================================

// Hardware
#define DATA_PIN 2  // GPIO2 (WS2812B data)

// Matrix dimensions
#define NUM_LEDS 64
#define MATRIX_WIDTH 8
#define MATRIX_HEIGHT 8

// WiFi AP configuration
#define WIFI_SSID "LED-Matrix-8266"
#define WIFI_PASSWORD "12345678"

// Display modes (no motion mode on ESP8266 - no IMU)
#define MODE_AMBIENT 0
#define MODE_EMOJI 1

// Effect counts
#define NUM_AMBIENT_EFFECTS 13
#define NUM_PALETTES 15

// Emoji settings
#define MAX_EMOJI_QUEUE 16
#define RANDOM_EMOJI_COUNT 8

// XY mapping (straight rows, no serpentine)
inline uint16_t XY(uint8_t x, uint8_t y) {
  return y * MATRIX_WIDTH + x;
}

#endif
