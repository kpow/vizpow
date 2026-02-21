#pragma once
// ============================================================================
// device_id.h — Per-Device Unique Network Identity
// ============================================================================
// Multiple vizbots on the same network need distinct SSIDs and mDNS hostnames.
//
// On first boot (or with no saved name), a 4-hex MAC suffix is used:
//   apSSID       = "vizBot-A3F2"
//   mdnsHostname = "vizbot-a3f2"   → resolves as vizbot-a3f2.local
//
// The user can set a friendly name via the web UI (/device/name endpoint).
// That name is saved to NVS and used on the next boot:
//   apSSID       = "vizbot-desk"
//   mdnsHostname = "vizbot-desk"   → resolves as vizbot-desk.local
//
// Call initDeviceID() once at the top of setup(), before runBootSequence().
// After that, use apSSID and mdnsHostname everywhere.
// ============================================================================

#include <Preferences.h>
#include "config.h"

#define DEVICE_NAME_MAX 24   // Fits in SSID (max 32) and mDNS label (max 63)

char apSSID[DEVICE_NAME_MAX];        // SSID as broadcast (original casing)
char mdnsHostname[DEVICE_NAME_MAX];  // mDNS label (lowercase, spaces→hyphens)

// Save a user-defined device name to NVS.
// Takes effect on next reboot (initDeviceID reads it at startup).
// Pass empty string to clear and revert to MAC-suffix fallback.
void saveDeviceName(const char* name) {
  Preferences prefs;
  prefs.begin("vizbot", false);
  prefs.putString("devName", name);
  prefs.end();
  DBG("Device name saved: ");
  DBGLN(name[0] ? name : "(cleared — MAC suffix on next boot)");
}

void initDeviceID() {
  // Try user-defined name from NVS first
  Preferences prefs;
  char savedName[DEVICE_NAME_MAX] = "";
  if (prefs.begin("vizbot", true)) {
    String n = prefs.getString("devName", "");
    if (n.length() > 0 && n.length() < DEVICE_NAME_MAX) {
      n.toCharArray(savedName, sizeof(savedName));
    }
    prefs.end();
  }

  if (savedName[0] != '\0') {
    // Custom name: SSID keeps original casing, mDNS goes lowercase + spaces→hyphens
    strncpy(apSSID, savedName, sizeof(apSSID) - 1);
    apSSID[sizeof(apSSID) - 1] = '\0';
    uint8_t i = 0;
    for (; i < strlen(savedName) && i < sizeof(mdnsHostname) - 1; i++) {
      char c = savedName[i];
      mdnsHostname[i] = (c == ' ') ? '-' : (char)tolower((unsigned char)c);
    }
    mdnsHostname[i] = '\0';
  } else {
    // Fallback: 4-hex MAC suffix, always unique per chip
    uint16_t suffix = (uint16_t)(ESP.getEfuseMac() & 0xFFFF);
    snprintf(apSSID,       sizeof(apSSID),       "%s-%04X", WIFI_SSID_BASE,     suffix);
    snprintf(mdnsHostname, sizeof(mdnsHostname),  "%s-%04x", MDNS_HOSTNAME_BASE, suffix);
  }

  DBG("Device ID: ");
  DBG(apSSID);
  DBG(" / ");
  DBGLN(mdnsHostname);
}
