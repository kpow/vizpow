#ifndef SYSTEM_STATUS_H
#define SYSTEM_STATUS_H

#include <Arduino.h>

// ============================================================================
// System Status — Tracks what subsystems are alive
// ============================================================================
// Populated during boot sequence, checked throughout firmware to skip
// dead hardware. Include this anywhere you need to read sysStatus.

struct SystemStatus {
  bool lcdReady;
  bool ledsReady;
  bool i2cReady;
  bool imuReady;
  bool touchReady;
  bool wifiReady;
  bool webServerReady;
  bool dnsReady;
  bool mdnsReady;
  bool settingsLoaded;
  IPAddress apIP;
  uint32_t bootTimeMs;
  uint8_t failCount;
  uint8_t bootReason;      // esp_reset_reason() value
};

extern SystemStatus sysStatus;

#endif // SYSTEM_STATUS_H
