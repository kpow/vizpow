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

// ============================================================================
// Apply cloud personalities to runtime array (slots 3+)
// ============================================================================
// Called after cloud sync writes new personality data. Parses the cached JSON
// and populates runtimePersonalities[BOT_NUM_BUILTIN_PERSONALITIES..].
// Cloud rate fields (floats) are mapped to firmware timer values.

void applyCloudPersonalities() {
  JsonDocument doc;
  if (!loadCloudPersonalities(doc)) {
    DBGLN("Cache: no cloud personalities to apply");
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) {
    DBGLN("Cache: cloud personalities array empty");
    return;
  }

  uint8_t slot = BOT_NUM_BUILTIN_PERSONALITIES;
  for (JsonObject p : arr) {
    if (slot >= MAX_RUNTIME_PERSONALITIES) break;

    RuntimePersonality& rp = runtimePersonalities[slot];
    memset(&rp, 0, sizeof(RuntimePersonality));

    const char* name = p["name"] | "Cloud";
    strncpy(rp.name, name, sizeof(rp.name) - 1);

    const char* id = p["id"] | "";
    strncpy(rp.cloudId, id, sizeof(rp.cloudId) - 1);

    // Map cloud rate multipliers to firmware timer values
    // Base values: exprMin=4000, exprMax=10000, sayMin=16000, sayMax=40000
    float exprVariety  = p["expressionVariety"] | 1.0f;
    float chatterFreq  = p["chatterFrequency"]  | 1.0f;

    // Higher variety = shorter expression timers (more frequent changes)
    rp.exprMinMs = (uint32_t)(4000.0f / max(exprVariety, 0.1f));
    rp.exprMaxMs = (uint32_t)(10000.0f / max(exprVariety, 0.1f));

    // Higher chatter = shorter saying timers
    rp.sayMinMs = (uint32_t)(16000.0f / max(chatterFreq, 0.1f));
    rp.sayMaxMs = (uint32_t)(40000.0f / max(chatterFreq, 0.1f));

    rp.sayChancePercent = (uint8_t)min(90, (int)(30.0f * chatterFreq));

    rp.idleTimeoutMs = p["sleepTimeoutMs"] | 90000;

    // Favorite expressions
    JsonArray favExprs = p["favoriteExpressions"];
    if (favExprs) {
      rp.favoriteExprCount = min((size_t)8, favExprs.size());
      for (uint8_t i = 0; i < rp.favoriteExprCount; i++) {
        rp.favoriteExprs[i] = favExprs[i] | 0;
      }
    }

    // Favorite palettes
    JsonArray favPals = p["favoritePalettes"];
    if (favPals) {
      rp.favoritePaletteCount = min((size_t)4, favPals.size());
      for (uint8_t i = 0; i < rp.favoritePaletteCount; i++) {
        rp.favoritePalettes[i] = favPals[i] | 0;
      }
    }

    // Favorite effects
    JsonArray favFx = p["favoriteEffects"];
    if (favFx) {
      rp.favoriteEffectCount = min((size_t)4, favFx.size());
      for (uint8_t i = 0; i < rp.favoriteEffectCount; i++) {
        rp.favoriteEffects[i] = favFx[i] | 0;
      }
    }

    // Saying category bitmask
    rp.sayingCategoryMask = p["sayingCategoryMask"] | 0;

    DBG("Cache: loaded cloud personality [");
    DBG(slot);
    DBG("] ");
    DBGLN(rp.name);

    slot++;
  }

  runtimePersonalityCount = slot;
  DBG("Cache: total personalities loaded: ");
  DBGLN(runtimePersonalityCount);
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
