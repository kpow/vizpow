#pragma once
// ============================================================================
// device_id.h — Per-Device Unique Network Identity
// ============================================================================
// Multiple vizbots on the same network need distinct SSIDs and mDNS hostnames.
// This header computes both from the chip's eFuse MAC (always unique, always
// available before WiFi init).
//
// Call initDeviceID() once at the top of setup(), before runBootSequence().
// After that, use apSSID and mdnsHostname everywhere instead of the
// compile-time WIFI_SSID_BASE / MDNS_HOSTNAME_BASE constants.
//
// Example output for a chip whose low 16 MAC bits are 0xA3F2:
//   apSSID       = "vizBot-A3F2"
//   mdnsHostname = "vizbot-a3f2"   → resolves as vizbot-a3f2.local
// ============================================================================

#include "config.h"

char apSSID[16];        // "vizBot-XXXX\0"  (max 11 chars + null)
char mdnsHostname[20];  // "vizbot-xxxx\0"  (max 15 chars + null)

void initDeviceID() {
  uint16_t suffix = (uint16_t)(ESP.getEfuseMac() & 0xFFFF);
  snprintf(apSSID,       sizeof(apSSID),       "%s-%04X", WIFI_SSID_BASE,     suffix);
  snprintf(mdnsHostname, sizeof(mdnsHostname),  "%s-%04x", MDNS_HOSTNAME_BASE, suffix);
  DBG("Device ID: ");
  DBG(apSSID);
  DBG(" / ");
  DBGLN(mdnsHostname);
}
