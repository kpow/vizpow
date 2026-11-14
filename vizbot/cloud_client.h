#ifndef CLOUD_CLIENT_H
#define CLOUD_CLIENT_H

#ifdef CLOUD_ENABLED

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "esp_http_client.h"
#include <mbedtls/platform.h>
#include <mbedtls/sha256.h>
#include "config.h"
#include "system_status.h"
#include "content_cache.h"
#include "device_id.h"    // deviceName: the name the cloud is told
#include "ota_update.h"   // upload-in-progress flag, update trial and rollback

// GTS Root R4 — Google Trust Services root CA used by DigitalOcean App Platform.
// Chain: server cert → WE1 (intermediate) → GTS Root R4 (this cert).
// Pinned instead of the full Mozilla CA bundle to avoid esp_crt_bundle static
// init crash on generic ESP32-S3 boards (TARGET_LCD). Valid until 2036-06-22.
static const char gts_root_r4_pem[] PROGMEM = R"PEM(
-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)PEM";

// ============================================================================
// Cloud Client — vizCloud HTTPS Communication
// ============================================================================
// Runs in cloudTask on Core 0 via pollCloudSync().
// Handles bot registration, periodic sync, command dispatch, and content updates.
//
// Speaks bot protocol v2 (vizCloud docs/rebuild/BOT-API-V2.md) and stays a v1
// bot towards a v1 server: every v2 field is an extra the old server ignores,
// and until a server issues this bot a token it keeps sending the v1 header.
//   register  + protocol, name, botId, resetReason      -> botToken, approval
//   sync      + protocol, name, firmwareVersion, configRev, resetReason (until
//               delivered once per boot), otaError (after a failed update)
//             -> approval, config { rev, ... }, firmware { buildId, ... }
//   401       -> wait CLOUD_AUTH_BACKOFF_MS, drop the token, register again
//
// Thread safety:
// - Pushes commands via FreeRTOS queue (cross-core safe)
// - CloudMeta written only on Core 0 (wifi task)
// - LittleFS writes guarded by contentUpdateInProgress flag
// - TLS naturally serialized with WLED HTTP — prevents heap fragmentation
// ============================================================================

// Forward declarations from task_manager.h
extern void cmdSetBrightness(uint8_t val);
extern void cmdSetExpression(uint8_t val);
extern void cmdSayText(const char* text, uint16_t durationMs);
extern void cmdSetBgStyle(uint8_t val);
extern void cmdSetPersonality(uint8_t val);
extern void cmdSetAmbientEffect(uint8_t val);
extern void cmdPlaySound(uint16_t freq, uint16_t duration);
extern void cmdPlaySequence(uint8_t seqId);
extern void cmdSetVolume(uint8_t vol);

// Forward declarations from bot_mode.h
extern uint8_t getBotPersonality();
extern uint8_t getBotExpression();
extern uint8_t getBotState();

// CloudMeta is defined in content_cache.h
extern CloudMeta cloudMeta;

// ============================================================================
// Cloud State
// ============================================================================

enum CloudState : uint8_t {
  CLOUD_IDLE,
  CLOUD_REGISTERING,
  CLOUD_REGISTERED,
  CLOUD_SYNCING,
  CLOUD_ERROR_AUTH,
  CLOUD_OFFLINE
};

static volatile CloudState cloudState = CLOUD_IDLE;

// Command ack ring buffer
#define CLOUD_ACK_SLOTS 8
static char commandAcks[CLOUD_ACK_SLOTS][48];
static uint8_t ackWriteIdx = 0;
static uint8_t ackCount = 0;

// Backoff state
static uint32_t cloudBackoffSec = 0;
static unsigned long lastSyncAttempt = 0;
static unsigned long cloudNextPollMs = 0;

// Protocol v2 identity. The token is this bot's credential: register issues it
// once and it lives in NVS. A v1 server never issues one, and then the bot
// carries on the v1 way.
static char cloudToken[48] = "";            // X-Bot-Token ("" = none yet)
static char cloudApproval[12] = "";         // approved | pending | disabled ("" = server did not say: v1)
static bool cloudNoTokenServer = false;     // register gave no token; do not ask again for now
static bool cloudTokenRetry = false;        // ...until cloudTokenRetryAtMs
static unsigned long cloudTokenRetryAtMs = 0;
static unsigned long cloudAuthRetryAtMs = 0;   // end of the back-off after a 401
static bool cloudHwTypeLegacy = false;      // server refused "stackchan": report the CoreS3 type instead
static bool cloudBootReported = false;      // resetReason has reached the server
static int cloudLastHttpCode = 0;
static volatile bool cloudEverSynced = false;
static volatile unsigned long cloudLastSyncOkMs = 0;

// Sync `config`: settings the admin changed, applied once per `rev`
static uint32_t cloudConfigRev = 0;         // last rev applied (reported as configRev)
static uint16_t cloudCfgPollSec = 0;        // poll interval it set (0 = none)
static char cloudCfgPersonality[32] = "";   // personality it named that is not loaded yet ("" = none)
static bool cloudCfgPersonalityDue = true;  // look for it again after boot / new content

// Cloud server: the built-in one, or the web panel's "Cloud server" setting.
// A custom server gets an identity of its own (NVS keys with a "t_" prefix), so
// the production id and token never reach a test server and are still there
// when the setting is cleared again.
static char cloudServerUrl[CLOUD_SERVER_URL_MAX] = CLOUD_SERVER_URL;   // in use, no trailing slash
static char cloudCustomServer[CLOUD_SERVER_URL_MAX] = "";              // the setting ("" = built-in)
static volatile bool cloudServerChanged = false;   // set by the web handler, acted on in the cloud task

// Firmware offered by sync, taken up after the response has been dealt with
struct CloudOtaOffer {
  bool present;
  char buildId[48];
  char version[32];
  char url[128];
  char sha256[65];
  uint32_t size;
};
static CloudOtaOffer cloudOtaOffer;
static char cloudOtaError[64] = "";         // reported as otaError until a sync accepts it
static bool cloudOtaHold = false;           // no update attempt before cloudOtaHoldUntilMs
static unsigned long cloudOtaHoldUntilMs = 0;

// Scheduled command buffer
#define SCHED_CMD_SLOTS 8
struct ScheduledCommand {
  char id[48];
  char type[20];
  uint8_t payloadBuf[128];
  uint16_t payloadLen;
  time_t executeAt;
  bool occupied;
};
static ScheduledCommand scheduledCmds[SCHED_CMD_SLOTS];

// ISO-8601 UTC parser
static time_t parseISO8601(const char* iso) {
  struct tm tm = {};
  sscanf(iso, "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
    &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
  tm.tm_year -= 1900;
  tm.tm_mon -= 1;
  return mktime(&tm);
}


// ============================================================================
// Hardware Type Mapping
// ============================================================================

static const char* getHardwareType() {
  #if defined(BOARD_HAS_STACKCHAN_BASE)
    return cloudHwTypeLegacy ? "m5stack_cores3" : "stackchan";
  #elif defined(TARGET_LCD)
    return "vizbot_lcd_169_touch";
  #elif defined(TARGET_LED)
    return "vizpow_led_matrix";
  #elif defined(TARGET_CORES3)
    return "m5stack_cores3";
  #else
    return "unknown";
  #endif
}

// ============================================================================
// Cloud Server Setting
// ============================================================================

// "" (built-in), https://host[:port], or http://host[:port] for a host on the
// local network only: 10.x, 172.16-31.x, 192.168.x or a .local name. Plain http
// carries the token in the clear, which is fine to a Mac on the same LAN and
// nowhere else.
static bool cloudServerUrlOk(const char* url) {
  if (!url[0]) return true;
  if (strlen(url) >= CLOUD_SERVER_URL_MAX) return false;
  bool https = strncmp(url, "https://", 8) == 0;
  if (!https && strncmp(url, "http://", 7) != 0) return false;

  const char* host = url + (https ? 8 : 7);
  size_t hostLen = 0;
  while (isalnum((unsigned char)host[hostLen]) || host[hostLen] == '.' || host[hostLen] == '-') hostLen++;
  if (hostLen == 0 || hostLen > 63) return false;
  const char* p = host + hostLen;
  if (*p == ':') {
    p++;
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) p++;
  }
  if (*p) return false;   // no path, query or anything else
  if (https) return true;

  char h[64];
  memcpy(h, host, hostLen);
  h[hostLen] = '\0';
  if (hostLen > 6 && strcasecmp(h + hostLen - 6, ".local") == 0) return true;
  int a, b, c, d;
  char canon[20];
  if (sscanf(h, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return false;
  snprintf(canon, sizeof(canon), "%d.%d.%d.%d", a, b, c, d);
  if (strcmp(canon, h) != 0) return false;   // plain dotted decimal only: no octal, no trailing label
  if ((a | b | c | d) < 0 || a > 255 || b > 255 || c > 255 || d > 255) return false;
  return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168);
}

static String cloudKey(const char* base) {
  return cloudCustomServer[0] ? String("t_") + base : String(base);
}

// ============================================================================
// NVS Persistence: identity and applied config, per server
// ============================================================================

static void loadCloudNVS() {
  cloudMeta.botId[0] = '\0';
  cloudMeta.registered = false;
  cloudMeta.pollIntervalSec = CLOUD_POLL_DEFAULT;
  cloudMeta.contentVersion = 0;
  cloudToken[0] = '\0';
  cloudConfigRev = 0;
  cloudCfgPollSec = 0;
  cloudCfgPersonality[0] = '\0';
  cloudCustomServer[0] = '\0';
  strlcpy(cloudServerUrl, CLOUD_SERVER_URL, sizeof(cloudServerUrl));

  Preferences prefs;
  if (prefs.begin(CLOUD_NVS_NAMESPACE, true)) {
    String srv = prefs.getString("server", "");
    if (srv.length() > 0 && cloudServerUrlOk(srv.c_str())) {
      strlcpy(cloudCustomServer, srv.c_str(), sizeof(cloudCustomServer));
      strlcpy(cloudServerUrl, srv.c_str(), sizeof(cloudServerUrl));
    }
    if (prefs.getBool("srvDirty", false)) cloudServerChanged = true;   // rebooted before the change was taken up

    String id = prefs.getString(cloudKey("botId").c_str(), "");
    if (id.length() > 0 && id.length() < sizeof(cloudMeta.botId)) {
      strncpy(cloudMeta.botId, id.c_str(), sizeof(cloudMeta.botId) - 1);
      cloudMeta.botId[sizeof(cloudMeta.botId) - 1] = '\0';
      cloudMeta.registered = true;
    }
    cloudMeta.pollIntervalSec = max((uint16_t)CLOUD_POLL_DEFAULT, prefs.getUShort(cloudKey("pollInt").c_str(), CLOUD_POLL_DEFAULT));
    cloudMeta.contentVersion = prefs.getUInt(cloudKey("contVer").c_str(), 0);

    String tok = prefs.getString(cloudKey("token").c_str(), "");
    if (tok.length() < sizeof(cloudToken)) strlcpy(cloudToken, tok.c_str(), sizeof(cloudToken));
    cloudConfigRev = prefs.getUInt(cloudKey("cfgRev").c_str(), 0);
    uint16_t cfgPoll = prefs.getUShort(cloudKey("cfgPoll").c_str(), 0);
    if (cfgPoll >= 5 && cfgPoll <= 300) {
      cloudCfgPollSec = cfgPoll;
      cloudMeta.pollIntervalSec = cfgPoll;
    }
    strlcpy(cloudCfgPersonality, prefs.getString(cloudKey("cfgPers").c_str(), "").c_str(), sizeof(cloudCfgPersonality));
    strlcpy(cloudOtaError, prefs.getString("otaErr", "").c_str(), sizeof(cloudOtaError));
    prefs.end();
  }
}

static void saveCloudNVS() {
  Preferences prefs;
  if (prefs.begin(CLOUD_NVS_NAMESPACE, false)) {
    prefs.putString(cloudKey("botId").c_str(), cloudMeta.botId);
    prefs.putUShort(cloudKey("pollInt").c_str(), cloudMeta.pollIntervalSec);
    prefs.putUInt(cloudKey("contVer").c_str(), cloudMeta.contentVersion);
    prefs.putString(cloudKey("token").c_str(), cloudToken);
    prefs.putUInt(cloudKey("cfgRev").c_str(), cloudConfigRev);
    prefs.putUShort(cloudKey("cfgPoll").c_str(), cloudCfgPollSec);
    prefs.putString(cloudKey("cfgPers").c_str(), cloudCfgPersonality);
    prefs.end();
  }
}

// Web panel "Cloud server" field. Only stores the setting; the cloud task
// switches over (cloudApplyServerChange) so a sync in flight is not cut across.
// Setting a custom server again, even the same one, pairs with it afresh.
bool cloudSetServer(const char* url) {
  if (!cloudServerUrlOk(url)) return false;
  Preferences prefs;
  if (!prefs.begin(CLOUD_NVS_NAMESPACE, false)) return false;
  if (url[0] || prefs.getString("server", "").length() > 0) {   // built-in to built-in changes nothing
    prefs.putString("server", url);
    prefs.putBool("srvDirty", true);
    cloudServerChanged = true;
  }
  prefs.end();
  return true;
}

static const char* cloudResetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "poweron";
    case ESP_RST_EXT:       return "external";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int_wdt";
    case ESP_RST_TASK_WDT:  return "task_wdt";
    case ESP_RST_WDT:       return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    default:                return "unknown";
  }
}

// ============================================================================
// Ack Buffer Management
// ============================================================================

static void pushAck(const char* cmdId) {
  strncpy(commandAcks[ackWriteIdx], cmdId, sizeof(commandAcks[0]) - 1);
  commandAcks[ackWriteIdx][sizeof(commandAcks[0]) - 1] = '\0';
  ackWriteIdx = (ackWriteIdx + 1) % CLOUD_ACK_SLOTS;
  if (ackCount < CLOUD_ACK_SLOTS) ackCount++;
}

static void clearAcks() {
  memset(commandAcks, 0, sizeof(commandAcks));
  ackWriteIdx = 0;
  ackCount = 0;
}

// ============================================================================
// Build Registration JSON
// ============================================================================

static String buildRegistrationBody() {
  JsonDocument doc;
  doc["macAddress"] = WiFi.macAddress();
  doc["hardwareType"] = getHardwareType();
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  doc["localIp"] = sysStatus.staIP.toString();

  JsonObject caps = doc["capabilities"].to<JsonObject>();
  #if defined(LCD_WIDTH)
    caps["screenWidth"] = LCD_WIDTH;
    caps["screenHeight"] = LCD_HEIGHT;
  #else
    caps["screenWidth"] = 8;
    caps["screenHeight"] = 8;
  #endif
  #if defined(TOUCH_ENABLED)
    caps["hasTouch"] = true;
  #else
    caps["hasTouch"] = false;
  #endif
  caps["hasIMU"] = true;
  caps["hasLED"] = true;
  #ifdef TARGET_CORES3
  caps["hasAudio"] = true;
  #else
  caps["hasAudio"] = false;
  #endif
  caps["hasBotMode"] = true;

  // Protocol v2 (a v1 server ignores these)
  doc["protocol"] = CLOUD_PROTOCOL_VERSION;
  doc["name"] = deviceName;
  if (cloudMeta.botId[0]) doc["botId"] = cloudMeta.botId;
  doc["resetReason"] = cloudResetReasonStr();

  String body;
  serializeJson(doc, body);
  return body;
}

// ============================================================================
// Build Sync JSON
// ============================================================================

static String buildSyncBody() {
  JsonDocument doc;
  doc["contentVersion"] = cloudMeta.contentVersion;
  doc["status"] = "active";

  // Protocol v2 (a v1 server ignores these)
  doc["protocol"] = CLOUD_PROTOCOL_VERSION;
  doc["name"] = deviceName;
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  doc["configRev"] = cloudConfigRev;
  if (!cloudBootReported) doc["resetReason"] = cloudResetReasonStr();
  if (cloudOtaError[0]) doc["otaError"] = cloudOtaError;

  if (ackCount > 0) {
    JsonArray acks = doc["commandAcks"].to<JsonArray>();
    // Read acks from ring buffer
    uint8_t start = (ackWriteIdx + CLOUD_ACK_SLOTS - ackCount) % CLOUD_ACK_SLOTS;
    for (uint8_t i = 0; i < ackCount; i++) {
      uint8_t idx = (start + i) % CLOUD_ACK_SLOTS;
      if (strlen(commandAcks[idx]) > 0) {
        acks.add(commandAcks[idx]);
      }
    }
  }

  // Enhanced state reporting
  JsonObject state = doc["state"].to<JsonObject>();
  state["expression"] = getBotExpression();
  state["personality"] = getBotPersonality();
  state["botState"] = getBotState();  // BOT_ACTIVE=0, BOT_IDLE=1, etc.
  state["rssi"] = WiFi.RSSI();
  state["freeHeap"] = ESP.getFreeHeap();
  state["uptime"] = millis() / 1000;

  // NTP time (ISO-8601 UTC) — always UTC regardless of the display timezone.
  // time() returns UTC epoch seconds; gmtime_r keeps the breakdown in UTC.
  time_t nowSec = time(nullptr);
  struct tm timeinfo;
  if (nowSec > 1000000000 && gmtime_r(&nowSec, &timeinfo)) {  // >2001 ⇒ NTP synced
    char timeBuf[25];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    state["ntpTime"] = timeBuf;
    state["ntpSynced"] = true;
  } else {
    state["ntpSynced"] = false;
  }

  // IMU orientation
  extern float accelX, accelY, accelZ;
  if (sysStatus.imuReady) {
    state["ax"] = (int)(accelX * 100);
    state["ay"] = (int)(accelY * 100);
    state["az"] = (int)(accelZ * 100);
  }

  // ESP-NOW mesh peer count
  extern uint8_t meshGetPeerCount();
  state["meshPeers"] = meshGetPeerCount();

  // Core S3 environment sensors
  #ifdef TARGET_CORES3
  if (sysStatus.proxLightReady) {
    extern ProxLightState proxLight;
    state["lux"] = proxLight.ambientLux;
    state["proximity"] = proxLight.rawProximity;
  }
  #endif

  String body;
  serializeJson(doc, body);
  return body;
}

// ============================================================================
// HTTPS Request Helper
// ============================================================================

// ESP-IDF native HTTPS client with pinned root CA cert.
// Large responses (>2KB) are streamed to a LittleFS temp file so we never
// need to grow a String while TLS buffers are consuming most of the heap.
// After the TLS connection closes (~32KB freed), we read the file back.
// Every cloud request starts here: sync, register and the firmware download
// share the TLS setup and the identity header.
static esp_http_client_handle_t cloudHttpInit(const char* url, esp_http_client_method_t method) {
  esp_http_client_config_t config = {};
  config.url = url;
  config.method = method;
  config.buffer_size = 1024;      // was 2048 — cloud responses are 63-500B, saves 1KB during TLS
  config.buffer_size_tx = 512;    // was 1024 — request headers + body fit in 512B, saves 512B
  // https: pinned root. http is only ever a private-network test server
  // (cloudServerUrlOk), where there is no certificate to pin.
  if (strncmp(url, "https://", 8) == 0) config.cert_pem = gts_root_r4_pem;
  config.timeout_ms = CLOUD_RESPONSE_TIMEOUT;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return nullptr;

  // The token once there is one. The legacy header only until then: a v1 server
  // requires it, a v2 server ignores it.
  if (cloudToken[0]) {
    esp_http_client_set_header(client, "X-Bot-Token", cloudToken);
  } else {
    esp_http_client_set_header(client, "X-Bot-Secret", CLOUD_LEGACY_COMPAT_KEY);
  }
  return client;
}

static int cloudPost(const char* url, const String& body, String& response) {
  esp_http_client_handle_t client = cloudHttpInit(url, HTTP_METHOD_POST);
  if (!client) {
    response = "init failed";
    return -1;
  }

  esp_http_client_set_header(client, "Content-Type", "application/json");

  esp_err_t err = esp_http_client_open(client, body.length());
  if (err != ESP_OK) {
    DBG("Cloud: open err=");
    DBGLN(esp_err_to_name(err));
    response = esp_err_to_name(err);
    esp_http_client_cleanup(client);
    return -1;
  }

  esp_http_client_write(client, body.c_str(), body.length());

  int content_length = esp_http_client_fetch_headers(client);
  int httpCode = esp_http_client_get_status_code(client);

  DBG("Cloud: HTTP ");
  DBG(httpCode);
  DBG(" len=");
  DBGLN(content_length);

  // Decide: stream to temp file (large/unknown), or read direct (small).
  // During active TLS, heap is extremely tight on non-PSRAM boards (~6KB free).
  // Streaming to flash avoids any heap growth during the read.
  bool useFile = sysStatus.littlefsReady &&
                 (content_length > 2048 || content_length < 0);

  char buf[256];
  int read_len, totalRead = 0;

  if (useFile) {
    File tmp = LittleFS.open("/cloud_tmp.json", "w");
    if (!tmp) {
      DBGLN("Cloud: temp file failed, falling back to direct");
      useFile = false;
    } else {
      // Response cap. The full payload (e.g. registration ~43KB) must be
      // captured or the JSON is truncated → IncompleteInput → registration
      // never completes and the client retries forever. Boards with PSRAM can
      // hold the temp String + parse (the large alloc lands in PSRAM, and the
      // read happens after TLS closes); without PSRAM we stay conservative to
      // avoid OOM. Runtime check covers every target uniformly.
      const int CLOUD_MAX_RESPONSE = psramFound() ? 65536 : 16384;
      while ((read_len = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        tmp.write((uint8_t*)buf, read_len);
        totalRead += read_len;
        if (totalRead > CLOUD_MAX_RESPONSE) break;
      }
      tmp.close();

      // Close TLS first — frees ~32KB of SSL buffers back to heap
      esp_http_client_close(client);
      esp_http_client_cleanup(client);

      // Now read temp file with recovered heap
      File tmpR = LittleFS.open("/cloud_tmp.json", "r");
      if (tmpR) {
        response = tmpR.readString();
        tmpR.close();
      }
      LittleFS.remove("/cloud_tmp.json");

      DBG("Cloud: streamed ");
      DBG(totalRead);
      DBG("B via file, got ");
      DBGLN(response.length());
      return httpCode;
    }
  }

  // Small response (<=2KB) — read directly into String
  response = "";
  if (content_length > 0) response.reserve(content_length + 1);
  while ((read_len = esp_http_client_read(client, buf, sizeof(buf) - 1)) > 0) {
    buf[read_len] = '\0';
    response += buf;
    totalRead += read_len;
    if (totalRead > 4096) break;
  }

  DBG("Cloud: read ");
  DBG(totalRead);
  DBG("B direct, got ");
  DBGLN(response.length());

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return httpCode;
}

// ============================================================================
// Process Content from Server Response
// ============================================================================

static void processContent(JsonObject& content) {
  // Serialize content types separately for cache
  String sayingsStr;
  String personalitiesStr;
  String sequencesStr;

  if (content["sayings"].is<JsonArray>()) {
    serializeJson(content["sayings"], sayingsStr);
  }
  if (content["personalities"].is<JsonArray>()) {
    serializeJson(content["personalities"], personalitiesStr);
  }
  if (content["sequences"].is<JsonArray>()) {
    serializeJson(content["sequences"], sequencesStr);
  }

  if (sysStatus.littlefsReady) {
    writeCloudContent(sayingsStr, personalitiesStr);
    if (sequencesStr.length() > 0) {
      writeCloudSequences(sequencesStr);
    }
    saveCloudMeta(cloudMeta);
    applyCloudPersonalities();
    cloudCfgPersonalityDue = true;   // the personality config named may only exist now
#ifdef MIDI_SYNTH_ENABLED
    applyCloudSequences();
#endif
  }

  DBG("Cloud: cached ");
  DBG(sayingsStr.length());
  DBG("B sayings, ");
  DBG(personalitiesStr.length());
  DBG("B personalities, ");
  DBG(sequencesStr.length());
  DBGLN("B sequences");
}

// ============================================================================
// Command Dispatch
// ============================================================================

static void dispatchCloudCommand(const char* type, JsonObject& payload) {
  if (strcmp(type, "expression") == 0) {
    uint8_t val = payload["value"] | 0;
    cmdSetExpression(val);
    DBG("Cloud cmd: expression=");
    DBGLN(val);

  } else if (strcmp(type, "say") == 0) {
    const char* text = payload["text"] | "Hello";
    uint16_t dur = payload["duration"] | 5000;
    cmdSayText(text, dur);
    DBG("Cloud cmd: say=");
    DBGLN(text);

  } else if (strcmp(type, "personality") == 0) {
    const char* name = payload["name"] | "";
    uint8_t idx = 0;  // default to Chill
    if (strcmp(name, "Hyper") == 0)  idx = 1;
    else if (strcmp(name, "Grumpy") == 0) idx = 2;
    else if (strcmp(name, "Sleepy") == 0) idx = 3;
    cmdSetPersonality(idx);
    DBG("Cloud cmd: personality=");
    DBGLN(name);

  } else if (strcmp(type, "brightness") == 0) {
    uint8_t val = payload["value"] | 15;
    // Cloud sends 0-100, firmware uses 1-50
    val = constrain(val / 2, 1, 50);
    cmdSetBrightness(val);
    DBG("Cloud cmd: brightness=");
    DBGLN(val);

  } else if (strcmp(type, "background") == 0) {
    uint8_t val = payload["value"] | 0;
    cmdSetBgStyle(val);
    DBG("Cloud cmd: background=");
    DBGLN(val);

  } else if (strcmp(type, "ambient_effect") == 0) {
    uint8_t val = payload["value"] | 0;
    cmdSetAmbientEffect(val);
    DBG("Cloud cmd: ambient_effect=");
    DBGLN(val);

  } else if (strcmp(type, "sound") == 0) {
    uint16_t freq = payload["freq"] | 440;
    uint16_t dur = payload["duration"] | 200;
    cmdPlaySound(freq, dur);
    DBG("Cloud cmd: sound freq=");
    DBG(freq);
    DBG(" dur=");
    DBGLN(dur);

  } else if (strcmp(type, "play_sequence") == 0) {
    uint8_t seqId = payload["sequenceId"] | 0;
    cmdPlaySequence(seqId);
    DBG("Cloud cmd: play_sequence=");
    DBGLN(seqId);

  } else if (strcmp(type, "set_volume") == 0) {
    uint8_t vol = payload["value"] | 200;
    cmdSetVolume(vol);
    DBG("Cloud cmd: set_volume=");
    DBGLN(vol);

  } else if (strcmp(type, "auto_brightness") == 0) {
    extern bool autoBrightnessEnabled;
    bool enabled = payload["enabled"] | true;
    autoBrightnessEnabled = enabled;
    DBG("Cloud cmd: auto_brightness=");
    DBGLN(enabled ? "on" : "off");

  } else if (strcmp(type, "sleep") == 0) {
    // Sleep command dispatched via command queue
    extern void cmdSleep(uint32_t durationMs);
    uint32_t dur = payload["durationMs"] | 30000;
    cmdSleep(dur);
    DBG("Cloud cmd: sleep dur=");
    DBGLN(dur);

  } else if (strcmp(type, "mesh_scan") == 0) {
    extern volatile bool meshScanRequested;
    meshScanRequested = true;
    DBGLN("Cloud cmd: mesh_scan requested (stub)");

  } else if (strcmp(type, "reboot") == 0) {
    DBGLN("Cloud cmd: reboot");
    delay(100);
    ESP.restart();

  } else {
    DBG("Cloud cmd: unknown type=");
    DBGLN(type);
  }
}

// ============================================================================
// Sync `config` — settings changed in the admin
// ============================================================================
// Each key goes down the path the web panel uses for the same setting, so the
// result is identical to changing it locally.

// Personality by name, built-in or synced. Applied once, as a local change
// would be; after that the panel's choice stands. An unknown name waits (in NVS,
// across reboots) for the content update that brings that personality.
static void applyCloudCfgPersonality() {
  cloudCfgPersonalityDue = false;
  if (!cloudCfgPersonality[0]) return;
  for (uint8_t i = 0; i < runtimePersonalityCount; i++) {
    if (strcasecmp(runtimePersonalities[i].name, cloudCfgPersonality) == 0) {
      cmdSetPersonality(i);   // as GET /bot/personality?v=
      cloudCfgPersonality[0] = '\0';
      saveCloudNVS();
      return;
    }
  }
  DBG("Cloud config: no personality named ");
  DBGLN(cloudCfgPersonality);
}

static void applyCloudConfig(JsonObject cfg) {
  if (cfg["brightness"].is<int>()) {
    cmdSetBrightness(constrain(cfg["brightness"].as<int>(), 1, 255));   // as GET /brightness?v=
  }
  if (cfg["personality"].is<const char*>()) {
    strlcpy(cloudCfgPersonality, cfg["personality"].as<const char*>(), sizeof(cloudCfgPersonality));
    applyCloudCfgPersonality();
  }
  if (cfg["pollIntervalSec"].is<int>()) {
    cloudCfgPollSec = constrain(cfg["pollIntervalSec"].as<int>(), 5, 300);
    cloudMeta.pollIntervalSec = cloudCfgPollSec;
  }
  if (cfg["deviceName"].is<const char*>()) {
    // Same rules and same effect as GET /device/name: stored now, and the SSID
    // and mDNS hostname follow on the next boot. `name` reports it from now on.
    String name = cfg["deviceName"].as<const char*>();
    name.trim();
    if (name.length() > 0 && name.length() < DEVICE_NAME_MAX) saveDeviceName(name.c_str());
  }
  cloudConfigRev = cfg["rev"] | cloudConfigRev;
  saveCloudNVS();
  DBG("Cloud: config rev ");
  DBGLN(cloudConfigRev);
}

// ============================================================================
// Cloud Firmware Update
// ============================================================================
// Sync offers `firmware` while an update is assigned to this bot. The image is
// downloaded with the bot's token over the same client as everything else,
// written to the app slot that is not running, and the boot slot is switched
// only once its size and SHA-256 match the offer. The new firmware then runs on
// trial until its first sync (ota_update.h). A failure is reported as otaError
// on the next sync; the server gives up after three.

#ifdef CLOUD_OTA_ENABLED
extern bool cloudOtaPowerOk();   // task_manager.h: false on a nearly flat battery

#define CLOUD_OTA_CHUNK 4096

static void cloudOtaFail(const char* why) {
  strlcpy(cloudOtaError, why, sizeof(cloudOtaError));
  Preferences prefs;
  if (prefs.begin(CLOUD_NVS_NAMESPACE, false)) {
    prefs.putString("otaErr", cloudOtaError);
    prefs.end();
  }
  DBG("Cloud OTA: failed: ");
  DBGLN(why);
}

static void cloudOtaUpdate() {
  const CloudOtaOffer& o = cloudOtaOffer;
  unsigned long now = millis();

  // Reasons to leave it for a later sync. None of these is a failure.
  if (!cloudToken[0]) return;                                            // the download needs the token
  if (strcmp(o.version, FIRMWARE_VERSION) == 0) return;                  // already running it
  if (cloudOtaHold && (int32_t)(now - cloudOtaHoldUntilMs) < 0) return;  // one attempt an hour
  if (otaTrialPending || otaUploadActive() || Update.isRunning()) return;   // a web panel upload owns the slot
  if (ackCount > 0) return;                                              // let this sync's command acks out first
  if (!cloudOtaPowerOk()) return;

  cloudOtaHold = true;
  cloudOtaHoldUntilMs = now + CLOUD_OTA_RETRY_MS;
  DBG("Cloud OTA: ");
  DBG(o.version);
  DBG(" build ");
  DBGLN(o.buildId);

  if (!o.buildId[0] || o.url[0] != '/' || strlen(o.sha256) != 64 || o.size == 0) {
    cloudOtaFail("bad firmware offer");
    return;
  }
  const esp_partition_t* slot = esp_ota_get_next_update_partition(NULL);
  if (!slot || slot == esp_ota_get_running_partition()) {
    cloudOtaFail("no spare app slot");
    return;
  }
  if (o.size > slot->size) {
    cloudOtaFail("image larger than the app slot");
    return;
  }
  Preferences prefs;
  if (prefs.begin(CLOUD_NVS_NAMESPACE, true)) {
    bool installed = prefs.getString("otaDone", "") == o.buildId;
    prefs.end();
    if (installed) {   // or it would install the same build every hour
      cloudOtaFail("build already installed, version differs");
      return;
    }
  }

  // A second TLS session plus the flash writer's buffers; boards without PSRAM
  // take all of it from internal RAM
  if (heap_caps_get_largest_free_block(psramFound() ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL) < 40960) {
    cloudOtaFail("not enough free memory");
    return;
  }

  char url[CLOUD_SERVER_URL_MAX + sizeof(o.url)];
  snprintf(url, sizeof(url), "%s%s", cloudServerUrl, o.url);   // a path on our own server, never another host
  esp_http_client_handle_t client = cloudHttpInit(url, HTTP_METHOD_GET);
  uint8_t* buf = (uint8_t*)malloc(CLOUD_OTA_CHUNK);
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  char why[64] = "";
  bool writing = false;
  uint32_t total = 0;

  do {
    if (!client || !buf) {
      strlcpy(why, "out of memory", sizeof(why));
      break;
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
      snprintf(why, sizeof(why), "connect: %s", esp_err_to_name(err));
      break;
    }
    int contentLength = esp_http_client_fetch_headers(client);
    int code = esp_http_client_get_status_code(client);
    if (code != 200) {
      snprintf(why, sizeof(why), "download HTTP %d", code);
      break;
    }
    if (contentLength > 0 && (uint32_t)contentLength != o.size) {
      snprintf(why, sizeof(why), "Content-Length %d, expected %u", contentLength, (unsigned)o.size);
      break;
    }
    if (!Update.begin(o.size, U_FLASH)) {
      snprintf(why, sizeof(why), "begin: %s", Update.errorString());
      break;
    }
    writing = true;

    int n;
    while ((n = esp_http_client_read(client, (char*)buf, CLOUD_OTA_CHUNK)) > 0) {
      if (total == 0 && buf[0] != 0xE9) {   // same check as the web panel upload
        strlcpy(why, "not a firmware image", sizeof(why));
        break;
      }
      if (total + n > o.size) {
        strlcpy(why, "download longer than size", sizeof(why));
        break;
      }
      mbedtls_sha256_update(&sha, buf, n);
      if (Update.write(buf, n) != (size_t)n) {
        snprintf(why, sizeof(why), "write: %s", Update.errorString());
        break;
      }
      total += n;
      vTaskDelay(1);   // flash writes back to back would starve IDLE0 and its watchdog
    }
    if (why[0]) break;
    if (total != o.size) {
      snprintf(why, sizeof(why), "got %u of %u bytes", (unsigned)total, (unsigned)o.size);
      break;
    }
    uint8_t digest[32];
    char hex[65];
    mbedtls_sha256_finish(&sha, digest);
    for (uint8_t i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    if (strcasecmp(hex, o.sha256) != 0) strlcpy(why, "sha256 mismatch", sizeof(why));
  } while (0);

  mbedtls_sha256_free(&sha);
  if (client) {
    esp_http_client_close(client);   // TLS gone before Update.end() hashes the image (see ota_update.h)
    esp_http_client_cleanup(client);
  }
  free(buf);

  if (!why[0]) {
    // Mark the trial before the boot slot changes, so the new firmware cannot
    // come up without knowing it is on trial.
    if (prefs.begin(CLOUD_NVS_NAMESPACE, false)) {
      prefs.putString("otaPend", o.buildId);
      prefs.putString("otaSlot", slot->label);
      prefs.end();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!Update.end()) {   // verifies the image on flash, then switches the boot slot
      snprintf(why, sizeof(why), "verify: %s", Update.errorString());
      writing = false;
      if (prefs.begin(CLOUD_NVS_NAMESPACE, false)) {
        prefs.remove("otaPend");
        prefs.remove("otaSlot");
        prefs.end();
      }
    }
  }
  if (why[0]) {
    if (writing && !otaUploadActive()) Update.abort();   // unless a web panel upload has since taken over
    cloudOtaFail(why);
    return;
  }

  DBGLN("Cloud OTA: installed, restarting");
  cmdSayText("Updated! Restarting...", 3000);
  delay(2500);
  ESP.restart();
}
#endif // CLOUD_OTA_ENABLED

// ============================================================================
// Registration
// ============================================================================

bool cloudRegister() {
  cloudState = CLOUD_REGISTERING;
  DBGLN("Cloud: registering...");

  String body = buildRegistrationBody();
  String response;
  char url[CLOUD_SERVER_URL_MAX + 32];
  snprintf(url, sizeof(url), "%s/api/bots/register", cloudServerUrl);

  int code = cloudPost(url, body, response);

  #ifdef BOARD_HAS_STACKCHAN_BASE
  if (code == 400 && !cloudHwTypeLegacy) {
    // A v1 server validates hardwareType and has never heard of "stackchan".
    // Register as the CoreS3 it used to be; a v2 server never gets here.
    DBGLN("Cloud: server refused the stackchan type, registering as m5stack_cores3");
    cloudHwTypeLegacy = true;
    body = buildRegistrationBody();
    code = cloudPost(url, body, response);
  }
  #endif
  cloudLastHttpCode = code;

  if (code == 200) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, response);
    const char* botId = doc["botId"] | "";
    if (err || !botId[0] || strlen(botId) >= sizeof(cloudMeta.botId)) {
      DBG("Cloud: register JSON error: ");
      DBGLN(err ? err.c_str() : "no botId");
      cloudLastHttpCode = 0;   // answered, but not usably
      cloudState = CLOUD_OFFLINE;
      return false;
    }

    // The id comes from the server. A bot that already had one sent it, and
    // gets it back. Its cached content is rewritten only if the version moved.
    uint32_t version = doc["contentVersion"] | 0;
    bool sameContent = strcmp(botId, cloudMeta.botId) == 0 && version == cloudMeta.contentVersion;
    strncpy(cloudMeta.botId, botId, sizeof(cloudMeta.botId) - 1);
    cloudMeta.botId[sizeof(cloudMeta.botId) - 1] = '\0';
    cloudMeta.contentVersion = version;
    cloudMeta.pollIntervalSec = doc["pollInterval"] | CLOUD_POLL_DEFAULT;
    if (cloudCfgPollSec) cloudMeta.pollIntervalSec = cloudCfgPollSec;   // the admin's setting stands
    cloudMeta.registered = true;

    // v2: this bot's token. It is only ever sent in a register response.
    const char* token = doc["botToken"] | "";
    if (token[0] && strlen(token) < sizeof(cloudToken)) {
      strlcpy(cloudToken, token, sizeof(cloudToken));
      cloudNoTokenServer = false;
    } else {
      cloudNoTokenServer = true;   // a v1 server: carry on as a v1 bot
    }
    const char* approval = doc["approval"] | "";
    strlcpy(cloudApproval, approval, sizeof(cloudApproval));

    // Persist to NVS
    saveCloudNVS();

    // Cache content
    if (!doc["content"].isNull() && !sameContent) {
      JsonObject content = doc["content"];
      processContent(content);
    }

    // Save meta to LittleFS
    if (sysStatus.littlefsReady) {
      saveCloudMeta(cloudMeta);
    }

    cloudState = CLOUD_REGISTERED;
    cloudBackoffSec = 0;
    DBG("Cloud: registered as ");
    DBG(cloudMeta.botId);
    DBGLN(cloudToken[0] ? " (token)" : " (no token: v1 server)");
    return true;

  } else if (code == 401) {
    // Never the end of cloud calls: wait, then register again (pollCloudSync)
    DBGLN("Cloud: 401 on register, retry in 1 h");
    cloudAuthRetryAtMs = millis() + CLOUD_AUTH_BACKOFF_MS;
    cloudState = CLOUD_ERROR_AUTH;
    return false;

  } else {
    DBG("Cloud: register failed HTTP ");
    DBG(code);
    DBG(" (");
    DBG(response);  // error string when code < 0
    DBGLN(")");
    cloudState = CLOUD_OFFLINE;
    return false;
  }
}

// ============================================================================
// Sync
// ============================================================================

bool cloudSync() {
  cloudState = CLOUD_SYNCING;

  String body = buildSyncBody();
  String response;
  bool sentResetReason = !cloudBootReported;
  bool sentOtaError = cloudOtaError[0] != '\0';
  char url[CLOUD_SERVER_URL_MAX + 64];
  snprintf(url, sizeof(url), "%s/api/bots/%s/sync", cloudServerUrl, cloudMeta.botId);

  int code = cloudPost(url, body, response);
  cloudLastHttpCode = code;

  if (code == 200) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, response);
    if (err) {
      DBG("Cloud: sync JSON error: ");
      DBGLN(err.c_str());
      cloudState = CLOUD_REGISTERED;
      return false;
    }

    cloudLastSyncOkMs = millis();
    cloudEverSynced = true;
    #ifdef CLOUD_OTA_ENABLED
    if (otaTrialPending) {
      // A cloud update on trial has now reached the cloud: keep it
      otaConfirmTrial();
      cloudOtaHold = true;
      cloudOtaHoldUntilMs = millis() + CLOUD_OTA_RETRY_MS;
    }
    #endif

    // v2: approval. A bot waiting for approval, or switched off in the admin,
    // gets an empty answer; apply none of it and keep polling as usual. A v1
    // server sends no approval, which reads as approved.
    const char* approval = doc["approval"] | "";
    strlcpy(cloudApproval, approval, sizeof(cloudApproval));
    if (strcmp(approval, "pending") == 0 || strcmp(approval, "disabled") == 0) {
      cloudState = CLOUD_REGISTERED;
      cloudBackoffSec = 0;
      return true;
    }

    // The server has what this request carried once only
    if (sentResetReason) cloudBootReported = true;
    if (sentOtaError) {
      cloudOtaError[0] = '\0';
      Preferences prefs;
      if (prefs.begin(CLOUD_NVS_NAMESPACE, false)) {
        prefs.remove("otaErr");
        prefs.end();
      }
    }

    // Update content version
    cloudMeta.contentVersion = doc["contentVersion"] | cloudMeta.contentVersion;

    // Process commands
    clearAcks();
    JsonArray commands = doc["commands"];
    if (!commands.isNull()) {
      for (JsonObject cmd : commands) {
        const char* id = cmd["id"] | "";
        const char* type = cmd["type"] | "";
        JsonObject payload = cmd["payload"];
        const char* execAt = cmd["execute_at"] | "";

        // Check if this is a scheduled command
        bool scheduled = false;
        if (strlen(execAt) > 0 && sysStatus.ntpSynced) {
          time_t execTime = parseISO8601(execAt);
          time_t now_t = time(NULL);
          if (execTime > now_t + 2) {
            // Schedule for later — find a free slot
            for (uint8_t s = 0; s < SCHED_CMD_SLOTS; s++) {
              if (!scheduledCmds[s].occupied) {
                strncpy(scheduledCmds[s].id, id, sizeof(scheduledCmds[s].id) - 1);
                strncpy(scheduledCmds[s].type, type, sizeof(scheduledCmds[s].type) - 1);
                // Serialize payload into buffer
                String payStr;
                serializeJson(payload, payStr);
                uint16_t len = min((uint16_t)payStr.length(), (uint16_t)(sizeof(scheduledCmds[s].payloadBuf) - 1));
                memcpy(scheduledCmds[s].payloadBuf, payStr.c_str(), len);
                scheduledCmds[s].payloadBuf[len] = '\0';
                scheduledCmds[s].payloadLen = len;
                scheduledCmds[s].executeAt = execTime;
                scheduledCmds[s].occupied = true;
                scheduled = true;
                DBG("Cloud: scheduled cmd type=");
                DBG(type);
                DBG(" at T+");
                DBGLN(execTime - now_t);
                break;
              }
            }
          }
        }

        if (!scheduled && strlen(type) > 0) {
          dispatchCloudCommand(type, payload);
        }
        // Always ack so server doesn't re-send
        if (strlen(id) > 0) {
          pushAck(id);
        }
      }
    }

    // Parse groups from sync response
    JsonArray groupsArr = doc["groups"];
    cloudMeta.groupCount = 0;
    if (!groupsArr.isNull()) {
      for (JsonObject g : groupsArr) {
        if (cloudMeta.groupCount >= MAX_CLOUD_GROUPS) break;
        CloudGroupInfo& gi = cloudMeta.groups[cloudMeta.groupCount];
        strncpy(gi.id, g["id"] | "", sizeof(gi.id) - 1);
        gi.id[sizeof(gi.id) - 1] = '\0';
        strncpy(gi.name, g["name"] | "", sizeof(gi.name) - 1);
        gi.name[sizeof(gi.name) - 1] = '\0';
        strncpy(gi.syncMode, g["syncMode"] | "independent", sizeof(gi.syncMode) - 1);
        gi.syncMode[sizeof(gi.syncMode) - 1] = '\0';
        strncpy(gi.wledIp, g["wledIp"] | "", sizeof(gi.wledIp) - 1);
        gi.wledIp[sizeof(gi.wledIp) - 1] = '\0';
        gi.wledOwner = g["wledOwner"] | false;
        strncpy(gi.wledStreamMode, g["wledStreamMode"] | "emoji", sizeof(gi.wledStreamMode) - 1);
        gi.wledStreamMode[sizeof(gi.wledStreamMode) - 1] = '\0';
        cloudMeta.groupCount++;
      }
    }

    // Update WLED stream permission from group data
    extern bool wledStreamAllowed;
    wledStreamAllowed = true;  // default: allowed
    for (uint8_t i = 0; i < cloudMeta.groupCount; i++) {
      if (strlen(cloudMeta.groups[i].wledIp) > 0) {
        wledStreamAllowed = cloudMeta.groups[i].wledOwner;
        break;
      }
    }

    // Parse fleet info
    JsonObject fleet = doc["fleet"];
    if (!fleet.isNull()) {
      cloudMeta.fleetTotal = fleet["totalBots"] | 0;
      cloudMeta.fleetOnline = fleet["onlineBots"] | 0;
    }

    // Process content update
    if (!doc["content"].isNull()) {
      JsonObject content = doc["content"];
      processContent(content);
      saveCloudNVS();
      DBGLN("Cloud: content updated");
    }

    // v2: settings changed in the admin. Sent only when its rev differs from
    // the configRev in the request; a v1 server always sends null.
    JsonObject cfg = doc["config"];
    if (!cfg.isNull() && cfg["rev"].is<int>()) applyCloudConfig(cfg);
    if (cloudCfgPersonalityDue) applyCloudCfgPersonality();

    // v2: an assigned firmware update, taken up by pollCloudSync() once this
    // response has been let go of
    #ifdef CLOUD_OTA_ENABLED
    JsonObject fw = doc["firmware"];
    if (!fw.isNull()) {
      CloudOtaOffer& o = cloudOtaOffer;
      strlcpy(o.buildId, fw["buildId"].as<String>().c_str(), sizeof(o.buildId));   // string or number
      strlcpy(o.version, fw["version"] | "", sizeof(o.version));
      strlcpy(o.url, fw["url"] | "", sizeof(o.url));
      strlcpy(o.sha256, fw["sha256"] | "", sizeof(o.sha256));
      o.size = fw["size"] | 0;
      o.present = !fw["buildId"].isNull();
    }
    #endif

    cloudState = CLOUD_REGISTERED;
    cloudBackoffSec = 0;
    return true;

  } else if (code == 401) {
    // Never the end of cloud calls: wait, then drop the token and register
    // again (pollCloudSync). The server then holds the new token until someone
    // approves the re-pair.
    DBGLN("Cloud: 401 on sync, re-registering in 1 h");
    cloudAuthRetryAtMs = millis() + CLOUD_AUTH_BACKOFF_MS;
    cloudState = CLOUD_ERROR_AUTH;
    return false;

  } else if (code == 429) {
    // Rate limited — double poll interval temporarily (as v1 firmware; but an
    // interval the admin set above 60 s is not shortened)
    if (!cloudCfgPollSec || cloudMeta.pollIntervalSec < 60) {
      cloudMeta.pollIntervalSec = min((uint16_t)(cloudMeta.pollIntervalSec * 2), (uint16_t)60);
    }
    DBG("Cloud: 429 — poll interval now ");
    DBGLN(cloudMeta.pollIntervalSec);
    cloudState = CLOUD_REGISTERED;
    return false;

  } else {
    DBG("Cloud: sync failed HTTP ");
    DBG(code);
    DBG(" (");
    DBG(response);  // error string when code < 0
    DBGLN(")");
    // Network errors (DNS fail, connect timeout) get short retry — transient issues.
    // Server errors (5xx) get exponential backoff — server might be down.
    if (code < 0) {
      // Network/DNS/connect error — retry in 30s, no escalation
      cloudBackoffSec = 30;
    } else {
      // Server error — exponential backoff
      if (cloudBackoffSec == 0) cloudBackoffSec = 10;
      else cloudBackoffSec = min(cloudBackoffSec * 2, (uint32_t)300);
    }
    cloudState = CLOUD_REGISTERED;
    return false;
  }
}

// ============================================================================
// Scheduled Command Polling — called from WiFi task loop
// ============================================================================

void pollScheduledCommands() {
  if (!sysStatus.ntpSynced) return;
  time_t now_t = time(NULL);
  for (uint8_t i = 0; i < SCHED_CMD_SLOTS; i++) {
    if (!scheduledCmds[i].occupied) continue;
    if (now_t >= scheduledCmds[i].executeAt) {
      // Time to execute — deserialize payload and dispatch
      JsonDocument payDoc;
      deserializeJson(payDoc, (const char*)scheduledCmds[i].payloadBuf);
      JsonObject payload = payDoc.as<JsonObject>();
      DBG("Cloud: executing scheduled cmd type=");
      DBGLN(scheduledCmds[i].type);
      dispatchCloudCommand(scheduledCmds[i].type, payload);
      scheduledCmds[i].occupied = false;
    }
  }
}

// ============================================================================
// Cloud Init (called from boot, before task starts)
// ============================================================================

// mbedTLS is built to allocate in internal RAM (CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC),
// so each sync's handshake took ~30 KB of it and left WiFi, lwIP and the web
// server ~3 KB to share (seen on /debug/tasks, 2026-11-06). Prefer PSRAM, as
// IDF's CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC would; boards without it fall back.
static void* cloudTlsCalloc(size_t n, size_t size) {
  void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void initCloudClient() {
  mbedtls_platform_set_calloc_free(cloudTlsCalloc, heap_caps_free);

  // Load from NVS first (survives reflash)
  loadCloudNVS();

  // Also try LittleFS meta (has content version + poll interval)
  if (sysStatus.littlefsReady) {
    CloudMeta fsMeta;
    if (loadCloudMeta(fsMeta)) {
      // NVS botId takes precedence, but use FS content version if newer
      if (fsMeta.contentVersion > cloudMeta.contentVersion) {
        cloudMeta.contentVersion = fsMeta.contentVersion;
      }
      if (strlen(cloudMeta.botId) == 0 && strlen(fsMeta.botId) > 0) {
        strncpy(cloudMeta.botId, fsMeta.botId, sizeof(cloudMeta.botId) - 1);
        cloudMeta.registered = true;
      }
    }
  }

  // Boot delay — let WiFi stack settle before first cloud poll
  cloudNextPollMs = millis() + 2000;

  // The last update failed (ota_update.h found it rolled back, or the download
  // did): no second attempt for an hour, however often the bot restarts into it
  if (cloudOtaError[0]) {
    cloudOtaHold = true;
    cloudOtaHoldUntilMs = millis() + CLOUD_OTA_RETRY_MS;
  }

  DBG("Cloud init: server=");
  DBG(cloudServerUrl);
  DBG(cloudToken[0] ? " token" : " no-token");
  DBG(" botId=");
  DBG(cloudMeta.botId);
  DBG(" ver=");
  DBG(cloudMeta.contentVersion);
  DBG(" poll=");
  DBGLN(cloudMeta.pollIntervalSec);
}

// ============================================================================
// Cloud Poll — called from wifiServerTask loop (Core 0), non-blocking
// ============================================================================

// The "Cloud server" setting changed: switch identity to the one kept for the
// new server. Runs in the cloud task, between requests.
static void cloudApplyServerChange() {
  cloudServerChanged = false;   // first, so a change made while this runs is not lost
  Preferences prefs;
  if (prefs.begin(CLOUD_NVS_NAMESPACE, false)) {
    if (prefs.getString("server", "").length() > 0) {
      // Pair afresh with a custom server; whatever identity an earlier one
      // issued means nothing to this one. The production identity is kept.
      static const char* const keys[] = {"t_botId", "t_pollInt", "t_contVer", "t_token", "t_cfgRev", "t_cfgPoll", "t_cfgPers"};
      for (const char* k : keys) prefs.remove(k);
    }
    prefs.remove("srvDirty");
    prefs.end();
  }
  loadCloudNVS();

  cloudMeta.contentVersion = 0;   // the cached content came from the other server: fetch this one's
  cloudMeta.groupCount = 0;
  cloudMeta.fleetTotal = 0;
  cloudMeta.fleetOnline = 0;
  saveCloudNVS();
  if (sysStatus.littlefsReady) saveCloudMeta(cloudMeta);
  sysStatus.cloudRegistered = cloudMeta.registered;

  clearAcks();
  cloudApproval[0] = '\0';
  cloudNoTokenServer = false;
  cloudTokenRetry = false;
  cloudHwTypeLegacy = false;
  cloudEverSynced = false;
  cloudBackoffSec = 0;
  cloudState = CLOUD_IDLE;
  cloudNextPollMs = millis();
  lastSyncAttempt = millis() - 400000UL;   // sync straight after registering
  DBG("Cloud: server is now ");
  DBGLN(cloudServerUrl);
}

// Bulk UDP senders hold off for each request and for parsing and storing its
// answer (system_status.h). The short wait lets packets already queued go first.
static void cloudNetHoldBegin() {
  cloudNetHoldMs = millis() | 1;   // 0 means no hold
  vTaskDelay(pdMS_TO_TICKS(50));
}

static void cloudNetHoldEnd() {
  cloudNetHoldMs = 0;
}

void pollCloudSync() {
  if (cloudServerChanged) cloudApplyServerChange();
  if (!sysStatus.staConnected) return;

  unsigned long now = millis();

  // After a 401: nothing for an hour, then forget the token and register again.
  // Cloud calls never stop for good.
  if (cloudState == CLOUD_ERROR_AUTH) {
    if ((int32_t)(now - cloudAuthRetryAtMs) < 0) return;
    DBGLN("Cloud: auth back-off over, registering again");
    cloudToken[0] = '\0';
    cloudNoTokenServer = false;
    cloudTokenRetry = false;
    saveCloudNVS();
    cloudState = CLOUD_IDLE;
  }
  if (now < cloudNextPollMs) return;

  if (cloudTokenRetry && (int32_t)(now - cloudTokenRetryAtMs) >= 0) {
    cloudTokenRetry = false;
    cloudNoTokenServer = false;
  }

  // Register if needed: no id yet (first boot, wipe), or an id but no token (a
  // bot upgraded from v1 firmware). A v1 server issues no token; that is asked
  // once per boot and the bot then syncs as v1 firmware does.
  bool needId = !cloudMeta.registered || strlen(cloudMeta.botId) == 0;
  if (needId || (!cloudToken[0] && !cloudNoTokenServer)) {
    static uint8_t regFails = 0;
    cloudNetHoldBegin();
    cloudRegister();
    cloudNetHoldEnd();
    if (cloudState == CLOUD_ERROR_AUTH) return;
    if (cloudState != CLOUD_REGISTERED) {
      if (regFails < 5) regFails++;
      if (needId || (cloudLastHttpCode < 0 && regFails <= 3)) {
        // Progressive backoff so a persistently failing handshake (e.g. TLS
        // -0x0050) doesn't retry on a tight loop: 10s, 20s, 40s … capped at 5min.
        uint32_t wait = (cloudBackoffSec > 0)
          ? cloudBackoffSec
          : min((uint32_t)(10u << (regFails - 1)), (uint32_t)300);
        cloudNextPollMs = now + wait * 1000;
        return;
      }
      // No token to be had right now, but the bot has an id from before: sync
      // with it as v1 firmware does, and ask for a token again in an hour.
      cloudNoTokenServer = true;
      cloudTokenRetry = true;
      cloudTokenRetryAtMs = now + CLOUD_AUTH_BACKOFF_MS;
      cloudState = CLOUD_REGISTERED;
    }
    regFails = 0;  // registered — reset backoff
  }

  // Sync at poll interval
  uint32_t interval = (cloudBackoffSec > 0)
    ? cloudBackoffSec
    : (uint32_t)cloudMeta.pollIntervalSec;

  if (now - lastSyncAttempt >= interval * 1000) {
    lastSyncAttempt = now;

    // TLS buffers live in PSRAM when there is some (cloudTlsCalloc), so that's the heap to check
    uint32_t tlsCaps = psramFound() ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
    size_t largest = heap_caps_get_largest_free_block(tlsCaps);
    size_t freeHeap = heap_caps_get_free_size(tlsCaps);
    DBG("Cloud: sync, largest block=");
    DBG(largest);
    DBG(" free=");
    DBGLN(freeHeap);

    DBG("Cloud: stack HWM=");
    DBGLN(uxTaskGetStackHighWaterMark(NULL));

    // Heap guard: TLS needs ~24KB contiguous (16.7KB in_buf + 4.4KB out_buf + 1.5KB HTTP + 2KB overhead)
    // With reduced HTTP buffers (1024+512 vs 2048+1024), threshold lowered from 32768.
    // Sockets, pbufs and the HTTP buffers still come from internal RAM (they're
    // under the 4 KB always-internal limit), so with TLS in PSRAM keep ~8 KB there too.
    size_t internalBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (largest < 28672 || internalBlock < 8192) {
      DBG("Cloud: skip — heap too fragmented (need 28672, have ");
      DBG(largest);
      DBG("; internal ");
      DBG(internalBlock);
      DBGLN(")");
      cloudNextPollMs = now + 10000;  // retry in 10s
      return;
    }

    cloudNetHoldBegin();
    cloudSync();

    #ifdef CLOUD_OTA_ENABLED
    if (cloudOtaOffer.present) {
      cloudOtaOffer.present = false;
      cloudOtaUpdate();
    }
    #endif
    cloudNetHoldEnd();
  }

  cloudNextPollMs = now + 1000;  // check again in 1s
}

// ============================================================================
// Status accessors (for web server /state endpoint)
// ============================================================================

const char* getCloudStateStr() {
  switch (cloudState) {
    case CLOUD_IDLE:        return "idle";
    case CLOUD_REGISTERING: return "registering";
    case CLOUD_REGISTERED:  return "registered";
    case CLOUD_SYNCING:     return "syncing";
    case CLOUD_ERROR_AUTH:  return "auth_error";
    case CLOUD_OFFLINE:     return "offline";
    default:                return "unknown";
  }
}

// More fields for the "cloud" object in /state and /cloud/status: which server,
// where approval stands, and how long ago a sync last worked (-1 = not yet).
String getCloudStatusExtraJson() {
  String json = ",\"server\":\"" + String(cloudServerUrl) + "\"";
  json += ",\"customServer\":\"" + String(cloudCustomServer) + "\"";
  json += ",\"protocol\":" + String(CLOUD_PROTOCOL_VERSION);
  json += ",\"hasToken\":" + String(cloudToken[0] ? "true" : "false");
  const char* approval = cloudApproval;
  bool known = !strcmp(approval, "approved") || !strcmp(approval, "pending") || !strcmp(approval, "disabled");
  json += ",\"approval\":\"" + String(known ? approval : "") + "\"";
  json += ",\"lastSyncSec\":" + String(cloudEverSynced ? (long)((millis() - cloudLastSyncOkMs) / 1000) : -1L);
  json += ",\"configRev\":" + String(cloudConfigRev);
  if (cloudState == CLOUD_ERROR_AUTH) {
    long left = (long)(cloudAuthRetryAtMs - millis()) / 1000;
    json += ",\"retrySec\":" + String(left > 0 ? left : 0L);
  }
  return json;
}

#endif // CLOUD_ENABLED
#endif // CLOUD_CLIENT_H
