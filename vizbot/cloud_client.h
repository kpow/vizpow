#ifndef CLOUD_CLIENT_H
#define CLOUD_CLIENT_H

#ifdef CLOUD_ENABLED

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "esp_http_client.h"
#include "config.h"
#include "system_status.h"
#include "content_cache.h"

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
// Runs cooperatively inside wifiServerTask on Core 0 via pollCloudSync().
// Handles bot registration, periodic sync, command dispatch, and content updates.
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
  #if defined(TARGET_LCD)
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
// NVS Persistence for botId
// ============================================================================

static void loadCloudNVS() {
  Preferences prefs;
  if (prefs.begin(CLOUD_NVS_NAMESPACE, true)) {
    String id = prefs.getString("botId", "");
    if (id.length() > 0 && id.length() < sizeof(cloudMeta.botId)) {
      strncpy(cloudMeta.botId, id.c_str(), sizeof(cloudMeta.botId) - 1);
      cloudMeta.botId[sizeof(cloudMeta.botId) - 1] = '\0';
      cloudMeta.registered = true;
    }
    cloudMeta.pollIntervalSec = max((uint16_t)CLOUD_POLL_DEFAULT, prefs.getUShort("pollInt", CLOUD_POLL_DEFAULT));
    cloudMeta.contentVersion = prefs.getUInt("contVer", 0);
    prefs.end();
  }
}

static void saveCloudNVS() {
  Preferences prefs;
  if (prefs.begin(CLOUD_NVS_NAMESPACE, false)) {
    prefs.putString("botId", cloudMeta.botId);
    prefs.putUShort("pollInt", cloudMeta.pollIntervalSec);
    prefs.putUInt("contVer", cloudMeta.contentVersion);
    prefs.end();
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

  // NTP time (ISO-8601 UTC)
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 0)) {
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
static int cloudPost(const char* url, const String& body, String& response) {
  esp_http_client_config_t config = {};
  config.url = url;
  config.method = HTTP_METHOD_POST;
  config.buffer_size = 1024;      // was 2048 — cloud responses are 63-500B, saves 1KB during TLS
  config.buffer_size_tx = 512;    // was 1024 — request headers + body fit in 512B, saves 512B
  config.cert_pem = gts_root_r4_pem;
  config.timeout_ms = CLOUD_RESPONSE_TIMEOUT;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    response = "init failed";
    return -1;
  }

  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "X-Bot-Secret", CLOUD_BOT_SECRET);

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
      while ((read_len = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        tmp.write((uint8_t*)buf, read_len);
        totalRead += read_len;
        if (totalRead > 16384) break;
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
// Registration
// ============================================================================

bool cloudRegister() {
  cloudState = CLOUD_REGISTERING;
  DBGLN("Cloud: registering...");

  String body = buildRegistrationBody();
  String response;
  char url[128];
  snprintf(url, sizeof(url), "%s/api/bots/register", CLOUD_SERVER_URL);

  int code = cloudPost(url, body, response);

  if (code == 200) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, response);
    if (err) {
      DBG("Cloud: register JSON error: ");
      DBGLN(err.c_str());
      cloudState = CLOUD_OFFLINE;
      return false;
    }

    // Store botId
    const char* botId = doc["botId"] | "";
    strncpy(cloudMeta.botId, botId, sizeof(cloudMeta.botId) - 1);
    cloudMeta.botId[sizeof(cloudMeta.botId) - 1] = '\0';
    cloudMeta.contentVersion = doc["contentVersion"] | 0;
    cloudMeta.pollIntervalSec = doc["pollInterval"] | CLOUD_POLL_DEFAULT;
    cloudMeta.registered = true;

    // Persist to NVS
    saveCloudNVS();

    // Cache content
    if (!doc["content"].isNull()) {
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
    DBGLN(cloudMeta.botId);
    return true;

  } else if (code == 401) {
    DBGLN("Cloud: 401 — bad secret, stopping");
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
  char url[128];
  snprintf(url, sizeof(url), "%s/api/bots/%s/sync", CLOUD_SERVER_URL, cloudMeta.botId);

  int code = cloudPost(url, body, response);

  if (code == 200) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, response);
    if (err) {
      DBG("Cloud: sync JSON error: ");
      DBGLN(err.c_str());
      cloudState = CLOUD_REGISTERED;
      return false;
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

    cloudState = CLOUD_REGISTERED;
    cloudBackoffSec = 0;
    return true;

  } else if (code == 401) {
    DBGLN("Cloud: 401 on sync — bad secret");
    cloudState = CLOUD_ERROR_AUTH;
    return false;

  } else if (code == 429) {
    // Rate limited — double poll interval temporarily
    cloudMeta.pollIntervalSec = min((uint16_t)(cloudMeta.pollIntervalSec * 2), (uint16_t)60);
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

void initCloudClient() {
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

  DBG("Cloud init: botId=");
  DBG(cloudMeta.botId);
  DBG(" ver=");
  DBG(cloudMeta.contentVersion);
  DBG(" poll=");
  DBGLN(cloudMeta.pollIntervalSec);
}

// ============================================================================
// Cloud Poll — called from wifiServerTask loop (Core 0), non-blocking
// ============================================================================

void pollCloudSync() {
  if (cloudState == CLOUD_ERROR_AUTH) return;
  if (!sysStatus.staConnected) return;

  unsigned long now = millis();
  if (now < cloudNextPollMs) return;

  // Register if needed
  if (!cloudMeta.registered || strlen(cloudMeta.botId) == 0) {
    cloudRegister();
    if (!cloudMeta.registered) {
      uint32_t wait = (cloudBackoffSec > 0) ? cloudBackoffSec : 10;
      cloudNextPollMs = now + wait * 1000;
      return;
    }
  }

  // Sync at poll interval
  uint32_t interval = (cloudBackoffSec > 0)
    ? cloudBackoffSec
    : (uint32_t)cloudMeta.pollIntervalSec;

  if (now - lastSyncAttempt >= interval * 1000) {
    lastSyncAttempt = now;

    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    DBG("Cloud: sync, largest block=");
    DBG(largest);
    DBG(" free=");
    DBGLN(freeHeap);

    DBG("Cloud: stack HWM=");
    DBGLN(uxTaskGetStackHighWaterMark(NULL));

    // Heap guard: TLS needs ~24KB contiguous (16.7KB in_buf + 4.4KB out_buf + 1.5KB HTTP + 2KB overhead)
    // With reduced HTTP buffers (1024+512 vs 2048+1024), threshold lowered from 32768.
    if (largest < 28672) {
      DBG("Cloud: skip — heap too fragmented (need 28672, have ");
      DBG(largest);
      DBGLN(")");
      cloudNextPollMs = now + 10000;  // retry in 10s
      return;
    }

    cloudSync();
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

#endif // CLOUD_ENABLED
#endif // CLOUD_CLIENT_H
