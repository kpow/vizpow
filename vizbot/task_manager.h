#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// Task Manager — I2C Mutex & Command Queue
// ============================================================================
// Prevents race conditions between WiFi handlers, IMU, and touch on
// shared I2C bus. Commands from WiFi/touch go through a queue so the
// render loop can apply them atomically between frames.
//
// Sprint 2: Runs in the existing single-threaded loop().
// Sprint 3: Will be consumed by FreeRTOS tasks on separate cores.
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
};

// 32-byte command payload — fits all command types
struct Command {
  CommandType type;
  union {
    uint8_t  u8val;
    uint16_t u16val;
    int32_t  i32val;
    struct {
      char text[28];
      uint16_t duration;
    } say;
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

// ============================================================================
// Drain Queue — called once per frame from the main loop
// ============================================================================
// Forward declarations for functions that apply commands
extern void setBotExpression(uint8_t index);
extern void setBotFaceColor(uint16_t color);
extern void setBotBackgroundStyle(uint8_t style);
extern void showBotSaying(const char* text, uint16_t durationMs);
extern void toggleBotTimeOverlay();
extern bool isBotTimeOverlayEnabled();
extern uint8_t brightness;
extern bool autoCycle;

void drainCommandQueue() {
  if (cmdQueue == nullptr) return;

  Command cmd;
  while (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE) {
    switch (cmd.type) {
      case CMD_SET_BRIGHTNESS:
        brightness = constrain(cmd.u8val, 1, 50);
        FastLED.setBrightness(brightness);
        break;
      case CMD_SET_EXPRESSION:
        setBotExpression(cmd.u8val);
        break;
      case CMD_SET_FACE_COLOR:
        setBotFaceColor(cmd.u16val);
        break;
      case CMD_SET_BG_STYLE:
        setBotBackgroundStyle(cmd.u8val);
        break;
      case CMD_SAY_TEXT:
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
        break;
    }
  }
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
