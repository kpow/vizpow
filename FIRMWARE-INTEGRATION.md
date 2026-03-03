# vizBot Firmware Integration with vizCloud

This document contains everything needed to add vizCloud integration to the vizBot firmware. It is self-contained — you should not need to reference any other spec or document.

---

## 1. Overview

**vizCloud** is a Node.js server that manages a fleet of vizBot devices. It handles bot registration, content delivery (sayings, personalities), and command orchestration (change expression, say text, change brightness, etc.).

**The goal:** Add a cloud client to the vizBot firmware so that bots register with vizCloud on boot, poll for commands and content updates, cache content locally, and fall back to compiled-in defaults when the server is unreachable.

**Firmware repo:** `/Users/kevinpower/projects/esp32-led-matrix`
**Main firmware directory:** `/Users/kevinpower/projects/esp32-led-matrix/vizbot/`
**Build system:** Arduino IDE (NOT PlatformIO). Single `.ino` entry + header files.
**Target MCU:** ESP32-S3

### Design Principles

- **Offline-first:** The bot must never block on the server. If unreachable, run on cached or compiled-in content.
- **Non-blocking:** Cloud operations run on Core 0 (WiFi core) via FreeRTOS task. Never block the render loop on Core 1.
- **Graceful degradation:** Content priority: server-provided cached content > compiled-in defaults.
- **Polling model:** Bot polls the server. Server does not push to bots.

---

## 2. Live Server Configuration

Use these values to connect to the deployed vizCloud instance:

```cpp
#define CLOUD_SERVER_URL    "https://vizcloud-raxo5.ondigitalocean.app"
#define CLOUD_BOT_SECRET    "349baac1c179460b0ea78ca572bcc7a1187bcab891b71b47c28dce5dae5c5103"
```

**Health check endpoint** (no auth required — use this to test HTTPS connectivity):
```
GET https://vizcloud-raxo5.ondigitalocean.app/health
Response: { "status": "ok", "uptime": 12345.678 }
```

**TLS/SSL:** The ESP32 Arduino core includes a root CA bundle that covers DigitalOcean's certificates. Use `WiFiClientSecure` with the default bundle — no custom cert pinning needed.

---

## 3. Bot API Reference

All bot endpoints require the `X-Bot-Secret` header. All request/response bodies are JSON (`Content-Type: application/json`).

**Base URL:** `https://vizcloud-raxo5.ondigitalocean.app/api/bots`

**Rate limit:** 100 requests per 60 seconds per bot (keyed by MAC address, then bot ID, then IP).

### 3.1 POST /api/bots/register

Called once on boot after WiFi STA connection is established. Also called on reconnect after being offline.

**Headers:**
```
Content-Type: application/json
X-Bot-Secret: 349baac1c179460b0ea78ca572bcc7a1187bcab891b71b47c28dce5dae5c5103
```

**Request body:**
```json
{
  "macAddress": "AA:BB:CC:DD:EE:FF",
  "hardwareType": "vizbot_lcd_169_touch",
  "firmwareVersion": "1.2.3",
  "localIp": "192.168.1.100",
  "capabilities": {
    "screenWidth": 240,
    "screenHeight": 280,
    "hasTouch": true,
    "hasIMU": true,
    "hasLED": true,
    "hasAudio": false,
    "hasBotMode": true
  }
}
```

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `macAddress` | string | Yes | Format: `XX:XX:XX:XX:XX:XX` (hex with colons). Use `WiFi.macAddress()`. |
| `hardwareType` | string | Yes | One of: `vizbot_lcd_169_touch`, `vizbot_lcd_169_shake`, `m5stack_cores3`, `vizpow_led_matrix` |
| `firmwareVersion` | string | No | Semantic version string, e.g. `"2.0.0"` |
| `localIp` | string | No | Local network IP from `WiFi.localIP().toString()` |
| `capabilities` | object | No | Hardware capabilities. Defaults to `{}` if omitted. |

**Response (200 OK):**
```json
{
  "botId": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "name": "vizBot-Alpha",
  "contentVersion": 1,
  "pollInterval": 10,
  "config": {
    "brightness": 25,
    "personalityId": null
  },
  "content": {
    "sayings": [
      {
        "id": "uuid-here",
        "text": "Just vibing over here...",
        "category": "idle",
        "moodExpression": null,
        "minDisplayMs": 3000,
        "timeRestrictStart": null,
        "timeRestrictEnd": null,
        "priority": 0
      }
    ],
    "personalities": [
      {
        "id": "uuid-here",
        "name": "Chill",
        "idleExpression": 0,
        "idleBlinkRate": 1.0,
        "idleLookRate": 1.0,
        "chatterFrequency": 1.0,
        "expressionVariety": 1.0,
        "sleepTimeoutMs": 30000,
        "sayingIds": ["saying-uuid-1", "saying-uuid-2"]
      }
    ]
  }
}
```

| Field | Type | Notes |
|-------|------|-------|
| `botId` | string (UUID) | Store this in NVS — needed for all subsequent API calls. |
| `name` | string | Server-assigned name (e.g. "vizBot-Alpha"). Informational. |
| `contentVersion` | integer | Current content version. Store and send on every sync. |
| `pollInterval` | integer | **Seconds** between sync calls. Default is 10. Server can change this. |
| `config.brightness` | integer | 0-100 brightness level. |
| `config.personalityId` | string or null | UUID of assigned personality, or null. |
| `content` | object | Full content payload. Cache this to LittleFS. |

**Error responses:**
- `400` — Invalid/missing `macAddress` or `hardwareType`
- `401` — Invalid `X-Bot-Secret`
- `429` — Rate limited
- `500` — Server error

### 3.2 POST /api/bots/:id/sync

The primary communication channel. Called every `pollInterval` seconds.

**URL:** `/api/bots/{botId}/sync` (use the `botId` from registration)

**Headers:**
```
Content-Type: application/json
X-Bot-Secret: 349baac1c179460b0ea78ca572bcc7a1187bcab891b71b47c28dce5dae5c5103
```

**Request body:**
```json
{
  "contentVersion": 1,
  "status": "active",
  "commandAcks": ["cmd-uuid-1", "cmd-uuid-2"]
}
```

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `contentVersion` | integer | Yes | The content version the bot currently has cached. |
| `status` | string | No | Bot status string, e.g. `"active"`. |
| `commandAcks` | array of strings | No | UUIDs of commands received in the previous sync. Send these to confirm receipt. |

**Response (200 OK):**
```json
{
  "contentVersion": 1,
  "commands": [
    {
      "id": "cmd-uuid-here",
      "type": "expression",
      "payload": { "value": 5 }
    },
    {
      "id": "cmd-uuid-2",
      "type": "say",
      "payload": { "text": "Hello from the cloud!", "duration": 5000 }
    }
  ],
  "content": null,
  "config": null
}
```

| Field | Type | Notes |
|-------|------|-------|
| `contentVersion` | integer | Server's current version. Store this as your new local version. |
| `commands` | array | Pending commands. Execute these and ack their IDs on the next sync. |
| `content` | object or null | **Only populated if `contentVersion` doesn't match.** When non-null, it has the same shape as the register response's `content` field. Cache it to LittleFS. |
| `config` | object or null | Reserved for future use. Currently always null. |

**Key behaviors:**
- Send `commandAcks` with the IDs of commands you received last time. Server deletes them.
- If `content` is non-null, you have new content — write it to cache and update your local content version.
- Commands older than 1 hour are auto-deleted server-side (you won't receive stale commands).

### 3.3 POST /api/bots/:id/heartbeat

Lightweight keepalive. Use this if you want to stay "online" without a full sync (e.g., between syncs).

**URL:** `/api/bots/{botId}/heartbeat`

**Headers:**
```
Content-Type: application/json
X-Bot-Secret: 349baac1c179460b0ea78ca572bcc7a1187bcab891b71b47c28dce5dae5c5103
```

**Request body:** `{}` (empty JSON object)

**Response:** `{ "ok": true }`

**Note:** If the bot's `last_seen_at` is older than `pollInterval * 3` seconds, the server marks it offline. With a default 10s poll interval, the bot has 30 seconds before being marked offline.

---

## 4. Data Models

### 4.1 Saying

```json
{
  "id": "uuid",
  "text": "Just vibing over here...",
  "category": "idle",
  "moodExpression": null,
  "minDisplayMs": 3000,
  "timeRestrictStart": null,
  "timeRestrictEnd": null,
  "priority": 0
}
```

| Field | Type | Notes |
|-------|------|-------|
| `id` | string (UUID) | Unique identifier. |
| `text` | string | The saying text. Max 500 characters. |
| `category` | string | One of: `idle`, `reaction`, `greeting`, `farewell`, `sleep`, `wake`, `custom` |
| `moodExpression` | integer or null | Expression index (0-19) to display with this saying. Null = don't change expression. |
| `minDisplayMs` | integer | Minimum milliseconds to show the speech bubble. Default 3000. |
| `timeRestrictStart` | string or null | Time-of-day restriction start (e.g. `"08:00:00"`). Null = no restriction. |
| `timeRestrictEnd` | string or null | Time-of-day restriction end. |
| `priority` | integer | Higher = more likely to be selected. 0 = normal. |

### 4.2 Personality

```json
{
  "id": "uuid",
  "name": "Chill",
  "idleExpression": 0,
  "idleBlinkRate": 1.0,
  "idleLookRate": 1.0,
  "chatterFrequency": 1.0,
  "expressionVariety": 1.0,
  "sleepTimeoutMs": 30000,
  "sayingIds": ["uuid-1", "uuid-2"]
}
```

| Field | Type | Notes |
|-------|------|-------|
| `id` | string (UUID) | Unique identifier. |
| `name` | string | `"Chill"`, `"Hyper"`, `"Grumpy"`, or `"Sleepy"` (4 defaults). |
| `idleExpression` | integer | Expression index (0-19) for idle state. |
| `idleBlinkRate` | float | Multiplier for blink frequency. 1.0 = normal. |
| `idleLookRate` | float | Multiplier for autonomous look-around. 1.0 = normal. |
| `chatterFrequency` | float | Multiplier for how often the bot speaks. 1.0 = normal. |
| `expressionVariety` | float | Multiplier for random expression changes. 1.0 = normal. |
| `sleepTimeoutMs` | integer | Milliseconds of inactivity before sleep mode. |
| `sayingIds` | array of strings | UUIDs of sayings assigned to this personality. |

### 4.3 Command

```json
{
  "id": "cmd-uuid",
  "type": "expression",
  "payload": { "value": 5 }
}
```

See Section 5 for all command types and their payloads.

---

## 5. Command Types Reference

Commands are queued by the admin and delivered to bots via the sync endpoint. The bot should execute the command and then include the command's `id` in `commandAcks` on the next sync call.

| Type | Payload | Action |
|------|---------|--------|
| `expression` | `{ "value": 0-19 }` | Change facial expression to the given index. |
| `say` | `{ "text": "Hello!", "duration": 5000 }` | Show speech bubble with text for `duration` ms. |
| `personality` | `{ "name": "Hyper" }` | Switch to the named personality. |
| `brightness` | `{ "value": 0-100 }` | Set LED brightness. |
| `speed` | `{ "value": 1-10 }` | Adjust animation speed. |
| `mode` | `{ "value": "bot" }` | Set operational mode. |
| `background` | `{ "value": 0 }` | Change background/ambient effect index. |
| `effect` | `{ ... }` | Trigger a visual effect (post-MVP). |
| `palette` | `{ ... }` | Change color palette (post-MVP). |
| `reboot` | `{}` | Restart the device. Call `ESP.restart()`. |

**For MVP, implement at minimum:** `expression`, `say`, `personality`, `brightness`, `background`, `reboot`.

---

## 6. Current Firmware Architecture

### 6.1 Project Structure

All files are in `/Users/kevinpower/projects/esp32-led-matrix/vizbot/`:

| File | Role |
|------|------|
| `vizbot.ino` | Entry point: `setup()` and `loop()`. Boot sequence, main render loop at ~30fps. |
| `config.h` | Board selection (`TARGET_*` defines), compile-time constants. |
| `device_id.h` | Per-device unique identifier from MAC suffix. Used for AP SSID and mDNS. |
| `bot_mode.h` | **Main bot state machine.** States: `BOT_ACTIVE`, `BOT_IDLE`, `BOT_SLEEPY`, `BOT_SLEEPING`. Contains 4 personality presets. |
| `bot_faces.h` | **20+ expression definitions** with parametric rendering (eyes, mouth, brows). Indexed 0-19+. |
| `bot_eyes.h` | Eye animation: blink, look-around, pupils. |
| `bot_sayings.h` | **Hardcoded sayings in PROGMEM.** 12 categories, ~80 sayings total. |
| `bot_overlays.h` | Speech bubble rendering, time overlay display. |
| `task_manager.h` | **FreeRTOS infrastructure.** I2C mutex, command queue (8 slots, 32 bytes each), WiFi task on Core 0. |
| `wifi_provisioning.h` | STA/AP WiFi provisioning. NVS credential storage in `"vizwifi"` namespace. |
| `web_server.h` | Captive portal + HTTP control endpoints on port 80. Runs on Core 0. |
| `settings.h` | NVS persistence (`"vizbot"` namespace). Dirty-flag pattern with 2s debounced flush. |
| `boot_sequence.h` | 9-stage visual boot screen on LCD. |
| `effects_ambient.h` | 13 ambient LED effects for bot background. |
| `palettes.h` | 15 FastLED color palettes. |
| `display_lcd.h` | LCD init, double-buffered rendering via Canvas. |
| `touch_control.h` | CST816T capacitive touch handling. |
| `layout.h` | Screen geometry constants. |
| `weather_data.h` | Open-Meteo weather API (HTTP GET). |
| `info_mode.h` | Weather info page display. |
| `system_status.h` | Hardware readiness tracking flags. |

### 6.2 Dual-Core Task Structure

| Core | What Runs | Notes |
|------|-----------|-------|
| **Core 0** | WiFi server task (web_server, DNS, WiFi connect, WLED) | Low priority. Non-blocking. |
| **Core 1** | Main `loop()` — IMU read, touch, rendering, command queue drain | ~30fps render loop. |

**Command queue** (`task_manager.h`): FreeRTOS queue, 8 slots, 32 bytes each. WiFi task and touch handler push commands; `loop()` on Core 1 drains and executes them. Non-blocking send (drops if full).

**WiFi task creation:**
```cpp
xTaskCreatePinnedToCore(wifiServerTask, "wifi_srv", 6144, nullptr, 1, &wifiTaskHandle, 0);
```

### 6.3 NVS Namespaces

| Namespace | Keys | Used By |
|-----------|------|---------|
| `"vizbot"` | `bright`, `lcdBr`, `effect`, `palette`, `autoCyc`, `bgStyle`, `hiRes`, `wLat`, `wLon`, `devName`, `wledIP`, `wledOn`, `wledSpd`, `wledR/G/B` | `settings.h` |
| `"vizwifi"` | `ssid`, `pass`, `verified` | `wifi_provisioning.h` |

### 6.4 What Does NOT Exist Yet

- No HTTPS client (only HTTP via `WiFiClient` for weather API)
- No LittleFS or SPIFFS — no filesystem partition in `partitions.csv`
- No ArduinoJson library (need to add for JSON parsing)
- No content caching
- No cloud authentication

---

## 7. New Modules to Create

### 7.1 `cloud_client.h`

Handles all communication with vizCloud.

**Responsibilities:**
- HTTPS POST requests to vizCloud using `WiFiClientSecure` + `HTTPClient`
- Bot registration on boot (after WiFi STA connects)
- Periodic sync polling on `pollInterval`
- Command execution dispatch (push received commands to existing FreeRTOS command queue)
- Content delivery to `content_cache.h` when new content arrives
- Retry logic with backoff on failure
- Store `botId` in NVS after first registration

**Runs on:** Core 0 as a FreeRTOS task (alongside the existing WiFi server task, or as part of it).

**NVS namespace:** `"vizcloud"` with keys:
- `botId` (string, 36 chars) — UUID from registration. Empty on first boot.
- `pollInt` (uint16) — poll interval in seconds. Default 10.
- `contVer` (int32) — cached content version number. Default 0.

**Cloud server URL and secret** should be `#define` constants in `config.h`:
```cpp
#define CLOUD_ENABLED        true
#define CLOUD_SERVER_URL     "https://vizcloud-raxo5.ondigitalocean.app"
#define CLOUD_BOT_SECRET     "349baac1c179460b0ea78ca572bcc7a1187bcab891b71b47c28dce5dae5c5103"
#define CLOUD_POLL_DEFAULT   10  // seconds, overridden by server
```

### 7.2 `content_cache.h`

Manages LittleFS-based content cache.

**Responsibilities:**
- Initialize LittleFS filesystem
- Write/read sayings and personalities as JSON files
- Track content version number
- Provide API for bot_mode.h and bot_sayings.h to read cached content
- Fall back to compiled-in PROGMEM content if cache is empty or corrupt

**LittleFS file layout:**
```
/cloud/
  version.txt          -- single integer: content version number
  sayings.json         -- full sayings array from server
  personalities.json   -- full personalities array from server
  config.json          -- bot config (brightness, personalityId)
```

---

## 8. Implementation Requirements

### 8.1 Partition Table Changes

The current `partitions.csv` has no filesystem partition. Add a LittleFS partition by reducing the app partition. The content payload is small (a few KB), so 64KB-128KB is sufficient.

**Current partitions.csv:**
```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x5000
otadata,  data, ota,     0xe000,   0x2000
app0,     app,  ota_0,   0x10000,  0x3F0000
```

**Updated partitions.csv (add LittleFS):**
```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x5000
otadata,  data, ota,     0xe000,   0x2000
app0,     app,  ota_0,   0x10000,  0x3D0000
littlefs, data, spiffs,  0x3E0000, 0x20000
```

This gives 128KB for LittleFS (more than enough for cached JSON) while keeping ~3.8MB for the app.

### 8.2 Boot Sequence Changes

Update `boot_sequence.h` from 9 stages to 11:

```
[1/11]  LCD .............. OK
[2/11]  LEDs ............. OK
[3/11]  I2C Bus .......... OK
[4/11]  IMU .............. OK
[5/11]  Touch ............ OK
[6/11]  WiFi AP .......... OK
[7/11]  DNS .............. OK
[8/11]  mDNS ............. OK
[9/11]  Web Server ....... OK
[10/11] LittleFS ......... OK
[11/11] Cloud ............ OK / OFFLINE (cached)
```

**Stage 10 (LittleFS):** Initialize LittleFS. If it fails, format and retry once. Load cached content version.

**Stage 11 (Cloud):** If WiFi STA is connected, attempt registration. If successful, show "OK". If server is unreachable, show "OFFLINE (cached)" and continue with cached/compiled-in content. This stage must **not block** for more than 5 seconds.

### 8.3 Cloud Sync Task

Create a new FreeRTOS task on Core 0 for cloud polling:

```cpp
void cloudSyncTask(void* param) {
    while (true) {
        if (wifiSTAConnected && cloudEnabled) {
            if (botId is empty) {
                // Not registered yet — try to register
                registerWithCloud();
            } else {
                // Already registered — sync
                syncWithCloud();
            }
        }
        vTaskDelay(pollInterval * 1000 / portTICK_PERIOD_MS);
    }
}
```

**Stack size:** 8192 bytes (HTTPS needs more stack than plain HTTP due to TLS).
**Priority:** 1 (same as WiFi server task).
**Core:** 0.

### 8.4 Registration Flow

```
1. Read MAC address: WiFi.macAddress()
2. Determine hardwareType from config.h TARGET_* defines
3. Build JSON request body with ArduinoJson
4. POST to /api/bots/register with X-Bot-Secret header
5. On 200 response:
   a. Parse JSON response
   b. Store botId in NVS ("vizcloud" / "botId")
   c. Store pollInterval in NVS
   d. Store contentVersion in NVS
   e. Write content to LittleFS cache (sayings.json, personalities.json)
   f. Apply config (brightness, personality)
6. On failure:
   a. Log error
   b. Load cached content from LittleFS (if any)
   c. Fall back to compiled-in PROGMEM defaults (if no cache)
   d. Retry on next poll interval
```

### 8.5 Sync Flow

```
1. Build JSON: { contentVersion, status: "active", commandAcks: [...] }
2. POST to /api/bots/{botId}/sync
3. On 200 response:
   a. Parse commands array — for each command:
      - Push to existing FreeRTOS command queue (task_manager.h)
      - Store command ID for ack on next sync
   b. If content is non-null:
      - Write to LittleFS cache
      - Update local contentVersion
      - Reload sayings/personalities from cache
   c. If config is non-null (future):
      - Apply brightness, poll interval changes
   d. Clear commandAcks list, replace with new command IDs
4. On failure:
   a. Keep existing commandAcks (retry ack next time)
   b. Continue running with cached content
```

### 8.6 Command Execution Mapping

Map cloud command types to existing firmware actions. Push commands through the existing FreeRTOS command queue in `task_manager.h`.

| Cloud Command | Firmware Action | Implementation |
|---------------|-----------------|----------------|
| `expression` | Change face | Set expression index via `setExpression(payload.value)` in `bot_mode.h` |
| `say` | Show speech bubble | Call speech bubble function in `bot_overlays.h` with `payload.text` and `payload.duration` |
| `personality` | Switch personality | Match `payload.name` to personality enum in `bot_mode.h` |
| `brightness` | Set LED brightness | `FastLED.setBrightness(payload.value)` + save to NVS |
| `background` | Change ambient effect | Set effect index in `effects_ambient.h` |
| `speed` | Adjust animation speed | Apply speed multiplier to animation timers |
| `mode` | Set operating mode | Switch between bot mode / info mode |
| `reboot` | Restart device | `ESP.restart()` |

### 8.7 Content Caching Strategy

**Write to cache** (after register or sync with new content):
1. Serialize sayings array to JSON string with ArduinoJson
2. Write to `/cloud/sayings.json` on LittleFS
3. Serialize personalities array to JSON string
4. Write to `/cloud/personalities.json`
5. Write content version to `/cloud/version.txt`

**Read from cache** (on boot, or after cache update):
1. Read `/cloud/version.txt` → local content version
2. Read and parse `/cloud/sayings.json` → populate runtime sayings array
3. Read and parse `/cloud/personalities.json` → populate runtime personalities

**Fallback chain:**
1. LittleFS cache (from last successful sync)
2. Compiled-in PROGMEM defaults (existing `bot_sayings.h` content)

**Memory considerations:**
- Use ArduinoJson's streaming/filter API for parsing — don't load entire response into RAM
- Sayings payload is typically 5-15KB depending on count
- Personalities payload is typically 1-3KB
- ESP32-S3 has plenty of RAM, but stream when possible

### 8.8 Saying Category Mapping

The cloud uses 7 categories. The firmware uses 12. Map them:

| Cloud Category | Firmware Category | Notes |
|----------------|-------------------|-------|
| `idle` | `SAY_IDLE` | Direct match. |
| `reaction` | `SAY_REACT_SHAKE` + `SAY_REACT_TAP` | Cloud doesn't distinguish shake vs tap — use for both or pick one. |
| `greeting` | `SAY_GREETING` | Direct match. |
| `farewell` | (no direct match) | New category — add to firmware or map to `SAY_IDLE`. |
| `sleep` | `SAY_SLEEP` | Direct match. |
| `wake` | `SAY_WAKE` | Direct match. |
| `custom` | (no direct match) | Treat as `SAY_IDLE` or add a new generic category. |

Firmware categories with no cloud equivalent (keep compiled-in defaults for these):
- `SAY_REACT_TAP` — can use cloud `reaction` sayings
- `SAY_TIME_MORNING`, `SAY_TIME_AFTERNOON`, `SAY_TIME_EVENING`, `SAY_TIME_NIGHT` — time-based, no cloud equivalent yet
- `SAY_STATUS` — system status messages, keep local
- `SAY_INFO_ENTER` — info mode transition, keep local

### 8.9 Personality Mapping

Map cloud personality fields to firmware `BotPersonality` struct fields:

| Cloud Field | Firmware Field | Mapping |
|-------------|----------------|---------|
| `name` | Personality enum | Match by name: `"Chill"` → `PERSONALITY_CHILL`, etc. |
| `idleExpression` | `favoriteExpressions[0]` | Expression index for idle state. |
| `idleBlinkRate` | Blink timer multiplier | 1.0 = default firmware timing. Scale intervals by `1.0 / idleBlinkRate`. |
| `idleLookRate` | Look-around timer multiplier | Scale look intervals by `1.0 / idleLookRate`. |
| `chatterFrequency` | `sayIntervalMin/Max` multiplier | Scale say intervals by `1.0 / chatterFrequency`. |
| `expressionVariety` | `expressionIntervalMin/Max` multiplier | Scale expression change intervals by `1.0 / expressionVariety`. |
| `sleepTimeoutMs` | `sleepTimeoutMs` | Direct mapping in milliseconds. |
| `sayingIds` | Filtered sayings list | Only use sayings with IDs in this list when this personality is active. |

**Note:** The firmware personality struct uses `idleTimeoutMs`, `sleepyTimeoutMs`, `sleepTimeoutMs` as separate timeouts for idle→sleepy→sleep transitions. The cloud only has `sleepTimeoutMs`. For MVP, map `sleepTimeoutMs` to the firmware's sleep timeout and derive the others proportionally (e.g., idle = sleep/4, sleepy = sleep/1.5).

---

## 9. Error Handling & Offline Strategy

### Connection Failures

| Scenario | Behavior |
|----------|----------|
| WiFi STA not connected | Skip all cloud operations. Run on cached/default content. |
| HTTPS connection failed | Log error, retry on next poll interval. No crash, no blocking. |
| HTTP 401 (bad secret) | Log error. Stop retrying (secret is wrong — needs firmware update). |
| HTTP 429 (rate limited) | Back off — double poll interval temporarily (up to 60s max). |
| HTTP 500 (server error) | Log error, retry on next poll interval. |
| JSON parse failure | Log error, discard response. Keep current cached content. |
| LittleFS write failure | Log error. Content stays in RAM for current session but won't persist across reboots. |

### Timeouts

- HTTPS connection timeout: **5 seconds**
- HTTPS response timeout: **10 seconds**
- Registration attempt during boot: **5 second max** (don't hold up boot)

### Offline Behavior

When the server is unreachable, the bot should be fully functional:
- Expressions, animations, touch/shake reactions all work normally
- Sayings come from cache or PROGMEM defaults
- Personality behavior uses cached personality data or compiled-in defaults
- Cloud sync task silently retries on each poll interval
- No user-visible error messages (the bot just works)

---

## 10. Libraries to Add

| Library | Purpose | Install |
|---------|---------|---------|
| **ArduinoJson** (v7+) | JSON serialization/deserialization | Arduino Library Manager: "ArduinoJson" by Benoit Blanchon |
| **LittleFS** | Filesystem for content cache | Built into ESP32 Arduino core (`#include <LittleFS.h>`) |

`WiFiClientSecure` and `HTTPClient` are already part of the ESP32 Arduino core — no additional install needed.

---

## 11. Testing Checklist

### Step 1: Verify HTTPS Connectivity
```
GET https://vizcloud-raxo5.ondigitalocean.app/health
Expected: { "status": "ok", "uptime": ... }
```
Test with `WiFiClientSecure` + `HTTPClient`. If TLS fails, the root CA bundle might need updating.

### Step 2: Test Registration
```
POST /api/bots/register
With X-Bot-Secret header and valid body
Expected: 200 with botId, content, etc.
```
After success, check the admin dashboard at `https://vizcloud-raxo5.ondigitalocean.app/admin` — the bot should appear in the list as "online".

### Step 3: Test Sync
```
POST /api/bots/{botId}/sync
With contentVersion from registration
Expected: 200 with empty commands, content: null
```

### Step 4: Test Command Delivery
1. From admin dashboard, send an "expression" command to the bot
2. On next sync, the bot should receive the command in the `commands` array
3. Bot executes the expression change
4. On the following sync, bot sends `commandAcks` with the command ID
5. Admin dashboard should show `command:delivered`

### Step 5: Test Content Update
1. From admin dashboard, add a new saying
2. Click "Publish" to bump content version
3. On next sync, bot should receive `content` (non-null) with the new saying
4. Verify the saying appears in the bot's cache

### Step 6: Test Offline Fallback
1. Disconnect WiFi (or point to a bad URL)
2. Bot should continue running with cached content
3. Reconnect WiFi — bot should resume syncing

---

## 12. Implementation Order

Suggested order for incremental development and testing:

1. **Add ArduinoJson** to the project
2. **Update partitions.csv** and add LittleFS init to boot sequence
3. **Create `content_cache.h`** — LittleFS read/write/format functions
4. **Create `cloud_client.h`** — HTTPS client, registration only
5. **Test registration** against live server
6. **Add sync loop** to `cloud_client.h`
7. **Add command execution** — wire cloud commands into existing command queue
8. **Add content caching** — write server content to LittleFS, read on boot
9. **Add personality/saying integration** — use cloud content instead of PROGMEM when available
10. **Test full lifecycle** — register, sync, receive commands, content updates, offline fallback

---

## Appendix A: Default Seed Data on Server

The server is seeded with this content (already live):

**33 Sayings** across 6 categories:
- 15 idle, 5 reaction, 4 greeting, 3 farewell, 3 sleep, 3 wake

**4 Personalities:**

| Name | idleExpression | idleBlinkRate | idleLookRate | chatterFrequency | expressionVariety | sleepTimeoutMs |
|------|---------------|---------------|--------------|-----------------|-------------------|----------------|
| Chill | 0 | 1.0 | 1.0 | 1.0 | 1.0 | 30000 |
| Hyper | 9 | 1.5 | 2.0 | 2.0 | 2.0 | 60000 |
| Grumpy | 15 | 0.7 | 0.5 | 1.5 | 0.5 | 15000 |
| Sleepy | 4 | 0.5 | 0.3 | 0.3 | 0.3 | 10000 |

## Appendix B: ArduinoJson Example

Parsing a sync response:

```cpp
#include <ArduinoJson.h>

// After receiving HTTP response body in String payload:
JsonDocument doc;
DeserializationError error = deserializeJson(doc, payload);
if (error) {
    Serial.printf("JSON parse error: %s\n", error.c_str());
    return;
}

int contentVersion = doc["contentVersion"];

// Process commands
JsonArray commands = doc["commands"];
for (JsonObject cmd : commands) {
    const char* id = cmd["id"];
    const char* type = cmd["type"];
    JsonObject cmdPayload = cmd["payload"];

    if (strcmp(type, "expression") == 0) {
        int value = cmdPayload["value"];
        // Push to command queue...
    } else if (strcmp(type, "say") == 0) {
        const char* text = cmdPayload["text"];
        int duration = cmdPayload["duration"] | 5000;
        // Show speech bubble...
    } else if (strcmp(type, "reboot") == 0) {
        ESP.restart();
    }
    // Store id for ack on next sync
}

// Check for content update
if (!doc["content"].isNull()) {
    JsonObject content = doc["content"];
    // Write to LittleFS cache...
}
```

## Appendix C: HTTPS Request Example

```cpp
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

WiFiClientSecure client;
client.setCACertBundle(NULL);  // Use default root CA bundle

HTTPClient http;
http.begin(client, "https://vizcloud-raxo5.ondigitalocean.app/api/bots/register");
http.addHeader("Content-Type", "application/json");
http.addHeader("X-Bot-Secret", CLOUD_BOT_SECRET);
http.setTimeout(10000);

String body = buildRegistrationJson();  // ArduinoJson serialized string
int httpCode = http.POST(body);

if (httpCode == 200) {
    String response = http.getString();
    // Parse with ArduinoJson...
} else {
    Serial.printf("Registration failed: HTTP %d\n", httpCode);
}

http.end();
```
