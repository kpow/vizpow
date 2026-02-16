/*
 * WiFi STA Diagnostic — ESP32-S3-Touch-LCD-1.69
 *
 * Pure serial diagnostic. No LCD, no LEDs, no libraries beyond WiFi.
 * Tests connection to "powerhouse" network with full logging.
 *
 * Applies all WLED-proven WiFi settings and reports:
 *   - Auth type of the target network (WPA2 vs WPA3 vs mixed)
 *   - Channel the network is on
 *   - RSSI (signal strength)
 *   - Connection status at each retry
 *   - Failure reason codes
 *
 * Flash this to the LCD board and open Serial Monitor at 115200.
 */

#include <WiFi.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

// Target network
const char* TARGET_SSID = "iPhone";
const char* TARGET_PASS = "z1b3jukfjyfay";

// Auth type names for logging
const char* authModeName(wifi_auth_mode_t auth) {
  switch (auth) {
    case WIFI_AUTH_OPEN:            return "OPEN";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2-PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2-PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3-PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3-PSK";
    case WIFI_AUTH_WAPI_PSK:        return "WAPI-PSK";
    default:                        return "UNKNOWN";
  }
}

const char* statusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:    return "IDLE";
    case WL_NO_SSID_AVAIL:  return "NO_SSID";
    case WL_CONNECTED:       return "CONNECTED";
    case WL_CONNECT_FAILED:  return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED:    return "DISCONNECTED";
    default:                 return "UNKNOWN";
  }
}

// WiFi event handler — logs disconnect reason codes
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.printf("  [EVENT] STA disconnected — reason: %d\n", info.wifi_sta_disconnected.reason);
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("  [EVENT] STA connected to AP");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("  [EVENT] Got IP: %s\n", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
      break;
    default:
      break;
  }
}

// Attempt STA connection and return true if connected
bool tryConnect(const char* label) {
  Serial.printf("\n--- %s ---\n", label);

  WiFi.disconnect(true);  // true = erase IDF stored credentials
  delay(200);

  WiFi.mode(WIFI_STA);
  delay(100);

  // WLED-proven settings
  WiFi.persistent(false);                          // Don't auto-save to NVS
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);       // Scan ALL channels
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);   // Pick strongest signal
  WiFi.setSleep(false);                             // Disable modem sleep
  WiFi.setTxPower(WIFI_POWER_19_5dBm);             // Full TX power
  WiFi.setAutoReconnect(false);                     // We handle retries ourselves

  Serial.println("Settings applied:");
  Serial.println("  persistent=false, scanMethod=ALL_CHANNEL, sleep=OFF");
  Serial.printf("  txPower=%d (19.5dBm), autoReconnect=OFF\n", WiFi.getTxPower());

  Serial.printf("Calling WiFi.begin(\"%s\", <pass>)...\n", TARGET_SSID);
  WiFi.begin(TARGET_SSID, TARGET_PASS);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    wl_status_t st = WiFi.status();
    Serial.printf("  try %2d/40  status:%d (%s)\n", tries + 1, st, statusName(st));

    // If we get a definitive failure, don't waste time
    if (st == WL_CONNECT_FAILED) {
      Serial.println("  >> CONNECT_FAILED — likely wrong password or auth mismatch");
      break;
    }
    if (st == WL_NO_SSID_AVAIL && tries > 5) {
      Serial.println("  >> NO_SSID after 5 tries — network not visible on any channel");
      break;
    }
    tries++;
  }

  wl_status_t finalStatus = WiFi.status();
  Serial.printf("Final status: %d (%s)\n", finalStatus, statusName(finalStatus));

  if (finalStatus == WL_CONNECTED) {
    Serial.printf("CONNECTED!\n");
    Serial.printf("  IP:      %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("  DNS:     %s\n", WiFi.dnsIP().toString().c_str());
    Serial.printf("  RSSI:    %d dBm\n", WiFi.RSSI());
    Serial.printf("  Channel: %d\n", WiFi.channel());
    Serial.printf("  BSSID:   %s\n", WiFi.BSSIDstr().c_str());
    return true;
  }

  return false;
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n\n========================================");
  Serial.println("  WiFi STA Diagnostic — LCD Board");
  Serial.println("========================================");
  Serial.printf("Target: \"%s\"\n", TARGET_SSID);
  Serial.printf("Chip:   %s rev%d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("MAC:    %s\n", WiFi.macAddress().c_str());
  Serial.printf("Heap:   %d bytes free\n", ESP.getFreeHeap());

  // ==========================================
  // STEP 0: Erase ALL stale WiFi state
  // ==========================================
  Serial.println("\n[STEP 0] Erasing stale NVS WiFi state...");
  // This clears the IDF-level WiFi credential cache that WiFi.persistent(true)
  // left behind from previous sketches. This is a one-time cleanup.
  nvs_flash_erase();
  nvs_flash_init();
  Serial.println("  NVS erased and re-initialized");

  // Register event handler for disconnect reason logging
  WiFi.onEvent(onWiFiEvent);

  // ==========================================
  // STEP 1: Scan — find powerhouse and report details
  // ==========================================
  Serial.println("\n[STEP 1] Scanning all channels...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  int n = WiFi.scanNetworks(false, false, false, 300);  // sync, no hidden, no passive, 300ms/ch
  Serial.printf("Found %d networks:\n", n);

  bool targetFound = false;
  int targetChannel = -1;
  int targetRSSI = -999;
  wifi_auth_mode_t targetAuth = WIFI_AUTH_OPEN;

  for (int i = 0; i < n; i++) {
    wifi_auth_mode_t auth = WiFi.encryptionType(i);
    Serial.printf("  %-24s ch:%-2d  rssi:%-4d  auth:%d (%s)\n",
      WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
      auth, authModeName(auth));

    if (WiFi.SSID(i) == TARGET_SSID) {
      targetFound = true;
      targetChannel = WiFi.channel(i);
      targetRSSI = WiFi.RSSI(i);
      targetAuth = auth;
    }
  }
  WiFi.scanDelete();

  if (!targetFound) {
    Serial.printf("\n!! \"%s\" NOT FOUND in scan !!\n", TARGET_SSID);
    Serial.println("Possible causes:");
    Serial.println("  - Router is off or not broadcasting SSID");
    Serial.println("  - Router is on channel 12/13 (region-locked)");
    Serial.println("  - Hardware issue with this ESP32's radio");
    Serial.println("\nWill still attempt WiFi.begin() in case scan missed it...");
  } else {
    Serial.printf("\n>> \"%s\" found: ch=%d  rssi=%d dBm  auth=%s\n",
      TARGET_SSID, targetChannel, targetRSSI, authModeName(targetAuth));

    if (targetAuth == WIFI_AUTH_WPA3_PSK) {
      Serial.println("!! WARNING: Network is WPA3-ONLY. ESP32-S3 support can be flaky.");
    } else if (targetAuth == WIFI_AUTH_WPA2_WPA3_PSK) {
      Serial.println(">> Network uses WPA2/WPA3 transition mode.");
      Serial.println("   ESP32 should negotiate WPA2, but some IDF versions have bugs here.");
    }
  }

  // ==========================================
  // STEP 2: Attempt 1 — standard connect (WLED settings)
  // ==========================================
  Serial.println("\n[STEP 2] Attempt 1 — STA-only, WLED settings");
  if (tryConnect("Attempt 1: STA-only + WLED settings")) {
    Serial.println("\n*** SUCCESS on Attempt 1 ***");
    Serial.println("The fix for vizbot: apply these same WLED settings + STA-first approach.");
    return;
  }

  // ==========================================
  // STEP 3: Attempt 2 — lower min security (WPA3 workaround)
  // ==========================================
  Serial.println("\n[STEP 3] Attempt 2 — lowered min security threshold");
  Serial.println("  Setting WiFi.setMinSecurity(WIFI_AUTH_WEP) to accept any auth...");

  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.setMinSecurity(WIFI_AUTH_WEP);  // Accept anything
  delay(100);

  if (tryConnect("Attempt 2: Lowered min security")) {
    Serial.println("\n*** SUCCESS on Attempt 2 ***");
    Serial.println("Fix: need WiFi.setMinSecurity() in vizbot — default threshold was blocking auth.");
    return;
  }

  // ==========================================
  // STEP 4: Attempt 3 — specify channel from scan
  // ==========================================
  if (targetFound && targetChannel > 0) {
    Serial.printf("\n[STEP 4] Attempt 3 — specify channel %d directly\n", targetChannel);

    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.setMinSecurity(WIFI_AUTH_WEP);

    Serial.printf("Calling WiFi.begin(\"%s\", <pass>, %d)...\n", TARGET_SSID, targetChannel);
    WiFi.begin(TARGET_SSID, TARGET_PASS, targetChannel);

    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 40) {
      delay(500);
      wl_status_t st = WiFi.status();
      Serial.printf("  try %2d/40  status:%d (%s)\n", tries + 1, st, statusName(st));
      if (st == WL_CONNECT_FAILED) break;
      tries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\n*** SUCCESS on Attempt 3 (channel %d) ***\n", targetChannel);
      Serial.printf("  IP: %s  RSSI: %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      Serial.println("Fix: pass explicit channel to WiFi.begin() in vizbot.");
      return;
    }
  }

  // ==========================================
  // STEP 5: All attempts failed
  // ==========================================
  Serial.println("\n========================================");
  Serial.println("  ALL ATTEMPTS FAILED");
  Serial.println("========================================");
  Serial.println("Diagnostic summary:");
  Serial.printf("  Target SSID:  \"%s\"\n", TARGET_SSID);
  Serial.printf("  Visible:      %s\n", targetFound ? "YES" : "NO");
  if (targetFound) {
    Serial.printf("  Channel:      %d\n", targetChannel);
    Serial.printf("  RSSI:         %d dBm\n", targetRSSI);
    Serial.printf("  Auth type:    %s\n", authModeName(targetAuth));
  }
  Serial.println("\nNext steps:");
  Serial.println("  - Check serial output above for disconnect reason codes");
  Serial.println("  - Reason 15 = 4-way handshake timeout (usually wrong password or auth issue)");
  Serial.println("  - Reason 2  = auth expire");
  Serial.println("  - Reason 201 = no AP found (shouldn't happen if scan found it)");
  Serial.println("  - Try a different network to rule out board hardware issue");
}

void loop() {
  // If connected, print a keepalive every 10s to confirm stability
  static unsigned long lastPrint = 0;
  if (WiFi.status() == WL_CONNECTED && millis() - lastPrint > 10000) {
    lastPrint = millis();
    Serial.printf("[%lus] Still connected — RSSI: %d dBm  IP: %s\n",
      millis() / 1000, WiFi.RSSI(), WiFi.localIP().toString().c_str());
  }
}
