# VizBot: Rock-Solid Networking & Sequential Boot Plan

## Goal
Replace the current fragile single-threaded WiFi setup with a **captive portal / WiFi provisioning** pattern, a **sequential boot loader with onscreen diagnostics**, and eliminate race conditions by moving to a proper dual-core FreeRTOS architecture.

---

## Phase 1: Sequential Boot Loader with Onscreen Diagnostics

**Problem:** Everything initializes in a blind sequence with no feedback. If WiFi fails, you'd never know.

**Solution:** A visual boot sequence on the LCD that shows each subsystem initializing, with pass/fail indicators.

### Files: `boot_sequence.h` (new), modifications to `vizbot.ino`

### Boot Stages (displayed on LCD):
```
[1/7] LCD .............. OK
[2/7] LEDs ............. OK  (64 LEDs on GPIO14)
[3/7] I2C Bus .......... OK  (SDA:11 SCL:10)
[4/7] IMU .............. OK  (QMI8658 @ 0x6B)
[5/7] Touch ............ OK  (CST816 @ 0x15)
[6/7] WiFi AP .......... OK  (vizBot 192.168.4.1)
[7/7] Web Server ....... OK  (port 80)

All systems ready. Starting vizBot...
```

### Implementation:
- Each stage runs **sequentially** with a dedicated init function
- Each stage has a **timeout** (e.g., WiFi: 5s, IMU: 2s, Touch: 3s)
- On failure: show RED status, log error, continue with degraded mode
- On success: show GREEN status, store capability flags
- Boot screen uses direct `gfx->` calls (no canvas, no FastLED — nothing else running yet)
- Store results in a `SystemStatus` struct:
  ```cpp
  struct SystemStatus {
    bool lcdReady;
    bool ledsReady;
    bool i2cReady;
    bool imuReady;
    bool touchReady;
    bool wifiReady;
    bool webServerReady;
    IPAddress apIP;
    uint32_t bootTimeMs;
  };
  ```
- The rest of the firmware checks these flags before using any subsystem

---

## Phase 2: Fix Race Conditions — Move to Dual-Core FreeRTOS

**Problem:** Single `loop()` runs WiFi, rendering, IMU, touch, and effects all in one thread. Everything fights for time and corrupts shared state.

**ESP32-S3 has two cores.** We should use them.

### Task Architecture:
```
Core 0 (Protocol CPU):            Core 1 (Application CPU):
┌─────────────────────┐           ┌─────────────────────┐
│  WiFi Task          │           │  Render Task         │
│  - handleClient()   │           │  - updateBotMode()   │
│  - DNS server       │           │  - renderBotMode()   │
│  - captive portal   │           │  - FastLED.show()    │
│  Priority: 1        │           │  - renderToLCD()     │
│                     │           │  Priority: 2         │
│  Sensor Task        │           │                      │
│  - readIMU()        │           │                      │
│  - handleTouch()    │           │                      │
│  - I2C mutex        │           │                      │
│  Priority: 2        │           │                      │
└─────────────────────┘           └─────────────────────┘
```

### Shared State Protection:
- **Command Queue** (FreeRTOS queue): WiFi task pushes commands (set expression, brightness, etc.), render task consumes them. No direct global writes from WiFi.
- **I2C Mutex** (FreeRTOS mutex): IMU and touch take the mutex before any I2C operation.
- **Render Lock** (FreeRTOS mutex): Canvas pointer swap is protected. Nobody else touches `gfx` during render.
- `currentPalette` changes are **double-buffered** — write to shadow, swap atomically.

### Files modified:
- `vizbot.ino` — replace `loop()` with task creation
- `web_server.h` — push commands to queue instead of direct writes
- `bot_mode.h` — consume command queue in update
- `touch_control.h` — acquire I2C mutex
- New: `task_manager.h` — FreeRTOS task setup, queues, mutexes

---

## Phase 3: Captive Portal & WiFi Provisioning

**Problem:** Hardcoded AP with no captive portal. Phones don't auto-open a config page. WiFi just "doesn't work" from the user's perspective.

**Solution:** Proper captive portal with DNS redirect so ANY device that connects gets redirected to the control page.

### How Captive Portal Works:
1. ESP32 starts as WiFi AP (`vizBot` / `12345678`)
2. DNS server runs on port 53, responds to ALL queries with `192.168.4.1`
3. Phone/laptop connects, OS does connectivity check (e.g., `captive.apple.com`)
4. DNS redirects to our IP → OS detects captive portal → auto-opens browser
5. User sees the vizBot control page immediately

### Implementation:
- **DNSServer library** (built into ESP32 Arduino) — wildcard DNS
- **AsyncWebServer** (ESPAsyncWebServer) — replaces blocking `WebServer`
  - Non-blocking request handling
  - Runs on its own task on Core 0
  - Handles multiple concurrent clients
  - Much better memory management
- **Captive portal detection endpoints:**
  - `/generate_204` (Android)
  - `/hotspot-detect.html` (Apple)
  - `/connecttest.txt` (Windows)
  - `/redirect` (Firefox)
  - All return 302 redirect to `http://192.168.4.1/`
- **mDNS** — `vizbot.local` hostname for easy access

### Files:
- `web_server.h` — rewrite with ESPAsyncWebServer + DNS + captive portal handlers
- `config.h` — add DNS/mDNS config
- `platformio.ini` or Arduino IDE — add `ESPAsyncWebServer` + `AsyncTCP` dependencies

---

## Phase 4: Persistent Settings (Preferences library)

**Problem:** Every power cycle resets brightness, expressions, WiFi state, etc.

**Solution:** ESP32 `Preferences` library (NVS flash) — simple key/value store, wear-leveled, no filesystem needed.

### Store:
- `brightness` (uint8_t)
- `autoCycle` (bool)
- `effectIndex` (uint8_t)
- `paletteIndex` (uint8_t)
- `botBackgroundStyle` (uint8_t)
- `wifiEnabled` (bool)
- Future: custom SSID/password for WiFi provisioning

### Files:
- New: `settings.h` — load/save with Preferences, with defaults
- `vizbot.ino` — load settings during boot sequence
- `web_server.h` — save settings on change

---

## Phase 5: Watchdog & Health Monitoring

**Problem:** If something hangs (WiFi stack, I2C, rendering), the device locks up silently.

### Solution:
- **ESP32 Task Watchdog Timer (TWDT)** — each FreeRTOS task registers with watchdog, must feed it every N seconds or the system reboots
- **Health heartbeat on LCD** — small indicator (dot or icon) that pulses each frame. If it freezes, you know the render task is stuck.
- **WiFi reconnect logic** — if AP goes down, auto-restart it
- **Boot reason logging** — on startup, check `esp_reset_reason()` and display if it was a watchdog reboot, brownout, etc.

---

## Implementation Order

| Step | What | Risk | Depends On |
|------|------|------|------------|
| 1 | Boot sequence with LCD diagnostics | Low | Nothing |
| 2 | SystemStatus flags & degraded mode | Low | Step 1 |
| 3 | I2C mutex for IMU+Touch | Medium | Nothing (can do parallel with 1-2) |
| 4 | Command queue for WiFi→Render | Medium | Step 3 |
| 5 | FreeRTOS dual-core task split | High | Steps 3-4 |
| 6 | Replace WebServer with AsyncWebServer | Medium | Step 5 |
| 7 | DNS server + captive portal | Low | Step 6 |
| 8 | mDNS (vizbot.local) | Low | Step 6 |
| 9 | Captive portal detection endpoints | Low | Step 7 |
| 10 | Preferences persistent storage | Low | Anytime |
| 11 | Watchdog timer | Low | Step 5 |
| 12 | Health indicator on LCD | Low | Step 5 |

### Suggested Implementation Grouping:

**Sprint 1 — Boot & Diagnostics (Steps 1-2)**
- Get the sequential boot loader working with onscreen status
- Establishes the diagnostic foundation everything else builds on
- Low risk, immediately visible improvement

**Sprint 2 — Race Condition Fixes (Steps 3-4)**
- I2C mutex, command queue
- Fix the most dangerous bugs without restructuring
- Can test in current single-thread architecture first

**Sprint 3 — Architecture Upgrade (Steps 5-6)**
- FreeRTOS task split + AsyncWebServer
- Biggest change, highest risk, but unlocks everything
- The networking "just works" after this

**Sprint 4 — Captive Portal (Steps 7-9)**
- DNS + detection endpoints + mDNS
- The user-facing networking experience
- Should be smooth since AsyncWebServer is already in place

**Sprint 5 — Polish (Steps 10-12)**
- Persistent settings, watchdog, health monitoring
- Makes it production-ready

---

## Dependencies to Add

```
ESPAsyncWebServer  (async HTTP server)
AsyncTCP           (async TCP for ESP32)
DNSServer          (built-in, for captive portal)
ESPmDNS            (built-in, for .local hostname)
Preferences        (built-in, for NVS storage)
```

---

## Risk Mitigation

- **Each sprint is independently testable** — the device works after each sprint, just with more features
- **Boot diagnostics come first** — so we can SEE what's happening during all subsequent work
- **Race condition fixes before architecture change** — reduces variables during the big refactor
- **AsyncWebServer is battle-tested** — widely used in ESP32 community, well-documented
- **Captive portal is standard pattern** — Apple/Android/Windows all have well-known detection URLs

## Notes

- The current `WebServer.h` (synchronous) is the #1 suspect for "WiFi not working" — it blocks the main loop for 100ms+ per request, causing timeouts and dropped connections
- FastLED + WiFi on ESP32 is a **known conflict** — FastLED disables interrupts during `show()`, which can drop WiFi packets. The FreeRTOS split (FastLED on Core 1, WiFi on Core 0) is the standard fix.
- The ESP32-S3 has plenty of RAM (512KB SRAM + PSRAM) for double-buffering, queues, and async server buffers
