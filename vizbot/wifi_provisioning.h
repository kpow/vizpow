#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"
#include "system_status.h"
#include "mem.h"
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include "lwip/sockets.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"

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
// Connect — called from WiFi task (Core 0) when PROV_CONNECT_REQUESTED.
// Uses BLOCKING wait — identical to the working POC.
// ============================================================================

extern bool wifiEnabled;
extern void startDNS();
extern void stopDNS();
extern bool startMDNS();

void requestWifiConnect(const char* ssid, const char* pass) {
  strncpy(wifiProv.ssid, ssid, 32);
  wifiProv.ssid[32] = '\0';
  strncpy(wifiProv.pass, pass, 63);
  wifiProv.pass[63] = '\0';
  wifiProv.failReason[0] = '\0';
  saveWifiCredentials(ssid, pass, false);
  wifiProv.state = PROV_CONNECT_REQUESTED;
  Serial.print("WiFi connect requested for: ");
  Serial.println(ssid);
}

// Blocking connect — matches POC exactly. Called from WiFi task (Core 0).
void doWifiConnectBlocking() {
  Serial.println("=== doWifiConnectBlocking START ===");
  Serial.print("SSID: ");
  Serial.println(wifiProv.ssid);
  Serial.print("PASS len: ");
  Serial.println(strlen(wifiProv.pass));

  wifiProv.state = PROV_CONNECTING;

  // --- POC sequence with WLED-proven settings ---
  WiFi.disconnect(true);
  delay(200);

  WiFi.mode(WIFI_AP_STA);
  delay(100);

  // WLED-proven settings — critical for reliable connections
  WiFi.persistent(false);                          // Don't auto-save to NVS
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);       // Scan ALL channels
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);   // Pick strongest signal
  WiFi.setSleep(false);                             // Disable modem sleep
  esp_wifi_set_max_tx_power(WIFI_TX_POWER);  // Per-target TX power (config.h)
  WiFi.setAutoReconnect(false);                     // We handle retries ourselves

  WiFi.softAP(apSSID, WIFI_PASSWORD, 1, false, 4);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("TX power: ");
  { int8_t txq = 0; esp_wifi_get_max_tx_power(&txq); Serial.println(txq); }

  WiFi.begin(wifiProv.ssid, wifiProv.pass);
  esp_wifi_set_max_tx_power(WIFI_TX_POWER);  // Re-apply: begin() can reset it
  Serial.println("WiFi.begin() called, blocking wait (WLED settings)...");

  // Blocking poll with status logging
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    wl_status_t st = WiFi.status();
    Serial.printf("  try %2d/40  status:%d\n", tries + 1, st);
    if (st == WL_CONNECT_FAILED) break;
    if (st == WL_NO_SSID_AVAIL && tries > 5) break;
    tries++;
  }

  wl_status_t status = WiFi.status();
  Serial.print("Final status: ");
  Serial.println(status);

  if (status == WL_CONNECTED) {
    sysStatus.staConnected = true;
    sysStatus.staIP = WiFi.localIP();
    wifiProv.connectedAtMs = millis();
    wifiProv.state = PROV_CONNECTED;
    saveWifiCredentials(wifiProv.ssid, wifiProv.pass, true);
    MDNS.end();
    startMDNS();
    // Configure NTP time sync in the user's timezone (cloud timestamps stay UTC)
    configTzTime(timezoneTZ, "pool.ntp.org", "time.nist.gov");
    Serial.print("STA CONNECTED! IP: ");
    Serial.println(sysStatus.staIP);
    Serial.print("NTP configured (TZ=");
    Serial.print(timezoneTZ);
    Serial.println(")");
    // Non-blocking NTP sync poll (up to 5s)
    for (int i = 0; i < 10; i++) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo, 0) && timeinfo.tm_year > 100) {
        sysStatus.ntpSynced = true;
        sysStatus.ntpSyncedAt = millis();
        Serial.println("NTP synced");
        break;
      }
      delay(500);
    }
  } else {
    // Failed — back to AP-only
    if (status == WL_NO_SSID_AVAIL) {
      strncpy(wifiProv.failReason, "Network not found", sizeof(wifiProv.failReason));
    } else if (status == WL_CONNECT_FAILED) {
      strncpy(wifiProv.failReason, "Wrong password", sizeof(wifiProv.failReason));
    } else {
      snprintf(wifiProv.failReason, sizeof(wifiProv.failReason), "Timed out (status=%d)", status);
    }

    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    delay(100);
    WiFi.softAP(apSSID, WIFI_PASSWORD, 1, false, 4);
    WiFi.setSleep(false);
    esp_wifi_set_max_tx_power(WIFI_TX_POWER);
    clearWifiCredentials();
    wifiProv.state = PROV_FAILED;
    sysStatus.staConnected = false;

    Serial.print("STA FAILED: ");
    Serial.println(wifiProv.failReason);
  }
  Serial.println("=== doWifiConnectBlocking END ===");
}

// Poll AP linger — after STA connects, keep AP alive but stop captive portal DNS.
// ESP-NOW requires the AP interface to remain active on the same WiFi channel.
// The AP in STA+AP mode consumes minimal power (beacons only, no clients expected).
void pollWifiApLinger() {
  if (wifiProv.state != PROV_CONNECTED) return;

  if (millis() - wifiProv.connectedAtMs > WIFI_AP_LINGER_MS) {
    // Stop captive portal DNS (no longer needed), but keep AP alive for ESP-NOW
    DBGLN("AP linger expired — stopping DNS, keeping AP for ESP-NOW mesh");

    stopDNS();
    sysStatus.dnsReady = false;

    // mDNS stays running on STA interface
    wifiProv.state = PROV_STA_ACTIVE;
    sysStatus.wifiReady = true;  // still has web access via STA

    DBG("STA+AP mode (mesh). STA IP: ");
    DBGLN(sysStatus.staIP);
  }
}

// ============================================================================
// Boot STA — try saved credentials at startup (called from boot_sequence)
// ============================================================================
// Returns true if STA connected successfully. Boot sequence calls this
// AFTER starting the AP, so the AP is always available as fallback.

bool bootAttemptSTA() {
  // Load saved credentials from NVS
  char ssid[33] = {0};
  char pass[64] = {0};

  if (!loadWifiCredentials(ssid, pass)) {
    Serial.println("=== bootAttemptSTA: No saved credentials, skipping ===");
    return false;
  }

  Serial.println("=== bootAttemptSTA ===");
  Serial.print("SSID: ");
  Serial.println(ssid);

  // STA-only mode for boot connection
  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_STA);
  delay(100);

  // WLED-proven settings — these make the difference
  WiFi.persistent(false);                          // Don't auto-save to NVS (we manage it)
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);       // Scan ALL channels, not just first match
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);   // Pick strongest signal
  WiFi.setSleep(false);                             // Disable modem sleep
  esp_wifi_set_max_tx_power(WIFI_TX_POWER);  // Per-target TX power (config.h)
  WiFi.setAutoReconnect(false);                     // We handle retries ourselves

  WiFi.begin(ssid, pass);
  esp_wifi_set_max_tx_power(WIFI_TX_POWER);  // Re-apply: begin() can reset it
  Serial.println("WiFi.begin() called (WLED settings applied)");

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    wl_status_t st = WiFi.status();
    Serial.printf("  try %2d/40  status:%d\n", tries + 1, st);
    if (st == WL_CONNECT_FAILED) break;
    if (st == WL_NO_SSID_AVAIL && tries > 5) break;
    tries++;
  }

  wl_status_t finalStatus = WiFi.status();
  Serial.print("Final status: ");
  Serial.println(finalStatus);

  if (finalStatus == WL_CONNECTED) {
    sysStatus.staConnected = true;
    sysStatus.staIP = WiFi.localIP();
    // Configure NTP time sync in the user's timezone (cloud timestamps stay UTC)
    configTzTime(timezoneTZ, "pool.ntp.org", "time.nist.gov");
    Serial.print("CONNECTED! IP: ");
    Serial.println(sysStatus.staIP);
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());
    Serial.print("NTP configured (TZ=");
    Serial.print(timezoneTZ);
    Serial.println(")");
    // Non-blocking NTP sync poll (up to 5s)
    for (int i = 0; i < 10; i++) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo, 0) && timeinfo.tm_year > 100) {
        sysStatus.ntpSynced = true;
        sysStatus.ntpSyncedAt = millis();
        Serial.println("NTP synced");
        break;
      }
      delay(500);
    }
    return true;
  }

  Serial.println("FAILED — falling back to AP");
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP);
  delay(100);
  WiFi.softAP(apSSID, WIFI_PASSWORD, 1, false, 4);
  WiFi.setSleep(false);
  esp_wifi_set_max_tx_power(WIFI_TX_POWER);
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
    WiFi.softAP(apSSID, WIFI_PASSWORD, 1, false, 4);
    WiFi.setSleep(false);
    esp_wifi_set_max_tx_power(WIFI_TX_POWER);

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

// ============================================================================
// STA watchdog — rejoin the saved network after it drops
// ============================================================================
// Auto-reconnect is off (setAutoReconnect(false) above) and the boot attempt
// is the only other retry, so a router blip at night used to leave the bot
// offline until someone power-cycled it. Now: if STA has been down 30 s, call
// WiFi.begin() again with the saved, verified credentials, backing off
// 30 s → 1 → 2 → 4 → 5 min. This also covers a failed boot join (AP-only
// fallback). It never runs mid-provisioning or while someone is on the setup
// hotspot, so it can't pull the portal out from under them.

#define STA_RETRY_FIRST_MS  30000UL
#define STA_RETRY_MAX_MS    300000UL

static uint32_t staDownSinceMs = 0;
static uint32_t staNextRetryMs = 0;
static uint32_t staRetryGapMs = STA_RETRY_FIRST_MS;
uint16_t staReconnects = 0;          // successful rejoins since boot (shown in /state)

// ---- Dead link ---------------------------------------------------------------
// 22 Nov 2026: mac (StackChan) sat for minutes with WiFi "connected" (IP and
// -70 dBm on his own screen) while nothing went in or out: no ping, no ARP
// reply, no wake audio. The watchdog above only acts when WiFi.status() drops,
// so he never came back by himself. Proof of a working link has to come from
// the network: every STA_PROBE_MS, one ICMP echo to the gateway. After
// STA_PROBE_DEAD_FAILS misses in a row the link is dead: what he looked like
// is written to RTC memory (survives the reboot a hang would end in) and he
// rejoins. Three dead links without one good reply between them: restart.
#define STA_PROBE_MS          20000UL
#define STA_PROBE_TIMEOUT_MS  1500
#define STA_PROBE_DEAD_FAILS  4
#define STA_DEAD_RESTART      3
#define NET_DEAD_MAGIC        0x4E444544

struct NetDeadRecord {          // one dead link, as it looked when it was called
  uint32_t upSec, freeInt, maxBlockInt, allocFails, lastFailSize;
  int8_t   rssi;
  uint8_t  channel, bssidLast, action;   // action: 1 rejoin, 2 restart
  int16_t  sockErr;                      // last probe's socket error (0 none)
  uint16_t fails;
};
struct NetDeadLog {
  uint32_t magic;
  uint32_t total;               // dead links ever (across resets, until power-off)
  uint32_t restarts;            // of those, ended in a restart
  NetDeadRecord last;
};
RTC_NOINIT_ATTR NetDeadLog netDeadLog;

struct StaProbe {
  uint32_t ok = 0, fail = 0, sockFails = 0, nextMs = 0, lastOkMs = 0;
  bool     armed = false;       // the gateway has answered since boot: some routers never do
  uint16_t failRun = 0;
  uint8_t  deadRun = 0;         // dead links since the last good reply
  int16_t  sockErr = 0;
  uint16_t seq = 0;
  uint32_t deadThisBoot = 0;
};
static StaProbe staProbe;
static bool staRejoinNow = false;   // a dead link: the first rejoin is due now, not in 30 s

void netDeadLogInit() {
  if (netDeadLog.magic != NET_DEAD_MAGIC) { memset(&netDeadLog, 0, sizeof(netDeadLog)); netDeadLog.magic = NET_DEAD_MAGIC; }
}

// One ICMP echo to the gateway. 1 = reply, 0 = none in time, -1 = no socket.
static int staProbeGateway() {
  const IPAddress gw = WiFi.gatewayIP();
  if ((uint32_t)gw == 0) return 0;
  const int s = lwip_socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (s < 0) { staProbe.sockErr = (int16_t)errno; return -1; }
  struct timeval tv = { STA_PROBE_TIMEOUT_MS / 1000, (STA_PROBE_TIMEOUT_MS % 1000) * 1000 };
  lwip_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  struct sockaddr_in to = {};
  to.sin_family = AF_INET;
  to.sin_addr.s_addr = (uint32_t)gw;
  struct { struct icmp_echo_hdr h; uint8_t pad[8]; } req = {};
  ICMPH_TYPE_SET(&req.h, ICMP_ECHO);
  req.h.id = PP_HTONS(0x7662);   // "vb"
  req.h.seqno = lwip_htons(++staProbe.seq);
  req.h.chksum = inet_chksum(&req, sizeof(req));
  int got = 0;
  if (lwip_sendto(s, &req, sizeof(req), 0, (struct sockaddr*)&to, sizeof(to)) < 0) {
    staProbe.sockErr = (int16_t)errno;
  } else {
    staProbe.sockErr = 0;
    const uint32_t start = millis();
    uint8_t buf[64];
    while (!got && millis() - start < STA_PROBE_TIMEOUT_MS) {
      const int n = lwip_recvfrom(s, buf, sizeof(buf), 0, nullptr, nullptr);
      if (n <= 0) break;                                  // timed out
      const int ihl = (buf[0] & 0x0F) * 4;
      if (n < ihl + (int)sizeof(struct icmp_echo_hdr)) continue;
      const struct icmp_echo_hdr* r = (const struct icmp_echo_hdr*)(buf + ihl);
      if (ICMPH_TYPE(r) == ICMP_ER && r->id == req.h.id && r->seqno == req.h.seqno) got = 1;
    }
  }
  lwip_close(s);
  return got;
}

// The link is dead: keep what it looked like, then rejoin (or restart).
static void staLinkDead(uint32_t now) {
  netDeadLogInit();
  staProbe.deadRun++;
  staProbe.deadThisBoot++;
  NetDeadRecord& r = netDeadLog.last;
  r.upSec = now / 1000;
  r.freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  r.maxBlockInt = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  r.allocFails = memAllocFails(true);
  r.lastFailSize = memLastAllocFail().size;
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) { r.rssi = ap.rssi; r.channel = ap.primary; r.bssidLast = ap.bssid[5]; }
  else { r.rssi = 0; r.channel = 0; r.bssidLast = 0; }
  r.sockErr = staProbe.sockErr;
  r.fails = staProbe.failRun;
  r.action = staProbe.deadRun >= STA_DEAD_RESTART ? 2 : 1;
  netDeadLog.total++;
  Serial.printf("[WiFi] link dead: %u probes unanswered, rssi %d, internal free %u (block %u)\n",
                staProbe.failRun, r.rssi, (unsigned)r.freeInt, (unsigned)r.maxBlockInt);
  staProbe.failRun = 0;
  if (r.action == 2) {
    netDeadLog.restarts++;
    Serial.println("[WiFi] link dead 3 times without a reply between: restarting");
    delay(100);
    esp_restart();
  }
  // Drop the association; the watchdog below sees the link down and rejoins
  // straight away (its first retry is due now, not in 30 s).
  staRejoinNow = true;
  WiFi.disconnect(false, false);
}

// /debug/tasks "net": probe counts this boot, and the last dead link (kept across resets).
void staProbeJson(JsonObject o) {
  netDeadLogInit();
  o["probeOk"] = staProbe.ok;
  o["probeFail"] = staProbe.fail;
  o["probeSockFails"] = staProbe.sockFails;
  o["armed"] = staProbe.armed;
  o["lastOkSecAgo"] = staProbe.lastOkMs ? (millis() - staProbe.lastOkMs) / 1000 : -1;
  o["dead"] = staProbe.deadThisBoot;
  o["deadEver"] = netDeadLog.total;
  o["deadRestarts"] = netDeadLog.restarts;
  wifi_ap_record_t ap;
  o["rssi"] = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
  if (netDeadLog.total) {
    const NetDeadRecord& r = netDeadLog.last;
    JsonObject l = o["lastDead"].to<JsonObject>();
    l["upSec"] = r.upSec;
    l["rssi"] = r.rssi;
    l["channel"] = r.channel;
    l["bssidLast"] = r.bssidLast;
    l["freeInt"] = r.freeInt;
    l["maxBlockInt"] = r.maxBlockInt;
    l["allocFails"] = r.allocFails;
    l["lastFailSize"] = r.lastFailSize;
    l["sockErr"] = r.sockErr;
    l["fails"] = r.fails;
    l["action"] = r.action == 2 ? "restart" : "rejoin";
  }
}

static void staProbeTick(uint32_t now) {
  if ((int32_t)(now - staProbe.nextMs) < 0) return;
  staProbe.nextMs = now + STA_PROBE_MS;
  const int r = staProbeGateway();
  if (r < 0) { staProbe.sockFails++; return; }   // no socket: says nothing about the link
  if (r == 1) {
    staProbe.ok++;
    staProbe.lastOkMs = now;
    staProbe.armed = true;
    staProbe.failRun = 0;
    staProbe.deadRun = 0;
    return;
  }
  staProbe.fail++;
  if (!staProbe.armed) return;                    // a gateway that never answers proves nothing
  if (++staProbe.failRun >= STA_PROBE_DEAD_FAILS) staLinkDead(now);
}

void pollStaWatchdog() {
  if (wifiProv.state == PROV_CONNECT_REQUESTED || wifiProv.state == PROV_CONNECTING) return;
  uint32_t now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    if (!sysStatus.staConnected || ip != sysStatus.staIP) {
      bool rejoined = staDownSinceMs != 0;
      sysStatus.staConnected = true;
      sysStatus.staIP = ip;
      MDNS.end();
      startMDNS();
      if (rejoined) staReconnects++;
      Serial.printf("[WiFi] STA %s, IP %s\n", rejoined ? "rejoined" : "up", ip.toString().c_str());
      staProbe.nextMs = now + STA_PROBE_MS;   // a new link: let DHCP and ARP settle first
      staProbe.failRun = 0;                   // armed stays: a rejoin that is still dead must count
    }
    staProbeTick(now);
    staDownSinceMs = 0;
    staRetryGapMs = STA_RETRY_FIRST_MS;
    return;
  }

  if (staDownSinceMs == 0) {
    staDownSinceMs = now;
    staNextRetryMs = now + (staRejoinNow ? 0 : STA_RETRY_FIRST_MS);
    staRejoinNow = false;
    if (sysStatus.staConnected) Serial.println("[WiFi] STA lost — will rejoin");
    sysStatus.staConnected = false;
    return;
  }
  if ((int32_t)(now - staNextRetryMs) < 0) return;
  staNextRetryMs = now + staRetryGapMs;
  staRetryGapMs = min(staRetryGapMs * 2, STA_RETRY_MAX_MS);

  if (WiFi.softAPgetStationNum() > 0) return;   // someone's using the setup hotspot

  static char ssid[33], pass[64];
  static bool haveCreds = false;
  if (!haveCreds) haveCreds = loadWifiCredentials(ssid, pass);
  if (!haveCreds) return;                        // never set up: the portal is the way in

  Serial.printf("[WiFi] rejoining \"%s\" (down %lus)\n", ssid, (unsigned long)((now - staDownSinceMs) / 1000));
  if (WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);   // boot fell back to AP-only
  WiFi.disconnect(false, false);
  WiFi.begin(ssid, pass);                        // non-blocking; the next polls see the result
  esp_wifi_set_max_tx_power(WIFI_TX_POWER);
}

// Called from WiFi task on Core 0 — same core as handler
void pollWifiConnectTask() {
  if (wifiProv.state == PROV_CONNECT_REQUESTED) {
    doWifiConnectBlocking();  // blocks ~15s, exactly like the POC
  }
  pollWifiApLinger();
  pollStaWatchdog();
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
