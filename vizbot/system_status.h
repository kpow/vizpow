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
  bool staConnected;     // STA connected to external network
  bool littlefsReady;    // LittleFS mounted successfully
  bool cloudRegistered;  // Registered with vizCloud
  bool speakerReady;     // Core S3 speaker initialized
  bool micReady;         // Core S3 microphone initialized
  bool proxLightReady;   // Core S3 proximity/light sensor initialized
  bool psramAvailable;   // PSRAM detected at boot
  uint32_t psramSizeKB;  // Total PSRAM in KB (0 if not available)
  IPAddress apIP;
  IPAddress staIP;       // IP on external network (when STA connected)
  uint32_t bootTimeMs;
  uint8_t failCount;
};

extern SystemStatus sysStatus;

#endif // SYSTEM_STATUS_H
