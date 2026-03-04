#ifndef PROXIMITY_LIGHT_H
#define PROXIMITY_LIGHT_H

#ifdef TARGET_CORES3

#include <Arduino.h>
#include <M5Unified.h>

// ============================================================================
// Proximity & Ambient Light — LTR-553ALS-WA (Core S3 Internal I2C)
// ============================================================================
// Proximity sensor + ambient light sensor at I2C address 0x23.
// Uses M5.In_I2C for internal bus access (avoids Wire conflicts).
// Updates every 100ms. Provides proximity detection, cover detection,
// and auto-brightness from ambient lux.
// ============================================================================

#define PROX_I2C_ADDR     0x23

// LTR-553 register addresses
#define LTR553_ALS_CONTR   0x80  // ALS control (active/standby)
#define LTR553_PS_CONTR    0x81  // PS control (active/standby)
#define LTR553_PS_LED      0x82  // PS LED pulse, current
#define LTR553_PS_N_PULSES 0x83  // PS number of pulses
#define LTR553_PS_MEAS     0x84  // PS measurement rate
#define LTR553_ALS_MEAS    0x85  // ALS measurement rate
#define LTR553_PART_ID     0x86  // Part ID (should be 0x92)
#define LTR553_MANUFAC_ID  0x87  // Manufacturer ID (should be 0x05)
#define LTR553_ALS_DATA_0  0x88  // ALS data low byte (ch1)
#define LTR553_ALS_DATA_1  0x89  // ALS data high byte (ch1)
#define LTR553_ALS_DATA_2  0x8A  // ALS data low byte (ch0)
#define LTR553_ALS_DATA_3  0x8B  // ALS data high byte (ch0)
#define LTR553_ALS_STATUS  0x8C  // ALS status
#define LTR553_PS_DATA_0   0x8D  // PS data low byte
#define LTR553_PS_DATA_1   0x8E  // PS data high byte (bits 2:0)
#define LTR553_PS_STATUS   0x8F  // PS status

// Detection thresholds
#define PROX_NEAR_THRESHOLD   200   // Raw proximity value for "hand nearby"
#define PROX_COVER_THRESHOLD  1500  // Raw proximity value for "sensor covered"
#define PROX_UPDATE_MS        100   // Update rate (100ms = 10Hz)

// Auto-brightness IIR smoothing
#define AUTO_BRIGHT_ALPHA     0.1f  // Lower = smoother transitions

struct ProxLightState {
  uint16_t rawProximity;        // Raw PS reading (0-2047, 11-bit)
  uint16_t ambientLux;          // Computed ambient light in lux
  bool nearDetected;            // Hand is nearby (rising edge for reactions)
  bool coverDetected;           // Sensor is fully covered
  uint8_t autoBrightness;       // IIR-smoothed brightness from lux (0-255)
  bool autoBrightnessEnabled;   // User toggle (default true)
  bool initialized;
  unsigned long lastUpdateMs;

  // Internal
  float smoothBrightness;       // Float for IIR filter

  void init() {
    rawProximity = 0;
    ambientLux = 0;
    nearDetected = false;
    coverDetected = false;
    autoBrightness = 255;
    autoBrightnessEnabled = true;
    initialized = false;
    lastUpdateMs = 0;
    smoothBrightness = 255.0f;

    // Verify manufacturer ID
    uint8_t manufId = readReg(LTR553_MANUFAC_ID);
    if (manufId != 0x05) {
      DBGLN("ProxLight: manufacturer ID mismatch, skipping init");
      return;
    }

    // Configure ALS: active mode, gain 1x (0x01)
    writeReg(LTR553_ALS_CONTR, 0x01);
    delay(10);

    // Configure PS: active mode (0x03 = active, 11-bit, 60kHz LED freq)
    writeReg(LTR553_PS_CONTR, 0x03);
    delay(10);

    // PS LED: 60kHz, 100% duty, 100mA current
    writeReg(LTR553_PS_LED, 0x7B);

    // PS pulses: 4 pulses
    writeReg(LTR553_PS_N_PULSES, 0x04);

    // PS measurement rate: 100ms
    writeReg(LTR553_PS_MEAS, 0x00);

    // ALS measurement rate: integration 100ms, repeat 100ms
    writeReg(LTR553_ALS_MEAS, 0x01);

    initialized = true;
    DBGLN("ProxLight: LTR-553 initialized");
  }

  // Call each frame — rate-limited internally
  void update() {
    if (!initialized) return;

    unsigned long now = millis();
    if (now - lastUpdateMs < PROX_UPDATE_MS) return;
    lastUpdateMs = now;

    // Read proximity (11-bit, 0-2047)
    uint8_t psLo = readReg(LTR553_PS_DATA_0);
    uint8_t psHi = readReg(LTR553_PS_DATA_1);
    rawProximity = ((uint16_t)(psHi & 0x07) << 8) | psLo;

    // Read ALS channel 0 and channel 1
    uint8_t alsLo0 = readReg(LTR553_ALS_DATA_2);
    uint8_t alsHi0 = readReg(LTR553_ALS_DATA_3);
    uint8_t alsLo1 = readReg(LTR553_ALS_DATA_0);
    uint8_t alsHi1 = readReg(LTR553_ALS_DATA_1);
    uint16_t ch0 = ((uint16_t)alsHi0 << 8) | alsLo0;
    uint16_t ch1 = ((uint16_t)alsHi1 << 8) | alsLo1;

    // Simple lux calculation (approximate for LTR-553)
    // Lux = (ch0 - ch1) * gain_factor. For gain=1x, integration=100ms:
    if (ch0 > ch1) {
      ambientLux = (ch0 - ch1);  // Simplified — 1:1 at gain 1x, 100ms
    } else {
      ambientLux = 0;
    }

    // Update detection flags
    nearDetected = (rawProximity > PROX_NEAR_THRESHOLD);
    coverDetected = (rawProximity > PROX_COVER_THRESHOLD);

    // Auto-brightness from ambient lux (IIR smoothed)
    if (autoBrightnessEnabled) {
      // Map lux to brightness: 0 lux -> 30 (minimum visible), 500+ lux -> 255
      float targetBright;
      if (ambientLux < 5) {
        targetBright = 30.0f;    // Very dark — dim backlight
      } else if (ambientLux > 500) {
        targetBright = 255.0f;   // Bright room — full brightness
      } else {
        targetBright = 30.0f + (float)ambientLux / 500.0f * 225.0f;
      }

      smoothBrightness = smoothBrightness * (1.0f - AUTO_BRIGHT_ALPHA) +
                          targetBright * AUTO_BRIGHT_ALPHA;
      autoBrightness = (uint8_t)constrain(smoothBrightness, 30.0f, 255.0f);
    }
  }

private:
  // Read a single register via M5 internal I2C
  uint8_t readReg(uint8_t reg) {
    uint8_t val = 0;
    M5.In_I2C.readRegister(PROX_I2C_ADDR, reg, &val, 1, 400000);
    return val;
  }

  // Write a single register via M5 internal I2C
  void writeReg(uint8_t reg, uint8_t val) {
    M5.In_I2C.writeRegister(PROX_I2C_ADDR, reg, &val, 1, 400000);
  }
};

// Global instance
ProxLightState proxLight;

#endif // TARGET_CORES3
#endif // PROXIMITY_LIGHT_H
