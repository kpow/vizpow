#ifndef CONTENT_CACHE_H
#define CONTENT_CACHE_H

#ifdef CLOUD_ENABLED

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "bot_sayings.h"

// ============================================================================
// Content Cache — LittleFS-based cloud content storage
// ============================================================================
// Manages cached sayings and personalities from vizCloud.
// Files are stored under /cloud/ on LittleFS.
//
// Layout:
//   /cloud/meta.json          — botId, contentVersion, pollInterval
//   /cloud/sayings.json       — full sayings array from server
//   /cloud/personalities.json — full personalities array from server
// ============================================================================

// Thread safety: set true while cloud task writes, checked by getCloudSaying()
volatile bool contentUpdateInProgress = false;

struct CloudGroupInfo {
  char id[40];
  char name[32];
  char syncMode[16];
  char wledIp[16];        // shared WLED IP for this group
  bool wledOwner;         // am I the stream owner?
  char wledStreamMode[8]; // "emoji", "weather", "toggle"
};

#define MAX_CLOUD_GROUPS 4

struct CloudMeta {
  char botId[40];
  uint32_t contentVersion;
  uint16_t pollIntervalSec;
  bool registered;
  CloudGroupInfo groups[MAX_CLOUD_GROUPS];
  uint8_t groupCount;
  uint16_t fleetTotal;
  uint16_t fleetOnline;
};

CloudMeta cloudMeta = {"", 0, CLOUD_POLL_DEFAULT, false, {}, 0, 0, 0};

// ============================================================================
// Filesystem Init
// ============================================================================

bool initContentCache() {
  if (!LittleFS.begin(true)) {  // true = format on first use
    DBGLN("LittleFS: mount failed even after format");
    return false;
  }

  // Ensure /cloud directory exists
  if (!LittleFS.exists("/cloud")) {
    LittleFS.mkdir("/cloud");
  }

  DBGLN("LittleFS: mounted OK");
  return true;
}

// ============================================================================
// CloudMeta Read/Write
// ============================================================================

bool loadCloudMeta(CloudMeta& meta) {
  File f = LittleFS.open("/cloud/meta.json", "r");
  if (!f) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    DBG("CloudMeta parse error: ");
    DBGLN(err.c_str());
    return false;
  }

  const char* id = doc["botId"] | "";
  strncpy(meta.botId, id, sizeof(meta.botId) - 1);
  meta.botId[sizeof(meta.botId) - 1] = '\0';
  meta.contentVersion = doc["contentVersion"] | 0;
  meta.pollIntervalSec = doc["pollInterval"] | CLOUD_POLL_DEFAULT;
  meta.registered = (strlen(meta.botId) > 0);

  return true;
}

bool saveCloudMeta(const CloudMeta& meta) {
  File f = LittleFS.open("/cloud/meta.json", "w");
  if (!f) {
    DBGLN("CloudMeta: failed to open for write");
    return false;
  }

  JsonDocument doc;
  doc["botId"] = meta.botId;
  doc["contentVersion"] = meta.contentVersion;
  doc["pollInterval"] = meta.pollIntervalSec;

  serializeJson(doc, f);
  f.close();
  return true;
}

// ============================================================================
// Content Read/Write
// ============================================================================

bool writeCloudContent(const String& sayingsJson, const String& personalitiesJson) {
  contentUpdateInProgress = true;

  bool ok = true;

  if (sayingsJson.length() > 0) {
    File f = LittleFS.open("/cloud/sayings.json", "w");
    if (f) {
      f.print(sayingsJson);
      f.close();
    } else {
      DBGLN("Cache: failed to write sayings");
      ok = false;
    }
  }

  if (personalitiesJson.length() > 0) {
    File f = LittleFS.open("/cloud/personalities.json", "w");
    if (f) {
      f.print(personalitiesJson);
      f.close();
    } else {
      DBGLN("Cache: failed to write personalities");
      ok = false;
    }
  }

  contentUpdateInProgress = false;
  return ok;
}

bool loadCloudSayings(JsonDocument& doc) {
  File f = LittleFS.open("/cloud/sayings.json", "r");
  if (!f) return false;

  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    DBG("Sayings parse error: ");
    DBGLN(err.c_str());
    return false;
  }
  return true;
}

bool loadCloudPersonalities(JsonDocument& doc) {
  File f = LittleFS.open("/cloud/personalities.json", "r");
  if (!f) return false;

  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    DBG("Personalities parse error: ");
    DBGLN(err.c_str());
    return false;
  }
  return true;
}

void clearContentCache() {
  LittleFS.remove("/cloud/meta.json");
  LittleFS.remove("/cloud/sayings.json");
  LittleFS.remove("/cloud/personalities.json");
  DBGLN("Cache: cleared");
}

// ============================================================================
// Cloud Saying Category Mapping
// ============================================================================
// Maps cloud category strings to firmware SayingCategory enum values.
// Returns SAY_CATEGORY_COUNT if no mapping exists (caller should fall through
// to PROGMEM).

static SayingCategory mapCloudCategory(const char* cloudCat) {
  if (strcmp(cloudCat, "idle") == 0)      return SAY_IDLE;
  if (strcmp(cloudCat, "reaction") == 0)  return SAY_REACT_SHAKE;  // used for both shake + tap
  if (strcmp(cloudCat, "greeting") == 0)  return SAY_GREETING;
  if (strcmp(cloudCat, "farewell") == 0)  return SAY_IDLE;         // map to idle for MVP
  if (strcmp(cloudCat, "sleep") == 0)     return SAY_SLEEP;
  if (strcmp(cloudCat, "wake") == 0)      return SAY_WAKE;
  if (strcmp(cloudCat, "custom") == 0)    return SAY_IDLE;         // map to idle for MVP
  return SAY_CATEGORY_COUNT;  // no match
}

// Which firmware categories can have cloud overrides
static bool categoryHasCloudMapping(SayingCategory cat) {
  switch (cat) {
    case SAY_IDLE:
    case SAY_REACT_SHAKE:
    case SAY_REACT_TAP:
    case SAY_GREETING:
    case SAY_SLEEP:
    case SAY_WAKE:
      return true;
    default:
      return false;
  }
}

// ============================================================================
// getCloudSaying — Pick a random cloud saying for a firmware category
// ============================================================================
// Returns true if a cloud saying was found and copied into buffer.
// Returns false if no cloud sayings available (caller should use PROGMEM).

bool getCloudSaying(SayingCategory category, char* buffer, uint8_t bufSize) {
  // Skip if update in progress or category has no cloud mapping
  if (contentUpdateInProgress) return false;
  if (!categoryHasCloudMapping(category)) return false;

  JsonDocument doc;
  if (!loadCloudSayings(doc)) return false;

  JsonArray sayings = doc.as<JsonArray>();
  if (sayings.isNull() || sayings.size() == 0) return false;

  // Collect indices of sayings matching this category
  // Use a small static buffer to avoid heap allocation
  static uint16_t matchIndices[64];
  uint16_t matchCount = 0;

  for (size_t i = 0; i < sayings.size() && matchCount < 64; i++) {
    const char* cat = sayings[i]["category"] | "";
    SayingCategory mapped = mapCloudCategory(cat);

    // For SAY_REACT_TAP, also accept "reaction" category
    if (mapped == category ||
        (category == SAY_REACT_TAP && mapped == SAY_REACT_SHAKE)) {
      matchIndices[matchCount++] = i;
    }
  }

  if (matchCount == 0) return false;

  // Pick random match
  uint16_t pick = matchIndices[random(0, matchCount)];
  const char* text = sayings[pick]["text"] | "";

  if (strlen(text) == 0) return false;

  strncpy(buffer, text, bufSize - 1);
  buffer[bufSize - 1] = '\0';
  return true;
}

#endif // CLOUD_ENABLED
#endif // CONTENT_CACHE_H
