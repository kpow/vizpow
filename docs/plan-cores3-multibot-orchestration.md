# Hardware Exploitation & Multi-Bot Orchestration Plan

## Overview

This document is a staged plan covering:

**Part A** — Exploit hardware capabilities on BOTH boards. The Waveshare has unused RTC, buzzer, battery ADC, and GPIO pads. The Core S3 adds speaker, dual mics, proximity/light sensor, camera, PMIC, SD card, and multi-touch. Each feature has a **decision section** — not all will be implemented.

**Part B** — Upgrade vizCloud from single-bot control to multi-bot orchestration. Groups, synchronized expressions, fleet management, ESP-NOW local mesh.

**Part C** — Integration with the display upgrades plan (LovyanGFX, tweens, enhanced face).

**Part D** — Unified staged roadmap.

---

## Hardware Corrections & Clarifications

| Item | Old Assumption | Actual |
|------|---------------|--------|
| Waveshare PSRAM | 2MB | **8MB OPI** (ESP32-S3R8 chip, Arduino IDE: PSRAM → "OPI PSRAM") |
| Waveshare Flash | 4MB | **16MB** (external Quad SPI) |
| Waveshare RTC | None | **PCF85063** on I2C bus (0x51), completely unused in firmware |
| Waveshare Buzzer | None | **GPIO33** (old board) / **GPIO42** (new board), completely unused |
| Waveshare Battery ADC | None | **GPIO1** with 200K/100K voltage divider, completely unused |
| Waveshare GPIO pads | None | **GPIO2, GPIO3, GPIO17, GPIO18** exposed as solder pads, plus I2C + UART pads |
| Core S3 Battery | 500mAh internal | **No battery installed** (PMIC exists but battery-dependent features are conditional) |
| Local mesh | BLE 5.0 | **ESP-NOW** (2-3KB overhead vs 60-75KB for BLE, <1ms latency, native broadcast) |

---

## Memory Budget Analysis

### Current Heap Baseline

| Component | Size | Location |
|-----------|------|----------|
| LCD framebuffer (TARGET_LCD) | 134KB | BSS static (or PSRAM if available) |
| LCD framebuffer (TARGET_CORES3) | 154KB | PSRAM via LGFX_Sprite |
| WiFi task stack | 8KB | BSS static |
| WiFi task TCB | ~376B | BSS static |
| Command queue (8 x 64B) | 512B | Heap |
| Cloud ack buffer (8 x 48B) | 384B | BSS static |
| WLED pixel buffer | 768B | BSS static |
| TLS peak (during sync) | ~24-32KB | Heap (contiguous required) |
| Heap guard threshold | 28KB | Minimum contiguous block before cloud sync |

### Cost of Proposed Additions

| Addition | Heap Cost | Notes |
|----------|-----------|-------|
| ESP-NOW (10 peers) | **~2-3KB** | Piggybacks on existing WiFi stack |
| Expanded JSON sync (+200B request, +500B response) | **~1-3KB peak** | Transient during serialize/parse |
| Mic recording (256 samples @ 16kHz) | **~3KB** | 512B sample buffer + 2KB DMA (internal SRAM required) |
| Audio analysis struct | **~64B** | RMS, peak, flags |
| Proximity/light sensor state | **~32B** | Lux, proximity raw, flags |
| RTC time struct | **~16B** | Cached time |
| Buzzer state | **~8B** | Tone frequency, duration |
| **Total all additions** | **~8-9KB** | Safe margin with 28KB TLS guard |

### PSRAM Available (Both Boards: 8MB)

Both boards have 8MB PSRAM. Large buffers (LCD canvas, camera frames, audio recording buffers) should use `ps_malloc()`. Internal heap is reserved for TLS and WiFi stack.

**Camera warning:** VGA frame = 153KB in PSRAM. Only attempt if LCD canvas is also in PSRAM and both fit comfortably.

### Verdict: Memory is NOT a blocker

Adding sensor polling, ESP-NOW mesh, and expanded JSON to the sync API costs ~8-9KB total. With the BSS framebuffer strategy keeping ~33KB contiguous heap for TLS, there's plenty of room. The dangerous option (BLE at 60-75KB) has been eliminated.

---

# PART A: Hardware Exploitation

## A.0 Hardware Comparison — Corrected

| Feature | Waveshare LCD 1.69" | M5Stack Core S3 | Both? |
|---------|---------------------|-----------------|-------|
| Display | ST7789V2 240x280 portrait | ILI9342C 320x240 landscape | Different |
| Touch | CST816T single-touch | FT6336U multi-touch + gestures | Different |
| IMU | QMI8658 6-axis | BMI270 + BMM150 9-axis | Different |
| RTC | **PCF85063** (0x51, unused!) | BM8563 (0x51) | **BOTH** (different chips, same function) |
| Buzzer/Speaker | **Buzzer** (GPIO33/42, unused!) | AW88298 I2S 1W speaker | Both have audio out (buzzer vs speaker) |
| Audio In | None | ES7210 dual MEMS mics | Core S3 only |
| Proximity | None | LTR-553ALS-WA | Core S3 only |
| Ambient Light | None | LTR-553ALS-WA | Core S3 only |
| Battery ADC | **GPIO1** (voltage divider, unused!) | AXP2101 PMIC | Both (different method) |
| Camera | None | GC0308 0.3MP | Core S3 only |
| SD Card | None | microSD via SPI | Core S3 only |
| PSRAM | 8MB OPI | 8MB QSPI | **BOTH** (same size, different type) |
| Flash | 16MB | 16MB | **BOTH** |
| Power Button | None | AXP2101 hardware button | Core S3 only |
| GPIO Expansion | **4 pads** (GPIO2,3,17,18) + I2C + UART | 3x Grove ports + M-Bus | Both |
| ESP-NOW | ESP-NOW (via WiFi radio) | ESP-NOW (via WiFi radio) | **BOTH** |

### Waveshare I2C Address Map (Bus: GPIO11 SDA / GPIO10 SCL)

| Address | Device | Status |
|---------|--------|--------|
| 0x15 | CST816T | **Active** — touch |
| 0x51 | PCF85063 | **Unused** — RTC |
| 0x6B | QMI8658 | **Active** — IMU |

### Core S3 I2C Address Map (Internal Bus: GPIO12 SDA / GPIO11 SCL)

| Address | Device | Status |
|---------|--------|--------|
| 0x23 | LTR-553ALS-WA | Unused — proximity + light |
| 0x34 | AXP2101 | Managed by M5Unified — PMIC |
| 0x36 | AW88298 | Managed by M5Unified — speaker amp |
| 0x38 | FT6336U | Managed by M5Unified — touch |
| 0x40 | ES7210 | Managed by M5Unified — mic ADC |
| 0x51 | BM8563 | Managed by M5Unified — RTC |
| 0x58 | AW9523B | Managed by M5Unified — GPIO expander |
| 0x69 | BMI270 | Managed by M5Unified — IMU |

---

## A.1 RTC — Persistent Clock (BOTH BOARDS)

### Hardware
- **Waveshare:** PCF85063 (I2C 0x51) — needs library, manual init
- **Core S3:** BM8563 (I2C 0x51) — managed by `M5.Rtc` API

### What It Enables
| Feature | Description |
|---------|-------------|
| Persistent clock | NTP sync → write to RTC. On boot without WiFi, read from RTC. Time overlay always accurate. |
| Time-aware expressions | Morning cheerful, afternoon active, evening relaxed, night sleepy |
| Scheduled personality | Cloud pushes: "Hyper 9-12, Chill 12-5, Sleepy after 10pm" |
| Alarm/reminder | User sets via web UI or cloud. Bot wakes and alerts. |
| Timed sleep (Core S3 only) | Power off, schedule RTC wake at specific time |

### Memory Cost
~16 bytes for cached time struct. RTC library (PCF85063) adds ~2-3KB flash.

### Files
- `vizbot/rtc_manager.h` — NEW: abstraction over PCF85063 (Waveshare) and BM8563 (Core S3), NTP→RTC sync, time-of-day classification

### Decision: Implement RTC?

| | |
|---|---|
| **Effort** | Low (both chips are simple I2C RTC) |
| **Value** | High — time overlay becomes reliable without WiFi, time-aware expressions add character |
| **Risk** | Low — read-only sensor, no side effects |
| **Memory** | Negligible (~16B RAM, ~3KB flash) |
| **Both boards?** | Yes |
| **Depends on** | Nothing — can be done independently |

---

## A.2 Buzzer / Speaker — Audio Output

### Hardware
- **Waveshare:** Piezo buzzer on GPIO33 (old board) / GPIO42 (new board). Single tone only via `tone()` / PWM.
- **Core S3:** AW88298 I2S Class-D 1W speaker. 8 mixing channels, WAV playback, 48kHz. `M5.Speaker.tone()`, `M5.Speaker.playRaw()`, `M5.Speaker.playWav()`.

### What It Enables

| Bot Event | Waveshare (Buzzer) | Core S3 (Speaker) |
|-----------|-------------------|-------------------|
| Boot complete | 3-note ascending beep | Rich startup chime |
| Expression change | Short blip (pitch varies by mood) | Pitched chirp with envelope |
| Tap reaction | Quick boop | Squeak / boop sound |
| Shake reaction | Rapid alternating tones | Rattle / dizzy sound effect |
| Speech bubble appear | Short pop beep | Pop sound via playRaw() |
| Sleep transition | Descending melody | Yawn-like tone sweep |
| Wake up | Rising beep | Rising chime |
| Notification | Single alert tone | Alert ding |
| Cloud command received | Subtle beep | Subtle acknowledgment |

### Memory Cost
- Waveshare buzzer: ~0 bytes (just `tone(pin, freq, duration)`)
- Core S3 speaker: ~0 bytes for tones. WAV clips: stored in PROGMEM or SD card, streamed.

### Cloud Integration
New command type:
```json
{ "type": "sound", "payload": { "tone": 440, "duration": 200 } }
```

### Files
- `vizbot/bot_sounds.h` — NEW: sound effect definitions, platform abstraction (buzzer vs speaker)
- `vizbot/bot_mode.h` — trigger sounds on state transitions

### Decision: Implement Audio?

| | |
|---|---|
| **Effort** | Low (buzzer trivial, Core S3 speaker managed by M5Unified) |
| **Value** | High — sound is the single biggest "aliveness" upgrade after the face itself |
| **Risk** | Low — can be toggled on/off, volume control |
| **Memory** | Negligible for tones. WAV clips need flash/SD space. |
| **Both boards?** | Yes (buzzer is limited but still adds personality) |
| **Depends on** | Nothing — can be done independently |

---

## A.3 Dual Microphones — Sound Reactivity (CORE S3 ONLY)

### Hardware
- ES7210 dual MEMS microphones via I2S Port 1
- Default 16kHz sample rate, 16-bit PCM
- `M5.Mic.record(buffer, length)`

### What It Enables

| Audio Event | Detection Method | Bot Reaction |
|-------------|------------------|--------------|
| Loud noise / clap | RMS amplitude spike > 3x average | Startled (SURPRISED), "Whoa!" |
| Sustained talking | RMS in speech range for >1s | Attentive (FOCUSED), pupils track stereo balance |
| Music / rhythm | Beat detection via amplitude peaks | Eyes pulse with beat |
| Extended silence | RMS below threshold for >30s | Transition to SLEEPY |
| Laughter | High amplitude + rapid variation | HAPPY or EXCITED |

### Implementation — Lightweight Audio Analysis
No FFT needed. Simple time-domain RMS analysis on Core 0:
```cpp
struct AudioAnalysis {
    int16_t sampleBuffer[256];     // 512 bytes
    float rmsLevel;                // current loudness 0.0-1.0
    float smoothLevel;             // low-pass filtered
    bool isSpikeDetected;          // sudden loud event
    bool isSpeechDetected;         // sustained mid-level
    uint32_t silenceDurationMs;    // how long quiet
    int8_t stereoBalance;          // -100 to +100
};
```

### Memory Cost
~3KB total (512B sample buffer + 2KB I2S DMA buffers in internal SRAM + ~128B DMA descriptors)

### Files
- `vizbot/audio_analysis.h` — NEW: mic sampling, RMS, spike/speech detection
- `vizbot/bot_mode.h` — consume audio events as interaction triggers

### Decision: Implement Microphone?

| | |
|---|---|
| **Effort** | Medium (I2S DMA setup, analysis code, tuning thresholds) |
| **Value** | High — clap-reactive and sound-aware bot is a wow factor |
| **Risk** | Medium — DMA buffers MUST be internal SRAM, threshold tuning needed |
| **Memory** | ~3KB internal SRAM (non-negotiable, can't use PSRAM for DMA) |
| **Both boards?** | No — Core S3 only |
| **Depends on** | Nothing — but pairs well with speaker (A.2) for call-and-response |

---

## A.4 Proximity & Ambient Light Sensor (CORE S3 ONLY)

### Hardware
- LTR-553ALS-WA (I2C 0x23)
- Ambient light: 0.01 - 64,000 lux
- Proximity: IR-based distance detection

### What It Enables

| Event | Detection | Bot Reaction |
|-------|-----------|--------------|
| Hand approaching | Proximity rising above threshold | Eyes widen, pupils track toward hand |
| Hand very close | High proximity, pre-touch | Anticipation expression |
| Hand withdrawn | Proximity drops | Relief or disappointment |
| Peek-a-boo | Rapid cover/uncover oscillation | Alternating SURPRISED → HAPPY |
| Room lights off | Lux < 10 | Sleepy, "It's dark..." |
| Room lights on | Lux rises after dark period | Wake, "Good morning!" |
| Bright sunlight | Lux > 10,000 | Squinting, "So bright!" |
| Auto-brightness | Continuous lux | LCD backlight scaled to ambient |

### Memory Cost
~32 bytes for sensor state struct.

### Files
- `vizbot/proximity_light.h` — NEW: sensor polling, event detection, auto-brightness

### Decision: Implement Proximity/Light?

| | |
|---|---|
| **Effort** | Low (simple I2C reads, threshold comparisons) |
| **Value** | High — proximity reactions are magical, auto-brightness is practical |
| **Risk** | Low — read-only sensor |
| **Memory** | Negligible (~32B) |
| **Both boards?** | No — Core S3 only |
| **Depends on** | Nothing |

---

## A.5 Enhanced IMU — 9-Axis (CORE S3) + Gyroscope Gestures (BOTH)

### Hardware
- **Waveshare:** QMI8658 — 6-axis (accel + gyro). Gyroscope currently unused!
- **Core S3:** BMI270 + BMM150 — 9-axis (accel + gyro + magnetometer)

### What Gyroscope Adds (BOTH BOARDS)

The current firmware only uses the accelerometer for tilt tracking and shake detection. The gyroscope is completely untapped on both boards.

| Sensor Data | New Feature | Both? |
|-------------|-------------|-------|
| Gyro rotation rate | Spin detection (dizzy with intensity) | Yes |
| Gyro integration | Gesture recognition: nod (yes), head-shake (no), lift, set-down | Yes |
| Gyro + accel fusion | Better orientation estimation (complementary filter) | Yes |
| Freefall detection | Accel magnitude near 0 = dropped. Scream on speaker. | Yes |
| Magnetometer heading | Compass-aware (Core S3 only) — know facing direction | Core S3 only |

### Orientation-Based Behaviors (Both Boards)

| Orientation | Detection | Bot Behavior |
|-------------|-----------|--------------|
| Upright | Gravity on Y-axis | Normal |
| Tilted | Gravity shifting | Eyes/face slide toward gravity |
| Upside down | Gravity on -Y | Confused, "I'm upside down!" |
| Face up (screen to ceiling) | Gravity on Z | Sleepy, "Staring at the ceiling..." |
| Face down | Gravity on -Z | Scared, "I can't see!", screen dims |
| Spinning | High gyro sustained | Progressive dizzy → spiral eyes |
| Freefall | |accel| < 0.3g | SCARED, descending tone |

### Memory Cost
~64 bytes for orientation state struct. Complementary filter adds ~20 bytes.

### Files
- `vizbot/imu_enhanced.h` — NEW: gyro processing, orientation estimation, gesture recognition
- `vizbot/bot_eyes.h` — enhanced pupil tracking with gyro smoothing

### Decision: Implement Enhanced IMU?

| | |
|---|---|
| **Effort** | Medium (gyro integration, complementary filter, gesture state machine) |
| **Value** | Very high — orientation awareness is one of the most fun features, works on BOTH boards |
| **Risk** | Low — builds on existing IMU code |
| **Memory** | Negligible (~84B) |
| **Both boards?** | **Yes** (gyro features). Compass is Core S3 only. |
| **Depends on** | Nothing — but audio (A.2) makes freefall more dramatic |

---

## A.6 Power Management / Battery

### Hardware
- **Waveshare:** Battery ADC on GPIO1 (200K/100K voltage divider). Can read voltage if battery connected.
- **Core S3:** AXP2101 PMIC — battery level %, voltage, current, charge state, power button, deep sleep. **Note: No battery currently installed.**

### What It Enables

| Feature | Waveshare | Core S3 |
|---------|-----------|---------|
| Battery level display | Voltage via ADC (if battery attached) | % via `M5.Power.getBatteryLevel()` |
| Low battery warnings | Threshold on voltage | Threshold on % |
| Charging detection | Not possible (no charger IC) | `M5.Power.isCharging()` |
| Power button input | N/A | Click/double-click/long-press events |
| Deep sleep | `esp_deep_sleep()` (no wake trigger) | `M5.Power.deepSleep()` with touch/timer wake |
| Timed power off/on | N/A | RTC alarm wake via AXP2101 |

### Memory Cost
~16 bytes for power state struct.

### Decision: Implement Power Management?

| | |
|---|---|
| **Effort** | Low (Core S3: M5Unified handles it. Waveshare: analogRead on GPIO1) |
| **Value** | Medium — useful if batteries are added later. Power button is nice on Core S3. |
| **Risk** | Low |
| **Memory** | Negligible |
| **Both boards?** | Partially (Waveshare ADC is basic, Core S3 is full-featured) |
| **Depends on** | Nothing. **Skip if no battery plans.** |

---

## A.7 Camera (CORE S3 ONLY)

### Hardware
- GC0308 0.3MP via DVP parallel interface
- VGA (640x480) stills, lower res for streaming

### What It Enables
| Feature | Complexity | Description |
|---------|------------|-------------|
| QR code scanning | Low | Configure WiFi, pair with cloud, load personality via QR |
| Motion detection | Low | Frame differencing — detect people walking by |
| Face detection | Medium | Detect human face in front, bot becomes attentive |
| Photo capture | Low | Take still on cloud command, store to SD |

### Memory Cost
**BIG:** QVGA frame = 76.8KB, VGA = 153.6KB — MUST be PSRAM.
Plus ~50KB for esp-who face detection library.

### Decision: Implement Camera?

| | |
|---|---|
| **Effort** | High (DVP init, PSRAM frame buffers, image processing) |
| **Value** | Medium — QR scanning is most practical. Face detection is cool but complex. |
| **Risk** | High — large PSRAM consumption competes with LCD canvas, complex driver |
| **Memory** | 77-154KB PSRAM per frame buffer |
| **Both boards?** | No — Core S3 only |
| **Depends on** | SD card (A.8) for photo storage |
| **Recommendation** | **Defer.** Lower ROI than other sensors. QR scanning could be a standalone later feature. |

---

## A.8 SD Card (CORE S3 ONLY)

### Hardware
- microSD slot, SDHC, SPI bus shared with display (CS=GPIO4)
- M5Unified handles bus sharing

### What It Enables
| Use Case | Description |
|----------|-------------|
| WAV sound storage | Sound clips loaded on demand (pairs with A.2 speaker) |
| Large content cache | Alternative to 128KB LittleFS — unlimited space |
| User content | Users drop custom sayings/sounds onto SD card |
| Logging | Debug logs, interaction history |

### Memory Cost
~1KB for SD library buffers (512B sector buffer + overhead).

### Decision: Implement SD Card?

| | |
|---|---|
| **Effort** | Low (M5Unified handles init, standard File API) |
| **Value** | Medium — mainly useful for WAV clips if speaker is implemented |
| **Risk** | Low — optional, graceful if no card inserted |
| **Memory** | ~1KB |
| **Both boards?** | No — Core S3 only |
| **Depends on** | Speaker (A.2) for primary use case |

---

## A.9 Multi-Touch Gestures (CORE S3 ONLY)

### Hardware
- FT6336U: 2-point multi-touch with hardware gesture detection
- IRQ on GPIO21

### What It Enables

| Gesture | Detection | Bot Reaction |
|---------|-----------|--------------|
| Single tap | 1 touch, quick release | Existing reaction (poke) |
| Double tap | 2 taps within 400ms | Toggle mode or special expression |
| Long press | Touch held > 800ms | Open touch menu (existing) |
| Swipe left/right | >50px horizontal | Previous/next expression or effect |
| Swipe up/down | >50px vertical | Brightness up/down |
| Two-finger tap | 2 simultaneous points | Easter egg expression |
| Pet/stroke | Slow repeated horizontal | Transition to BLISS, purring sound |

### Memory Cost
~32 bytes for gesture state.

### Decision: Implement Multi-Touch Gestures?

| | |
|---|---|
| **Effort** | Medium (gesture state machine, FT6336U register reads) |
| **Value** | Medium — swipe and pet are fun, but single-touch already works well |
| **Risk** | Low |
| **Memory** | Negligible |
| **Both boards?** | No — Core S3 only (CST816T on Waveshare is single-touch) |
| **Depends on** | Nothing |

---

## A.10 Waveshare Exposed GPIO Pads

### Available Pads

| Pad | GPIO | Capabilities |
|-----|------|-------------|
| OUTPUT_IO2 | GPIO2 | General purpose, ADC, touch-capable |
| OUTPUT_IO3 | GPIO3 | General purpose, ADC, touch-capable |
| OUTPUT_IO17 | GPIO17 | General purpose |
| OUTPUT_IO18 | GPIO18 | General purpose |
| I2C | GPIO10/GPIO11 | Shared with onboard I2C bus |
| UART | GPIO43/GPIO44 | TX/RX |

### Potential Uses
| Expansion | GPIO | Description |
|-----------|------|-------------|
| NeoPixel ring/strip | GPIO2 or GPIO3 | LED halo around bot matching expression color |
| Servo motor (SG90) | GPIO17 (PWM) | Physical head nod/tilt following expressions |
| PIR motion sensor | GPIO2 (digital in) | Detect people without camera |
| External button | GPIO3 (digital in) | Physical button input |
| Analog sensor | GPIO2 or GPIO3 (ADC) | Light sensor, potentiometer, etc. |

### Decision: Use GPIO Pads?

| | |
|---|---|
| **Effort** | Varies by peripheral |
| **Value** | NeoPixel ring is highest value — visual enhancement |
| **Risk** | Low — opt-in per device |
| **Both boards?** | Waveshare only (Core S3 uses Grove ports instead) |
| **Depends on** | Physical hardware connected |
| **Recommendation** | **Document as expansion options.** Implement NeoPixel support if user wants halo ring. |

---

## A.11 Grove Expansion Ports (CORE S3 ONLY)

### Ports

| Port | Pins | Type | Best Use |
|------|------|------|----------|
| Port A (Red) | GPIO1/GPIO2 | I2C | ENV sensor (temp/humidity), NFC reader |
| Port B (Black) | GPIO8/GPIO9 | GPIO/ADC/PWM | Servo, NeoPixel, PIR, IR transmitter |
| Port C (Blue) | GPIO17/GPIO18 | UART | GPS, external MCU |

### Decision: Same as A.10 — document as expansion options, implement on demand.

---

# PART B: Multi-Bot Orchestration via vizCloud

## B.0 Current State

- Each bot registers individually via MAC address, gets unique `botId`
- Bots poll `/api/bots/:id/sync` every **60 seconds**
- Commands are per-bot (must be queued individually)
- Content (sayings + personalities) is shared via `contentVersion`
- **No broadcast, no groups, no synchronization**

### Poll Interval Considerations

Current: 60 seconds. Options:
- **60s (keep):** Lowest server load, fine for content updates and non-time-critical commands
- **30s:** Better command responsiveness, 2x server load
- **10s:** Near-real-time feel, 6x server load but still light for a few bots
- **Adaptive:** Start at 60s, reduce to 10s when bot is in a group with active orchestration

**Recommendation:** Keep 60s default. Reduce to 10-15s for bots in active groups via server-pushed `pollInterval`. The server already controls this per-bot.

---

## B.1 ESP-NOW Local Mesh (BOTH BOARDS)

### Why ESP-NOW, Not BLE

| | ESP-NOW | BLE 5.0 (NimBLE) |
|---|---|---|
| Additional RAM | **~2-3KB** | **~60-75KB** |
| Latency | **<1ms** | 20-50ms |
| Broadcast | **Native** | Requires mesh stack |
| Max peers | 20 | 3-9 connections |
| Range | ~150m outdoor | ~50m |
| WiFi coexistence | **Free** (same radio) | Costs ~75KB more heap |
| Setup complexity | Low | High |
| Packet size | 250B (v1) / 1470B (v2) | 23B default MTU |

ESP-NOW runs on the WiFi radio already active. Adding it costs almost nothing.

### ESP-NOW State Packet (30 bytes)

```cpp
typedef struct __attribute__((packed)) {
    uint8_t  protocolVersion;   // 1B — always 1
    uint8_t  deviceId;          // 1B — last byte of MAC
    uint8_t  expressionIdx;     // 1B — current expression (0-19)
    uint8_t  botState;          // 1B — active/idle/sleepy/sleeping
    uint8_t  batteryPct;        // 1B — 0-100 or 0xFF if no battery
    uint8_t  mode;              // 1B — bot/ambient/emoji/info
    uint8_t  effectIdx;         // 1B — ambient effect
    uint8_t  paletteIdx;        // 1B — palette index
    int16_t  orientationPitch;  // 2B — degrees * 10
    int16_t  orientationRoll;   // 2B — degrees * 10
    uint16_t compassHeading;    // 2B — 0-359 (Core S3 only, 0xFFFF = unavailable)
    uint8_t  audioLevel;        // 1B — 0-255 RMS (Core S3 only, 0 = no mic)
    uint8_t  proximityNear;     // 1B — 0/1 (Core S3 only)
    uint16_t ambientLux;        // 2B — (Core S3 only, 0xFFFF = unavailable)
    uint8_t  reserved[12];      // 12B — future use
} MeshStatePacket;              // 30 bytes total
```

### What ESP-NOW Enables

| Feature | Description |
|---------|-------------|
| Bot discovery | Each bot broadcasts state every 2-5 seconds. Nearby bots detect each other. |
| Proximity reactions | Two bots within range: look at each other, synchronized blink, "Oh hi!" |
| Expression mirroring | Leader bot changes expression → followers react |
| Offline group sync | No WiFi/cloud needed — bots synchronize locally |
| Low-latency coordination | <1ms delivery for real-time synchronized animations |
| Compass facing detection | If two Core S3 bots report headings ~180° apart, they're facing each other |

### Implementation
- `WIFI_AP_STA` mode (already used for STA provisioning)
- ESP-NOW and WiFi AP must share the same channel
- Broadcast to `ff:ff:ff:ff:ff:ff` for discovery
- Register specific peers for directed messages

### Memory Cost
~2-3KB on top of existing WiFi stack. 30 bytes per received peer state (cached for up to 10 peers = 300 bytes).

### Files
- `vizbot/esp_now_mesh.h` — NEW: init, broadcast, receive, peer management, discovery
- `vizbot/bot_mode.h` — consume nearby-bot events

### Decision: Implement ESP-NOW Mesh?

| | |
|---|---|
| **Effort** | Medium (ESP-NOW API is simple, but discovery protocol and reactions need design) |
| **Value** | Very high for multi-bot — this IS the local mesh foundation |
| **Risk** | Low — ESP-NOW is battle-tested on ESP32, <3KB overhead |
| **Memory** | ~3KB |
| **Both boards?** | **Yes** |
| **Depends on** | Nothing. Pairs with cloud orchestration (B.2+). |

---

## B.2 Group Management (Cloud)

### Data Model

```json
{
  "id": "group-uuid",
  "name": "Living Room Bots",
  "botIds": ["bot-uuid-1", "bot-uuid-2", "bot-uuid-3"],
  "syncMode": "coordinated",
  "leaderBotId": "bot-uuid-1"
}
```

### Group Sync Modes

| Mode | Behavior |
|------|----------|
| **independent** | Bots share content but act independently (current default) |
| **mirrored** | All bots show same expression simultaneously |
| **coordinated** | Leader sets mood, followers react with complementary expressions |
| **conversation** | Bots take turns speaking (one shows bubble, others listen) |
| **performance** | Scripted choreographed sequence |

### Cloud API Additions

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/groups` | GET/POST | List/create groups |
| `/api/groups/:id` | GET/PUT/DELETE | Group CRUD |
| `/api/groups/:id/command` | POST | Broadcast command to all group members |
| `/api/groups/:id/sequence` | POST | Start choreographed sequence |
| `/api/fleet/status` | GET | All bots summary |
| `/api/fleet/broadcast` | POST | Command to ALL online bots |

### Group Command Broadcast

Admin sends command to group → server fans out to each bot's command queue → bots pick up on next sync.

---

## B.3 Synchronized Expressions

### The Timing Problem

With 60-second polling, bots receive commands at different times. "Group expression change" could take 0-60 seconds to propagate.

### Solution 1: Scheduled Commands with Timestamps

```json
{
  "type": "expression",
  "payload": { "value": 1 },
  "executeAt": 1709500000
}
```

All bots receive the command on their next sync but hold it until `executeAt`. With NTP-synced clocks (both boards have RTC for backup), bots execute within ~50ms of each other.

**Requirements:**
- All bots NTP-synced
- `executeAt` must be 2x `pollInterval` in the future (so all bots receive it)
- Firmware holds commands in queue until timestamp

### Solution 2: ESP-NOW for Instant Sync

For sub-second synchronization, the group leader receives the cloud command and immediately broadcasts via ESP-NOW to local peers. Latency: <1ms.

This is **hybrid orchestration**: cloud for remote commands, ESP-NOW for local instant relay.

### Firmware Changes

```cpp
struct Command {
    CommandType type;
    union { ... } payload;
    uint32_t executeAt;  // NEW: Unix timestamp, 0 = immediate
};
```

In `drainCommandQueue()`: if `executeAt > 0 && now() < executeAt`, re-queue and skip.

---

## B.4 Enhanced Sync Protocol

### Current Sync Request
```json
{ "contentVersion": 1, "status": "active", "commandAcks": [...] }
```

### Enhanced Sync Request
```json
{
  "contentVersion": 1,
  "status": "active",
  "commandAcks": [...],
  "state": {
    "expression": 1,
    "batteryLevel": 73,
    "orientation": "upright",
    "ambientLux": 450,
    "audioLevel": 35,
    "proximityNear": false,
    "uptime": 3600
  },
  "nearbyBots": ["vizBot-B4E2"]
}
```

Additional request payload: ~200 bytes. Peak heap during serialization: ~1-3KB (transient).

### Enhanced Sync Response
```json
{
  "contentVersion": 1,
  "commands": [...],
  "content": null,
  "config": null,
  "group": {
    "id": "group-uuid",
    "syncMode": "coordinated",
    "role": "leader",
    "memberCount": 3
  }
}
```

Additional response payload: ~200-500 bytes. Peak heap during parsing: ~1-3KB (transient).

**Memory impact: Safe.** Both are transient allocations during sync, freed immediately after.

---

## B.5 Choreographed Sequences

### Data Model
```json
{
  "id": "sequence-uuid",
  "name": "Morning Greeting",
  "duration": 15000,
  "steps": [
    { "time": 0, "botRole": "leader", "actions": [
      { "type": "expression", "value": 1 },
      { "type": "say", "text": "Good morning!", "duration": 3000 }
    ]},
    { "time": 2000, "botRole": "follower", "actions": [
      { "type": "expression", "value": 1 },
      { "type": "say", "text": "Morning!", "duration": 2000 }
    ]}
  ]
}
```

### Execution
1. Cloud sends `start_sequence` command with `executeAt` timestamp
2. Bot stores sequence, starts timer at `executeAt`
3. Each step fires at its time offset for the bot's assigned role
4. Sequence ends when duration expires

### Files
- `vizbot/bot_sequences.h` — NEW: sequence parser, timeline executor

---

## B.6 New Cloud Command Types

| Type | Payload | Description |
|------|---------|-------------|
| `sound` | `{ "tone": 440, "duration": 200 }` | Play tone |
| `set_volume` | `{ "value": 150 }` | Set speaker volume (0-255) |
| `auto_brightness` | `{ "enabled": true }` | Toggle light-sensor auto-brightness |
| `start_sequence` | `{ "sequenceId": "...", "role": "leader", "executeAt": N }` | Choreographed sequence |
| `sleep` | `{ "wakeAt": N }` | Enter deep sleep (Core S3 with RTC wake) |
| `mesh_scan` | `{}` | Report ESP-NOW discovered peers |

---

## B.7 Fleet Dashboard Enhancements

| Feature | Description |
|---------|-------------|
| Fleet overview | All bots with status, battery, expression, environment |
| Group management | Create/edit groups, assign bots |
| Live expression view | Current expression icon per bot |
| Environment dashboard | Per-bot: light level, noise, proximity, orientation |
| Broadcast controls | One-click expression/saying to group or all bots |
| Sequence editor | Timeline-based choreography builder |
| Schedule editor | Time-of-day personality switching |
| Alerts | Offline, low battery, errors |

---

# PART C: Integration with Graphics Upgrades

| Display Upgrade | Sensor Benefit | Multi-Bot Benefit |
|-----------------|----------------|-------------------|
| **LovyanGFX migration** | Eliminates DisplayProxy — unified rendering for both targets | Consistent rendering across mixed-hardware fleet |
| **Tween system** | Sound triggers sync to animation tweens. Proximity approach = smooth eye widen. Freefall = smooth panic transition. | Choreographed sequences use tweens for smooth coordinated transitions |
| **Enhanced face shapes** | More shapes for sensor reactions (proximity flinch, sound-reactive mouths) | Richer expression vocabulary for group coordination |
| **Dirty-rect rendering** | Less SPI bandwidth = lower power draw if battery added | Longer battery life for untethered multi-bot |

---

# PART D: Unified Staged Roadmap

## Stage 1: Graphics Foundation (Display Upgrades Modules 1-2)

| Task | Description |
|------|-------------|
| 1.1 | Migrate TARGET_LCD from Arduino_GFX to LovyanGFX |
| 1.2 | Unify DisplayProxy — both targets speak LovyanGFX natively |
| 1.3 | Enable DMA SPI, anti-aliased primitives |
| 1.4 | Switch speech bubble to smooth VLW font |
| 1.5 | Implement `tween.h` with 8 easing functions |
| 1.6 | Replace manual lerp in bot_eyes.h, bot_faces.h, bot_overlays.h |
| 1.7 | Verify both targets compile and render correctly |

**Outcome:** Unified graphics layer. Foundation for everything else. No throw-away code.

---

## Stage 2: Both-Board Hardware (RTC, Audio, Enhanced IMU)

| Task | Description | Both? |
|------|-------------|-------|
| 2.1 | RTC driver — PCF85063 (Waveshare) + BM8563 (Core S3) abstraction | Yes |
| 2.2 | NTP→RTC sync, persistent time overlay | Yes |
| 2.3 | Time-aware expression defaults | Yes |
| 2.4 | Buzzer/speaker driver (`bot_sounds.h`) — platform abstraction | Yes |
| 2.5 | Expression sound effects (boot, tap, shake, sleep, wake) | Yes |
| 2.6 | Enhanced IMU — gyroscope processing, orientation detection | Yes |
| 2.7 | Orientation behaviors (upside down, facedown, spinning, freefall) | Yes |
| 2.8 | IMU gesture recognition (nod, shake-head, lift, set-down) | Yes |

**Outcome:** Both boards use all their shared sensors. Bot is dramatically more alive.

---

## Stage 3: Core S3 Exclusive Sensors

| Task | Description |
|------|-------------|
| 3.1 | Proximity/ambient light driver (`proximity_light.h`) |
| 3.2 | Auto-brightness from ambient light |
| 3.3 | Proximity-reactive expressions |
| 3.4 | Mic audio analysis (`audio_analysis.h`) — RMS, spike, speech detection |
| 3.5 | Sound-reactive expressions (clap, talk, silence) |
| 3.6 | Multi-touch gesture recognition |
| 3.7 | Cloud `sound` command support |

**Outcome:** Core S3 exploits its full sensor suite.

---

## Stage 4: Enhanced Face & Speech Bubble (Display Upgrades Module 3)

| Task | Description |
|------|-------------|
| 4.1 | 6 new eye shapes |
| 4.2 | 6 new mouth shapes |
| 4.3 | New expressions using new shapes |
| 4.4 | Speech bubble rework — larger, AA font, smooth corners |
| 4.5 | Sound sync with expression transitions (tween callbacks) |
| 4.6 | Sensor-aware bubble positioning |

**Outcome:** More expressive face. Sound + sensor integration with visuals.

---

## Stage 5: ESP-NOW Local Mesh

| Task | Description |
|------|-------------|
| 5.1 | ESP-NOW init, broadcast, receive (`esp_now_mesh.h`) |
| 5.2 | State packet broadcast every 2-5 seconds |
| 5.3 | Nearby bot discovery and tracking |
| 5.4 | Proximity reactions (two bots meet → both react) |
| 5.5 | Expression mirroring / leader-follower via ESP-NOW |
| 5.6 | Hybrid relay: cloud command → leader bot → ESP-NOW broadcast to local peers |

**Outcome:** Bots aware of each other locally. Sub-millisecond coordination.

---

## Stage 6: Multi-Bot Cloud Orchestration

| Task | Description |
|------|-------------|
| 6.1 | Cloud: group management API |
| 6.2 | Cloud: group command broadcast |
| 6.3 | Enhanced sync protocol (state reporting + group info) |
| 6.4 | `executeAt` scheduled commands for synchronized expressions |
| 6.5 | Choreographed sequence API + firmware executor |
| 6.6 | Fleet dashboard |
| 6.7 | Personality schedules (time-of-day switching) |
| 6.8 | Adaptive poll interval (60s default, 10-15s for active groups) |

**Outcome:** Full fleet orchestration. Groups, broadcast, choreography, dashboard.

---

## Stage 7: Polish & Performance

| Task | Description |
|------|-------------|
| 7.1 | Dirty-rect rendering (Display Module 4) |
| 7.2 | SD card support for WAV assets (Core S3) |
| 7.3 | Camera QR scanning (Core S3, if desired) |
| 7.4 | Cloud analytics dashboard |
| 7.5 | Power optimization, memory profiling |

---

## New Files Summary

| File | Stage | Purpose | Both boards? |
|------|-------|---------|-------------|
| `tween.h` | 1 | Animation tween system | Yes |
| `rtc_manager.h` | 2 | RTC abstraction (PCF85063 / BM8563) | Yes |
| `bot_sounds.h` | 2 | Sound effects (buzzer / speaker) | Yes |
| `imu_enhanced.h` | 2 | Gyro, orientation, gestures | Yes |
| `proximity_light.h` | 3 | LTR-553 proximity + light | Core S3 |
| `audio_analysis.h` | 3 | Mic RMS analysis | Core S3 |
| `esp_now_mesh.h` | 5 | ESP-NOW local mesh | Yes |
| `bot_sequences.h` | 6 | Choreographed sequences | Yes |
| `sd_manager.h` | 7 | SD card assets | Core S3 |
| `camera_manager.h` | 7 | Camera (if desired) | Core S3 |

---

## Feature Decision Summary

Use this table to decide what to implement. Each row is independent.

| # | Feature | Effort | Value | Memory | Both Boards? | Depends On | Recommend? |
|---|---------|--------|-------|--------|-------------|------------|------------|
| A.1 | RTC persistent clock | Low | High | ~16B | Yes | Nothing | **Yes** |
| A.2 | Buzzer/speaker audio | Low | High | ~0B | Yes (buzzer/speaker) | Nothing | **Yes** |
| A.3 | Microphone reactivity | Medium | High | ~3KB internal | Core S3 only | Nothing | **Yes** |
| A.4 | Proximity/light sensor | Low | High | ~32B | Core S3 only | Nothing | **Yes** |
| A.5 | Enhanced IMU (gyro/orientation) | Medium | Very High | ~84B | Yes | Nothing | **Yes** |
| A.6 | Power/battery management | Low | Medium | ~16B | Partial | Nothing | **If battery planned** |
| A.7 | Camera | High | Medium | ~77-154KB PSRAM | Core S3 only | SD card | **Defer** |
| A.8 | SD card | Low | Medium | ~1KB | Core S3 only | Speaker (for WAV) | **If speaker done** |
| A.9 | Multi-touch gestures | Medium | Medium | ~32B | Core S3 only | Nothing | **Optional** |
| A.10 | GPIO expansion (NeoPixel etc.) | Varies | Varies | Varies | Waveshare pads / Core S3 Grove | Hardware connected | **On demand** |
| B.1 | ESP-NOW local mesh | Medium | Very High | ~3KB | Yes | Nothing | **Yes** |
| B.2 | Cloud group management | Medium | High | ~0 (server-side) | N/A | Nothing | **Yes** |
| B.3 | Synchronized expressions | Medium | High | ~0 | Yes | RTC (A.1) + Groups (B.2) | **Yes** |
| B.4 | Enhanced sync protocol | Low | Medium | ~1-3KB transient | Yes | Nothing | **Yes** |
| B.5 | Choreographed sequences | Medium | High | ~2KB | Yes | Groups (B.2) + Sync (B.3) | **Yes** |
| B.6 | Fleet dashboard | Medium | High | ~0 (server-side) | N/A | Groups (B.2) | **Yes** |
