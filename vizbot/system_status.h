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
  bool midiReady;        // SAM2695 MIDI synth initialized (Core S3 Grove Port C)
  bool micReady;         // Core S3 microphone initialized
  bool proxLightReady;   // Core S3 proximity/light sensor initialized
  bool psramAvailable;   // PSRAM detected at boot
  bool ntpSynced;            // NTP time obtained
  // StackChan base subsystems (only meaningful when BOARD_HAS_STACKCHAN_BASE)
#ifdef BOARD_HAS_STACKCHAN_BASE
  bool scIoExpanderReady;    // PY32L020 IO expander (I2C 0x6F)
  bool scVmEnReady;          // VM_EN servo power rail enabled
  bool scServoXReady;        // SCS0009 yaw servo
  bool scServoYReady;        // SCS0009 pitch servo
  bool scBaseLedsReady;      // WS2812C 12-LED ring
  bool scHeadTouchReady;     // Si12T capacitive touch (I2C 0x68)
  bool scBatteryMonReady;    // INA226 battery monitor (I2C 0x41)
  bool scCameraReady;        // GC0308 camera
  bool scPhotoFsReady;       // LittleFS photo storage partition
#endif
  unsigned long ntpSyncedAt; // millis() when NTP first synced
  uint32_t psramSizeKB;  // Total PSRAM in KB (0 if not available)
  IPAddress apIP;
  IPAddress staIP;       // IP on external network (when STA connected)
  uint32_t bootTimeMs;
  uint8_t failCount;
};

extern SystemStatus sysStatus;

#endif // SYSTEM_STATUS_H
