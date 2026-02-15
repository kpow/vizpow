#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"
#include "system_status.h"

// ============================================================================
// WiFi Provisioning — STA connection + NVS credential storage
// ============================================================================
// Handles connecting to a user's home WiFi network. Credentials are stored
// in NVS flash so the device auto-connects on subsequent boots.
//
// Key design decisions:
//   - AP_STA mode is ONLY used during the brief provisioning transition
//   - Once STA is confirmed, AP shuts down after WIFI_AP_LINGER_MS
//   - If STA fails at boot, device falls back to AP-only (no STA retries)
//   - Credentials saved with verified flag — only auto-connect if verified
//   - WiFi.scanNetworks(true) for async scan to avoid blocking
// ============================================================================

// Provisioning state machine
enum WifiProvState : uint8_t {
  PROV_IDLE = 0,        // Not doing anything
  PROV_SCANNING,        // Async WiFi scan in progress
  PROV_SCAN_DONE,       // Scan results ready
  PROV_CONNECT_REQUESTED, // Handler set credentials, main loop will connect
  PROV_CONNECTING,      // Attempting STA connection
  PROV_CONNECTED,       // STA connected, AP still alive (linger period)
  PROV_FAILED,          // STA connection failed
  PROV_STA_ACTIVE,      // STA-only mode, AP shut down
};

// Scan result entry (lightweight — we only keep what we need)
#define WIFI_MAX_SCAN_RESULTS 15

struct WifiScanEntry {
  char ssid[33];
  int8_t rssi;
  bool open;  // no password required
};

// Provisioning state — connect/poll all on Core 0 (WiFi task), scan on Core 1
struct WifiProvData {
  WifiProvState state;

  // Credentials being attempted
  char ssid[33];
  char pass[64];

  // Scan results
  WifiScanEntry scanResults[WIFI_MAX_SCAN_RESULTS];
  uint8_t scanCount;

  // Timing
  unsigned long connectStartMs;
  unsigned long connectedAtMs;  // when STA connected (for AP linger countdown)

  // Failure reason for UI
  char failReason[32];
};

static WifiProvData wifiProv = {};
static Preferences wifiPrefs;

// ============================================================================
// NVS Credential Storage
// ============================================================================

bool loadWifiCredentials(char* ssid, char* pass) {
  wifiPrefs.begin(WIFI_NVS_NAMESPACE, true);  // read-only
  bool verified = wifiPrefs.getBool("verified", false);
  if (verified) {
    String s = wifiPrefs.getString("ssid", "");
    String p = wifiPrefs.getString("pass", "");
    if (s.length() > 0) {
      strncpy(ssid, s.c_str(), 32);
      ssid[32] = '\0';
      strncpy(pass, p.c_str(), 63);
      pass[63] = '\0';
      wifiPrefs.end();
      return true;
    }
  }
  wifiPrefs.end();
  return false;
}

void saveWifiCredentials(const char* ssid, const char* pass, bool verified) {
  wifiPrefs.begin(WIFI_NVS_NAMESPACE, false);  // read-write
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("pass", pass);
  wifiPrefs.putBool("verified", verified);
  wifiPrefs.end();
  DBG("WiFi credentials saved (verified=");
  DBG(verified);
  DBGLN(")");
}

void clearWifiCredentials() {
  wifiPrefs.begin(WIFI_NVS_NAMESPACE, false);
  wifiPrefs.clear();
  wifiPrefs.end();
  DBGLN("WiFi credentials cleared");
}

bool hasVerifiedCredentials() {
  wifiPrefs.begin(WIFI_NVS_NAMESPACE, true);
  bool v = wifiPrefs.getBool("verified", false);
  wifiPrefs.end();
  return v;
}

// ============================================================================
// Scan — async WiFi scan
// ============================================================================

void startWifiScan() {
  wifiProv.state = PROV_SCANNING;
  wifiProv.scanCount = 0;
  WiFi.scanNetworks(true);  // async=true
  DBGLN("WiFi scan started (async)");
}

// Check scan progress — call from main loop
void pollWifiScan() {
  if (wifiProv.state != PROV_SCANNING) return;

  int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) return;  // still scanning

  if (result == WIFI_SCAN_FAILED || result < 0) {
    DBGLN("WiFi scan failed");
    wifiProv.state = PROV_SCAN_DONE;
    wifiProv.scanCount = 0;
    return;
  }

  // Collect results
  wifiProv.scanCount = min((int)result, (int)WIFI_MAX_SCAN_RESULTS);
  for (uint8_t i = 0; i < wifiProv.scanCount; i++) {
    strncpy(wifiProv.scanResults[i].ssid, WiFi.SSID(i).c_str(), 32);
    wifiProv.scanResults[i].ssid[32] = '\0';
    wifiProv.scanResults[i].rssi = WiFi.RSSI(i);
    wifiProv.scanResults[i].open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
  }

  WiFi.scanDelete();  // free memory
  wifiProv.state = PROV_SCAN_DONE;

  DBG("WiFi scan done: ");
  DBG(wifiProv.scanCount);
  DBGLN(" networks found");
}

// ============================================================================
// Connect — switch to AP_STA and attempt STA connection
// ============================================================================
// Request Connect — called from web handler, just saves creds + sets flag.
// The actual WiFi calls happen in the main loop via pollWifiProvisioning().
// This avoids calling WiFi.mode/begin from inside a handler on Core 0.
// ============================================================================

void requestWifiConnect(const char* ssid, const char* pass) {
  strncpy(wifiProv.ssid, ssid, 32);
  wifiProv.ssid[32] = '\0';
  strncpy(wifiProv.pass, pass, 63);
  wifiProv.pass[63] = '\0';
  wifiProv.failReason[0] = '\0';

  // Save credentials immediately (unverified)
  saveWifiCredentials(ssid, pass, false);

  // Set flag — main loop will pick this up and do the actual connection
  wifiProv.state = PROV_CONNECT_REQUESTED;

  DBG("WiFi connect requested for: ");
  DBGLN(ssid);
}

// ============================================================================
// Begin Connect — called from MAIN LOOP (not from handler).
// Follows the exact same pattern as the working POC.
// ============================================================================

extern bool wifiEnabled;
extern void startDNS();
extern void stopDNS();
extern bool startMDNS();

void beginWifiConnect() {
  DBGLN("--- beginWifiConnect (main loop) ---");

  // Clean slate — POC proved this is required for ESP32 WiFi stack
  WiFi.disconnect(true);
  delay(100);

  // Switch to AP_STA (same sequence as POC)
  WiFi.mode(WIFI_AP_STA);
  delay(100);

  // Re-establish the AP (mode change drops it)
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, 1, false, 4);

  // Start STA connection (full TX power — don't set 8.5dBm here)
  WiFi.begin(wifiProv.ssid, wifiProv.pass);

  wifiProv.state = PROV_CONNECTING;
  wifiProv.connectStartMs = millis();

  DBG("WiFi STA connecting to: ");
  DBGLN(wifiProv.ssid);
}

// Poll STA connection progress — call from main loop
void pollWifiConnect() {
  if (wifiProv.state != PROV_CONNECTING) return;

  wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED) {
    // Success!
    sysStatus.staConnected = true;
    sysStatus.staIP = WiFi.localIP();
    wifiProv.connectedAtMs = millis();
    wifiProv.state = PROV_CONNECTED;

    // Mark credentials as verified
    saveWifiCredentials(wifiProv.ssid, wifiProv.pass, true);

    // Restart mDNS on STA interface
    MDNS.end();
    startMDNS();

    DBG("WiFi STA connected! IP: ");
    DBGLN(sysStatus.staIP);
    return;
  }

  // Check timeout
  if (millis() - wifiProv.connectStartMs > WIFI_STA_CONNECT_TIMEOUT_MS) {
    // Failed — go back to AP-only
    DBGLN("WiFi STA connection timed out");

    if (status == WL_NO_SSID_AVAIL) {
      strncpy(wifiProv.failReason, "Network not found", sizeof(wifiProv.failReason));
    } else if (status == WL_CONNECT_FAILED) {
      strncpy(wifiProv.failReason, "Wrong password", sizeof(wifiProv.failReason));
    } else {
      strncpy(wifiProv.failReason, "Connection timed out", sizeof(wifiProv.failReason));
    }

    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    delay(100);

    // Re-establish AP
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, 1, false, 4);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    // Clear unverified credentials so we don't try them again at boot
    clearWifiCredentials();

    wifiProv.state = PROV_FAILED;
    sysStatus.staConnected = false;

    DBG("WiFi STA failed: ");
    DBGLN(wifiProv.failReason);
  }
}

// Poll AP linger — after STA connects, keep AP alive for a while then shut it down
void pollWifiApLinger() {
  if (wifiProv.state != PROV_CONNECTED) return;

  if (millis() - wifiProv.connectedAtMs > WIFI_AP_LINGER_MS) {
    // Shut down AP, switch to STA-only
    DBGLN("AP linger expired — switching to STA-only");

    stopDNS();
    sysStatus.dnsReady = false;
    WiFi.softAPdisconnect(true);

    // mDNS stays running on STA interface
    wifiProv.state = PROV_STA_ACTIVE;
    sysStatus.wifiReady = true;  // still has web access via STA

    DBG("STA-only mode. IP: ");
    DBGLN(sysStatus.staIP);
  }
}

// ============================================================================
// Boot STA — try saved credentials at startup (called from boot_sequence)
// ============================================================================
// Returns true if STA connected successfully. Boot sequence calls this
// AFTER starting the AP, so the AP is always available as fallback.

bool bootAttemptSTA() {
  char ssid[33] = {};
  char pass[64] = {};

  if (!loadWifiCredentials(ssid, pass)) {
    DBGLN("No saved WiFi credentials");
    return false;
  }

  DBG("Saved WiFi found: ");
  DBGLN(ssid);

  // Clean slate before mode switch (POC pattern)
  WiFi.disconnect(true);
  delay(100);

  // Switch to AP_STA to keep AP alive during attempt
  WiFi.mode(WIFI_AP_STA);
  delay(100);

  // Re-establish AP after mode change
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, 1, false, 4);

  // Attempt STA connection (full TX power for router reach)
  WiFi.begin(ssid, pass);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_STA_CONNECT_TIMEOUT_MS) {
    delay(250);
    DBG(".");
  }
  DBGLN();

  if (WiFi.status() == WL_CONNECTED) {
    sysStatus.staConnected = true;
    sysStatus.staIP = WiFi.localIP();
    strncpy(wifiProv.ssid, ssid, 32);
    wifiProv.state = PROV_CONNECTED;
    wifiProv.connectedAtMs = millis();

    DBG("STA connected at boot! IP: ");
    DBGLN(sysStatus.staIP);
    return true;
  }

  // Failed — go back to AP-only
  DBGLN("STA boot connect failed — staying in AP mode");
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP);
  delay(100);

  // Re-establish AP (low TX power is fine — phone is nearby)
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, 1, false, 4);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  // Update AP IP (might have changed after mode switch)
  sysStatus.apIP = WiFi.softAPIP();

  return false;
}

// ============================================================================
// Reset — forget credentials, revert to AP-only
// ============================================================================

extern void startDNS();

void resetWifiProvisioning() {
  clearWifiCredentials();

  if (sysStatus.staConnected || wifiProv.state == PROV_STA_ACTIVE) {
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    delay(100);
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, 1, false, 4);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    // Restart captive portal DNS
    startDNS();
    sysStatus.dnsReady = true;
    MDNS.end();
    startMDNS();
    sysStatus.mdnsReady = true;
  }

  sysStatus.staConnected = false;
  sysStatus.staIP = IPAddress(0, 0, 0, 0);
  wifiProv.state = PROV_IDLE;
  wifiProv.ssid[0] = '\0';
  wifiProv.pass[0] = '\0';

  DBGLN("WiFi provisioning reset — AP-only mode");
}

// ============================================================================
// Main loop poll — call once per frame
// ============================================================================

// Called from WiFi task on Core 0 — same core as handler, no cross-core issue
void pollWifiConnectTask() {
  if (wifiProv.state == PROV_CONNECT_REQUESTED) {
    beginWifiConnect();
  }
  pollWifiConnect();
  pollWifiApLinger();
}

// Called from main loop on Core 1 — only scan (no connect state needed)
void pollWifiProvisioning() {
  pollWifiScan();
}

// ============================================================================
// Status JSON — for /wifi/status endpoint
// ============================================================================

String getWifiStatusJson() {
  String json = "{\"state\":\"";

  switch (wifiProv.state) {
    case PROV_IDLE:              json += "idle"; break;
    case PROV_SCANNING:          json += "scanning"; break;
    case PROV_SCAN_DONE:         json += "scan_done"; break;
    case PROV_CONNECT_REQUESTED: json += "connecting"; break;  // show as connecting
    case PROV_CONNECTING:        json += "connecting"; break;
    case PROV_CONNECTED:         json += "connected"; break;
    case PROV_FAILED:            json += "failed"; break;
    case PROV_STA_ACTIVE:        json += "sta_active"; break;
  }
  json += "\"";

  if (wifiProv.state == PROV_CONNECT_REQUESTED || wifiProv.state == PROV_CONNECTING ||
      wifiProv.state == PROV_CONNECTED || wifiProv.state == PROV_STA_ACTIVE ||
      wifiProv.state == PROV_FAILED) {
    json += ",\"ssid\":\"";
    json += wifiProv.ssid;
    json += "\"";
  }

  if (sysStatus.staConnected) {
    json += ",\"ip\":\"";
    json += sysStatus.staIP.toString();
    json += "\"";
  }

  if (wifiProv.state == PROV_FAILED) {
    json += ",\"reason\":\"";
    json += wifiProv.failReason;
    json += "\"";
  }

  if (wifiProv.state == PROV_SCAN_DONE) {
    json += ",\"networks\":[";
    for (uint8_t i = 0; i < wifiProv.scanCount; i++) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"";
      // Escape quotes in SSID
      for (const char* c = wifiProv.scanResults[i].ssid; *c; c++) {
        if (*c == '"') json += "\\\"";
        else json += *c;
      }
      json += "\",\"rssi\":";
      json += wifiProv.scanResults[i].rssi;
      json += ",\"open\":";
      json += wifiProv.scanResults[i].open ? "true" : "false";
      json += "}";
    }
    json += "]";
  }

  json += "}";
  return json;
}

#endif // WIFI_PROVISIONING_H
