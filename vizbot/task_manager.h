#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <Arduino.h>
#include <DNSServer.h>
#include "config.h"

// ============================================================================
// Task Manager — I2C Mutex, Command Queue & WiFi Task
// ============================================================================
// Prevents race conditions between WiFi handlers, IMU, and touch on
// shared I2C bus. Commands from WiFi/touch go through a queue so the
// render loop can apply them atomically between frames.
//
// Sprint 3: WiFi server.handleClient() runs in its own FreeRTOS task
// pinned to Core 0, keeping the render loop on Core 1 unblocked.
// ============================================================================

// ============================================================================
// I2C Mutex
// ============================================================================
// IMU and touch share the same I2C bus. Without a mutex, concurrent
// reads can corrupt transactions. FreeRTOS mutex so it's ready for
// multi-core in Sprint 3.

static SemaphoreHandle_t i2cMutex = nullptr;

void initI2CMutex() {
  i2cMutex = xSemaphoreCreateMutex();
  if (i2cMutex == nullptr) {
    DBGLN("ERROR: Failed to create I2C mutex");
  }
}

// Acquire I2C bus. Returns true if acquired within timeout.
bool i2cAcquire(uint32_t timeoutMs = 50) {
  if (i2cMutex == nullptr) return true;  // No mutex = no protection (fallback)
  return xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

// Release I2C bus.
void i2cRelease() {
  if (i2cMutex == nullptr) return;
  xSemaphoreGive(i2cMutex);
}

// ============================================================================
// Command Queue — WiFi/Touch -> Render
// ============================================================================
// Instead of web handlers directly mutating global state while the
// render loop is reading it, they push commands to this queue.
// The main loop drains the queue between frames.

enum CommandType : uint8_t {
  CMD_SET_BRIGHTNESS = 0,
  CMD_SET_EXPRESSION,
  CMD_SET_FACE_COLOR,
  CMD_SET_BG_STYLE,
  CMD_SAY_TEXT,
  CMD_SET_TIME_OVERLAY,
  CMD_TOGGLE_TIME_OVERLAY,
  CMD_SET_AUTOCYCLE,
  CMD_SET_HIRES_MODE,
  CMD_TOGGLE_INFO_MODE,
  CMD_SET_PERSONALITY,
  CMD_SET_AMBIENT_EFFECT,
  CMD_PLAY_SOUND,
  CMD_SET_VOLUME,
  CMD_AUTO_BRIGHTNESS,
  CMD_SLEEP,
  CMD_MESH_SCAN,
  CMD_SET_PERSONALITY_LIST,
};

// ~64-byte command payload — fits all command types including multi-word phrases
struct Command {
  CommandType type;
  union {
    uint8_t  u8val;
    uint16_t u16val;
    int32_t  i32val;
    struct {
      char text[60];
      uint16_t duration;
    } say;
    struct {
      uint16_t freq;
      uint16_t duration;
    } sound;
    struct {
      uint8_t  list[12];   // personality indices for rotation
      uint8_t  count;
      uint32_t intervalMs;
    } plist;
  };
};

#define CMD_QUEUE_SIZE 8

static QueueHandle_t cmdQueue = nullptr;

void initCommandQueue() {
  cmdQueue = xQueueCreate(CMD_QUEUE_SIZE, sizeof(Command));
  if (cmdQueue == nullptr) {
    DBGLN("ERROR: Failed to create command queue");
  }
}

// Push a command (non-blocking, drops if queue full)
bool pushCommand(const Command& cmd) {
  if (cmdQueue == nullptr) return false;
  return xQueueSend(cmdQueue, &cmd, 0) == pdTRUE;
}

// Convenience helpers for common commands
void cmdSetBrightness(uint8_t val) {
  Command cmd;
  cmd.type = CMD_SET_BRIGHTNESS;
  cmd.u8val = val;
  pushCommand(cmd);
}

void cmdSetExpression(uint8_t val) {
  Command cmd;
  cmd.type = CMD_SET_EXPRESSION;
  cmd.u8val = val;
  pushCommand(cmd);
}

void cmdSetFaceColor(uint16_t color) {
  Command cmd;
  cmd.type = CMD_SET_FACE_COLOR;
  cmd.u16val = color;
  pushCommand(cmd);
}

void cmdSetBgStyle(uint8_t val) {
  Command cmd;
  cmd.type = CMD_SET_BG_STYLE;
  cmd.u8val = val;
  pushCommand(cmd);
}

void cmdSayText(const char* text, uint16_t durationMs) {
  Command cmd;
  cmd.type = CMD_SAY_TEXT;
  strncpy(cmd.say.text, text, sizeof(cmd.say.text) - 1);
  cmd.say.text[sizeof(cmd.say.text) - 1] = '\0';
  cmd.say.duration = durationMs;
  pushCommand(cmd);
}

void cmdSetTimeOverlay(bool enabled) {
  Command cmd;
  cmd.type = CMD_SET_TIME_OVERLAY;
  cmd.u8val = enabled ? 1 : 0;
  pushCommand(cmd);
}

void cmdToggleTimeOverlay() {
  Command cmd;
  cmd.type = CMD_TOGGLE_TIME_OVERLAY;
  pushCommand(cmd);
}

void cmdSetAutoCycle(bool enabled) {
  Command cmd;
  cmd.type = CMD_SET_AUTOCYCLE;
  cmd.u8val = enabled ? 1 : 0;
  pushCommand(cmd);
}

void cmdSetHiResMode(bool enabled) {
  Command cmd;
  cmd.type = CMD_SET_HIRES_MODE;
  cmd.u8val = enabled ? 1 : 0;
  pushCommand(cmd);
}

void cmdToggleInfoMode() {
  Command cmd;
  cmd.type = CMD_TOGGLE_INFO_MODE;
  cmd.u8val = 0;
  pushCommand(cmd);
}

void cmdSetPersonality(uint8_t val) {
  Command cmd;
  cmd.type = CMD_SET_PERSONALITY;
  cmd.u8val = val;
  pushCommand(cmd);
}

void cmdSetAmbientEffect(uint8_t val) {
  Command cmd;
  cmd.type = CMD_SET_AMBIENT_EFFECT;
  cmd.u8val = val;
  pushCommand(cmd);
}

void cmdPlaySound(uint16_t freq, uint16_t duration) {
  Command cmd;
  cmd.type = CMD_PLAY_SOUND;
  cmd.sound.freq = freq;
  cmd.sound.duration = duration;
  pushCommand(cmd);
}

void cmdSetVolume(uint8_t vol) {
  Command cmd;
  cmd.type = CMD_SET_VOLUME;
  cmd.u8val = vol;
  pushCommand(cmd);
}

void cmdAutoBrightness(bool enabled) {
  Command cmd;
  cmd.type = CMD_AUTO_BRIGHTNESS;
  cmd.u8val = enabled ? 1 : 0;
  pushCommand(cmd);
}

void cmdSleep(uint32_t durationMs) {
  Command cmd;
  cmd.type = CMD_SLEEP;
  cmd.i32val = (int32_t)durationMs;
  pushCommand(cmd);
}

void cmdMeshScan() {
  Command cmd;
  cmd.type = CMD_MESH_SCAN;
  pushCommand(cmd);
}

// ============================================================================
// Drain Queue — called once per frame from the main loop
// ============================================================================
// Forward declarations for functions that apply commands
extern void setBotExpression(uint8_t index);
extern void setBotFaceColor(uint16_t color);
extern void setBotBackgroundStyle(uint8_t style);
extern void showBotSaying(const char* text, uint16_t durationMs);
extern void setBotPersonality(uint8_t index);
extern void toggleBotTimeOverlay();
extern bool isBotTimeOverlayEnabled();
extern uint8_t brightness;
extern uint8_t effectIndex;
extern bool autoCycle;
extern bool hiResMode;
extern void toggleHiResMode();

// Deferred say — held until mesh peer finishes WLED
static bool deferredSayPending = false;
static Command deferredSayCmd;

// Mesh peer check (defined in esp_now_mesh.h, safe to read from Core 1)
extern bool meshAnyPeerWledActiveForIP(uint32_t);
extern uint32_t wledGetIPAsU32();

void drainCommandQueue() {
  if (cmdQueue == nullptr) return;

  // Check if a deferred say can now execute
  if (deferredSayPending) {
    if (!meshAnyPeerWledActiveForIP(wledGetIPAsU32())) {
      deferredSayPending = false;
      showBotSaying(deferredSayCmd.say.text, deferredSayCmd.say.duration);
    }
    // Still blocked — skip processing new commands to preserve ordering
    return;
  }

  Command cmd;
  while (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE) {
    switch (cmd.type) {
      case CMD_SET_BRIGHTNESS:
        brightness = constrain(cmd.u8val, 1, 50);
        FastLED.setBrightness(brightness);
        markSettingsDirty();
        break;
      case CMD_SET_EXPRESSION:
        setBotExpression(cmd.u8val);
        break;
      case CMD_SET_FACE_COLOR:
        setBotFaceColor(cmd.u16val);
        break;
      case CMD_SET_BG_STYLE:
        setBotBackgroundStyle(cmd.u8val);
        markSettingsDirty();
        break;
      case CMD_SAY_TEXT:
        // Defer speech if a mesh peer is currently using WLED
        if (meshAnyPeerWledActiveForIP(wledGetIPAsU32())) {
          deferredSayPending = true;
          deferredSayCmd = cmd;
          DBGLN("Say deferred — mesh peer using WLED");
          return;  // Stop draining — preserve command ordering
        }
        showBotSaying(cmd.say.text, cmd.say.duration);
        break;
      case CMD_SET_TIME_OVERLAY:
        // Set to desired state — toggle if it doesn't match
        if ((cmd.u8val == 1) != isBotTimeOverlayEnabled()) {
          toggleBotTimeOverlay();
        }
        break;
      case CMD_TOGGLE_TIME_OVERLAY:
        toggleBotTimeOverlay();
        break;
      case CMD_SET_AUTOCYCLE:
        autoCycle = (cmd.u8val == 1);
        markSettingsDirty();
        break;
      case CMD_SET_HIRES_MODE:
        if ((cmd.u8val == 1) != hiResMode) {
          toggleHiResMode();  // handles screen clear + markSettingsDirty
        }
        break;
      case CMD_TOGGLE_INFO_MODE: {
        extern struct InfoModeData infoMode;
        if (infoMode.active) {
          infoMode.beginExitTransition();
        } else {
          infoMode.beginEnterTransition();
        }
        break;
      }
      case CMD_SET_PERSONALITY:
        setBotPersonality(cmd.u8val);
        // Setting single personality stops rotation
        botMode.personalityListCount = 0;
        botMode.personalityRotIntervalMs = 0;
        break;
      case CMD_SET_PERSONALITY_LIST:
        botMode.personalityListCount = min((uint8_t)MAX_RUNTIME_PERSONALITIES, cmd.plist.count);
        for (uint8_t i = 0; i < botMode.personalityListCount; i++) {
          botMode.personalityList[i] = cmd.plist.list[i];
        }
        botMode.personalityRotIntervalMs = cmd.plist.intervalMs;
        botMode.lastPersonalityRotMs = millis();
        break;
      case CMD_SET_AMBIENT_EFFECT:
        effectIndex = cmd.u8val % NUM_AMBIENT_EFFECTS;
        markSettingsDirty();
        break;
      case CMD_PLAY_SOUND:
        #ifdef TARGET_CORES3
        {
          extern BotSounds botSounds;
          botSounds.playTone(cmd.sound.freq, cmd.sound.duration);
        }
        #endif
        break;
      case CMD_SET_VOLUME:
        #ifdef TARGET_CORES3
        {
          extern BotSounds botSounds;
          botSounds.setVolume(cmd.u8val);
          markSettingsDirty();
        }
        #endif
        break;
      case CMD_AUTO_BRIGHTNESS:
        {
          extern bool autoBrightnessEnabled;
          autoBrightnessEnabled = (cmd.u8val == 1);
        }
        break;
      case CMD_SLEEP:
        {
          extern struct InfoModeData infoMode;
          if (infoMode.active) {
            infoMode.beginExitTransition();
          }
          // TODO: transition to BOT_SLEEPING state with duration
          // For now just trigger idle-to-sleepy
        }
        break;
      case CMD_MESH_SCAN:
        {
          extern volatile bool meshScanRequested;
          meshScanRequested = true;
        }
        break;
    }
  }
}

// ============================================================================
// WiFi Server Task — unified Core 0 network loop
// ============================================================================
// All network operations in one cooperative loop: HTTP server, DNS, WLED,
// weather, and cloud TLS. Single task = natural serialization — cloud TLS
// can't overlap with WLED HTTP, preventing heap fragmentation.

extern WebServer server;
extern DNSServer dnsServer;
extern bool wifiEnabled;

// Defined in wifi_provisioning.h — handles connect request + STA polling.
// Runs on Core 0 (same core as handler), no cross-core visibility issues.
extern void pollWifiConnectTask();

// Defined in wled_display.h — sends queued text to WLED + handles restore timer.
extern void pollWledDisplay();

// Defined in weather_data.h — checks fetchRequested flag and fetches if needed.
extern void pollWeatherFetch();

// Defined in esp_now_mesh.h — periodic mesh broadcast + stale peer eviction.
extern void pollMeshBroadcast();

// Defined in cloud_client.h — non-blocking cloud sync (TLS registration + polling).
#ifdef CLOUD_ENABLED
extern void pollCloudSync();
extern void pollScheduledCommands();
#endif

TaskHandle_t wifiTaskHandle = nullptr;

// Static task stack — lives in BSS instead of heap, keeping the largest
// free heap block contiguous (~33KB) so TLS can allocate its ~32KB buffers.
// IMPORTANT: StackType_t is uint8_t on ESP-IDF, so array size = bytes directly.
static StackType_t wifiTaskStack[8192];   // 8KB in BSS
static StaticTask_t wifiTaskTCB;

void wifiServerTask(void* param) {
  for (;;) {
    if (wifiEnabled) {
      pollWifiConnectTask();             // connect request + STA poll
      pollWledDisplay();                 // WLED text send + restore
      pollWeatherFetch();                // Weather API fetch (if requested)
      #ifdef CLOUD_ENABLED
      pollCloudSync();                   // Cloud registration + sync (TLS)
      pollScheduledCommands();           // Execute scheduled commands at their target time
      #endif
      pollScheduledContent();              // Periodic weather + emoji cycles
      pollMeshBroadcast();               // ESP-NOW mesh broadcast + stale eviction
      dnsServer.processNextRequest();    // Captive portal DNS
      server.handleClient();             // HTTP
    }
    vTaskDelay(pdMS_TO_TICKS(2));  // ~500 req/s max, yields to WiFi stack
  }
}

// Call after WiFi AP + web server are up (end of boot sequence)
void startWifiTask() {
  wifiTaskHandle = xTaskCreateStaticPinnedToCore(
    wifiServerTask,   // Task function
    "wifi_srv",        // Name
    8192,              // Stack depth (StackType_t = uint8_t on ESP-IDF, so bytes)
    nullptr,           // Parameter
    1,                 // Priority (low — WiFi stack is higher)
    wifiTaskStack,     // Stack buffer (BSS)
    &wifiTaskTCB,      // TCB (BSS)
    0                  // Core 0 (protocol CPU)
  );
  DBGLN("WiFi server task started on Core 0 (static 8KB)");
}

// ============================================================================
// Init all task infrastructure
// ============================================================================

void initTaskManager() {
  initI2CMutex();
  initCommandQueue();
  DBGLN("Task manager initialized (I2C mutex + command queue)");
}

#endif // TASK_MANAGER_H
