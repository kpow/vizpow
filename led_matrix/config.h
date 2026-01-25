#ifndef CONFIG_H
#define CONFIG_H

// Hardware configuration
#define NUM_LEDS 64
#define DATA_PIN 14
#define WIDTH 8
#define HEIGHT 8
#define I2C_SDA 11
#define I2C_SCL 12

// WiFi AP configuration
#define WIFI_SSID "LED-Matrix"
#define WIFI_PASSWORD "12345678"

// Display modes
#define MODE_MOTION 0
#define MODE_AMBIENT 1
#define MODE_EMOJI 2

// Effect counts
#define NUM_MOTION_EFFECTS 12
#define NUM_AMBIENT_EFFECTS 16
#define NUM_PALETTES 15

// Emoji settings
#define MAX_EMOJI_QUEUE 8

// XY mapping for serpentine matrix layout
inline uint16_t XY(uint8_t x, uint8_t y) {
  if (y & 1) {
    return y * WIDTH + (WIDTH - 1 - x);
  } else {
    return y * WIDTH + x;
  }
}

#endif
