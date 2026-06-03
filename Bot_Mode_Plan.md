# VizPow Bot Mode

Feature Planning Document

Target Platform: ESP32-S3-Touch-LCD-1.69 (TARGET_LCD only)

February 2026

# 1. Overview

Bot Mode is a new display mode for VizPow that renders an animated desktop companion character on the 240x280 LCD. Inspired by products like the Dasai Mochi (a dashboard robot with 70+ face animations and gyroscope reactions), Bot Mode brings a persistent animated character to the VizPow platform with deep integration into the existing effect and notification systems.

Bot Mode is LCD-only. It does not interact with the 8x8 LED matrix. The LED matrix continues to run its current mode (ambient/emoji) independently while Bot Mode occupies the LCD.

> *Scope: This document covers Bot Mode features only. The planned cloud-based application control system (server-client content management) is a separate initiative and is not detailed here, though Bot Mode should be architected to receive content updates from that future system.*

# 2. Target Hardware

Bot Mode requires the LCD and touch input. It runs exclusively on TARGET_LCD (ESP32-S3-Touch-LCD-1.69).

|               |                    |                                                |
|---------------|--------------------|------------------------------------------------|
| **Component** | **Spec**           | **Bot Mode Usage**                             |
| LCD Display   | 240x280 ST7789     | Character rendering, face animations, overlays |
| Touch         | CST816S capacitive | Pet/tap interactions, menu access              |
| IMU           | QMI8658 6-axis     | Reaction to tilt/shake/motion                  |
| LED Matrix    | 8x8 WS2812B        | Not used by Bot Mode (runs independently)      |
| WiFi          | ESP32-S3 built-in  | Web control, future cloud sync                 |

# 3. Character System

## 3.1 Face & Expression Engine

The core of Bot Mode is an expressive face rendered on the LCD. The face is composed of layered, independently animated components.

### Face Components

|               |                                                        |                                             |
|---------------|--------------------------------------------------------|---------------------------------------------|
| **Component** | **Description**                                        | **Animation Type**                          |
| Eyes          | Primary expression driver; shape, size, pupil position | Procedural with IMU pupil tracking |
| Eyebrows      | Mood modifier; angle and position                      | Lerped position/rotation                    |
| Mouth         | Secondary expression; open/closed/shape variants       | Procedural + interpolation                 |

### Eye Animation System

Eyes are the most important element. They need to feel alive. The eye system should support the following behaviors:

- **Idle look-around:** Eyes periodically glance in random directions with smooth easing. Configurable frequency (e.g., every 2-8 seconds).

- **Blink:** Regular blinking at natural intervals (every 3-7 seconds). Blink is a quick close-open sprite sequence (~150ms). Occasional double-blink.

- **Slow blink:** Drowsy/sleepy expression. Eyelids half-close, pause, then open slowly.

- **Squint:** Suspicious or focused look. Eyes narrow horizontally.

- **Wide eyes:** Surprise or excitement. Eyes enlarge with possible pupil dilation.

- **Motion tracking:** Pupil position shifts based on IMU tilt data. Tilt left = eyes look left. Subtle and continuous.

- **Shake reaction:** Hard shake triggers a startled wide-eye or dizzy spiral animation.

### Expression Presets

Predefined expression states that combine face component settings. Each expression defines eye shape, brow position, and mouth shape.

|                |                                   |                 |                           |                             |
|----------------|-----------------------------------|-----------------|---------------------------|-----------------------------|
| **Expression** | **Eyes**                          | **Brows**       | **Mouth**                 | **Notes**                   |
| Neutral        | Default round, centered pupils    | Relaxed         | Flat line or slight smile | Default idle state          |
| Happy          | Upward arcs (^ ^)                 | Raised          | Wide smile                |                             |
| Sad            | Droopy, downward gaze             | Angled inward   | Downturned                | Optional tear drop          |
| Angry          | Narrowed, sharp                   | V-shaped inward | Gritting/flat             | Optional vein pop overlay   |
| Surprised      | Wide circles, small pupils        | Raised high     | O-shape                   | Brief duration              |
| Sleepy         | Half-lidded                       | Relaxed low     | Slight open               | Triggers after idle timeout |
| Love           | Heart-shaped eyes                 | Raised          | Smile                     | Floating hearts effect      |
| Dizzy          | Spiral/X eyes                     | Uneven          | Wavy line                 | Post-shake reaction         |
| Thinking       | Looking up-right, one raised brow | Asymmetric      | Slight pucker             | Ellipsis bubble             |
| Excited        | Star/sparkle eyes                 | Raised          | Big grin                  | Bounce animation            |
| Mischievous    | Side-glance, one narrowed         | One raised      | Smirk                     |                             |

> *Expression count target: 12-20 expressions at launch. The Dasai Mochi Gen 3 ships with 70+ animations, so there is significant room to expand post-launch.*

### Sayings / Text Bubbles

The bot can display short text messages in a speech bubble overlay on the LCD. These are triggered by events, timers, or user interaction.

- **Display method:** Rendered text bubble above or beside the character face. Auto-sized to content. Appears with a pop-in animation, lingers for 3-5 seconds, then fades out.

- **Trigger types:** Scheduled (random from a pool), event-driven (shake, tap, time-of-day), or pushed from web UI / future cloud system.

- **Content:** Short phrases, 1-2 lines max given LCD width. Examples: greetings, quips, status messages, time display, notifications.

- **Storage:** Sayings stored as string arrays in PROGMEM. Categorized by context (greetings, reactions, idle chatter, time-based).

Example saying categories:

- Greetings: 'Hey!', 'Yo!', 'Sup?', 'Hiii~'

- Idle: 'I'm bored...', 'Whatcha doing?', '\*yawn\*', 'This is fine.'

- Reactions (shake): 'Whoa!', 'Easy!', 'AHHH!', 'I felt that.'

- Reactions (tap): 'Ow!', 'Hehe', 'That tickles', 'Poke.'

- Time-based: 'Good morning!', 'Lunch time?', 'Getting late...', 'Zzz...'

- Status: 'WiFi connected', 'Battery low', 'Mode changed'

# 4. VizPow Integration

## 4.1 Mode Architecture

Bot Mode becomes the 4th display mode for TARGET_LCD, alongside Motion, Ambient, and Emoji. The mode cycle via shake becomes: Motion > Ambient > Emoji > Bot > Motion. Bot Mode only controls the LCD. The LED matrix continues running whatever mode was active when Bot Mode was entered, or can be set independently via the web UI.

## 4.2 LCD Rendering

Bot Mode takes full control of the LCD when active. The rendering pipeline:

- **Background:** Solid color or simple animated pattern (breathing gradient, slow color shift). Should be configurable via palette selection.

- **Character layer:** Face components rendered center-screen using TFT drawing primitives. Procedural geometry only — no sprites.

- **Overlay layer:** Text bubbles, notification banners, time display, status icons. Drawn on top of character.

- **Transition:** Smooth fade or slide transition when entering/exiting Bot Mode from other modes.

## 4.3 Notification / Time Overlay

Bot Mode should support overlay elements that integrate with the broader VizPow system:

- **Time display:** Optional persistent clock in a corner of the LCD. Configurable format (12h/24h). Can be toggled on/off. Uses NTP when WiFi is connected, otherwise shows uptime.

- **Notifications:** Banner-style alerts that slide in from top or bottom. Used for: mode changes, WiFi status, shake events, or pushed messages from web UI / future cloud system. Auto-dismiss after a few seconds.

- **Slideshow slot:** Bot Mode can optionally participate in auto-cycle. When auto-cycle is on, Bot Mode gets periodic screen time in the rotation alongside ambient effects and emojis. Duration configurable.

- **Weather overlay:** Current temperature and a simple procedural weather icon (sun, cloud, rain, snow, storm) displayed in a corner or as a periodic notification banner. Fetched from a free weather API (e.g., Open-Meteo, no API key required). Polls on a configurable interval (e.g., every 30 minutes). Can also influence bot behavior — bot expression could react to weather (sleepy when rainy, happy when sunny). Togglable on/off.

> *Network requirement: Weather (and NTP time sync) require the ESP32 to be connected to an internet-capable WiFi network in STA (station) mode. In default AP mode (192.168.4.1), there is no internet access. STA mode configuration can be done via the web UI or will be handled by the future cloud application control system. When no internet is available, weather overlay is hidden and time falls back to uptime display.*

## 4.4 Touch Interactions

Since Bot Mode is LCD-only, touch input maps directly to character interactions:

- **Tap:** Triggers a reaction expression + saying. Random selection from reaction pool.

- **Long press:** Opens the existing touch menu (same as other modes). Bot-specific menu items could include: expression selector, sayings toggle, time overlay toggle.

- **Swipe (if detectable):** Could change expression or trigger a specific animation. Low priority, depends on CST816S gesture support.

## 4.5 IMU Reactions

Bot Mode uses the IMU for character reactions, similar to the Dasai Mochi gyroscope feature:

- **Tilt:** Eyes track tilt direction (pupils shift). Subtle, continuous. Also slight character body lean if rendering supports it.

- **Shake:** Triggers dizzy/startled expression. Strong shake = more dramatic reaction. Cooldown to prevent spam.

- **Idle detection:** If no motion for extended period (e.g., 5 minutes), bot transitions to sleepy expression and eventually 'falls asleep' (eyes closed, Zzz animation).

- **Wake:** Any motion after sleep state triggers a wake-up animation sequence.

# 5. Web Interface Changes

The web UI at 192.168.4.1 needs a new Bot tab and controls.

## 5.1 Bot Tab

- **Expression picker:** Grid of expression thumbnails. Tap to trigger that expression immediately.

- **Sayings input:** Text field to push a custom saying to the bot. Displays as a speech bubble for configurable duration.

- **Personality selector:** Preset personality profiles that adjust idle behavior, saying frequency, and expression distribution (e.g., 'Chill', 'Hyper', 'Grumpy', 'Sleepy').

- **Time overlay toggle:** On/off for persistent clock display.

- **Background palette:** Select background color/gradient from existing palette system.

## 5.2 API Endpoints (New)

|                                 |            |                                               |
|---------------------------------|------------|-----------------------------------------------|
| **Endpoint**                    | **Method** | **Description**                               |
| /bot/expression?v=N             | GET        | Set expression by index                       |
| /bot/say?text=MSG&dur=MS        | GET        | Display custom text bubble                    |
| /bot/personality?v=N            | GET        | Set personality preset                        |
| /bot/time?v=0|1                | GET        | Toggle time overlay                           |
| /bot/background?v=N             | GET        | Set background style/palette                  |
| /bot/weather?v=0|1             | GET        | Toggle weather overlay                        |
| /bot/weather/config?lat=N&lon=N | GET        | Set location for weather (latitude/longitude) |
| /bot/state                      | GET        | Return current bot state (JSON)               |

# 6. Implementation Plan

## 6.1 New Files

|                |                                                                                                           |
|----------------|-----------------------------------------------------------------------------------------------------------|
| **File**       | **Purpose**                                                                                               |
| bot_mode.h     | Bot Mode main loop, state machine, render pipeline, idle/sleep logic                                      |
| bot_faces.h    | Expression definitions, face component sprites, animation sequences                                       |
| bot_sayings.h  | Saying string pools (PROGMEM), category system, selection logic                                           |
| bot_eyes.h     | Eye animation engine: blink, look-around, tracking, pupil procedural movement                             |
| bot_overlays.h | Time display, notification banners, text bubble rendering, weather overlay (API fetch + procedural icons) |

## 6.2 Modified Files

|                 |                                                                                        |
|-----------------|----------------------------------------------------------------------------------------|
| **File**        | **Changes**                                                                            |
| vizpow.ino      | Add Bot Mode to mode enum and loop. Update shake cycle to include mode 3.              |
| config.h        | Add BOT_MODE constant (3). Add bot-related config (idle timeout, blink interval, etc.) |
| display_lcd.h   | Add bot rendering hooks. Bot Mode takes LCD control away from hi-res ambient.          |
| touch_control.h | Add tap/interaction handlers for Bot Mode. Add bot menu items.                         |
| web_server.h    | Add Bot tab HTML, bot API endpoints, bot state in /state JSON.                         |

## 6.3 Rendering Approach

All face rendering is procedural — drawn at runtime using TFT drawing primitives (filled circles, arcs, rounded rects, lines). No sprite sheets or pre-rendered bitmaps.

**Art Direction**

- Simple and bold. Up to 4 colors on a dark/black background. High contrast is critical for readability on the small 240x280 screen.

- Geometric shapes only: filled circles/ellipses for eyes, arcs for brows and mouth. No fine detail — it won’t read at this scale.

- Primary face color should be selectable from the existing palette system (default: white or cyan on black).

**How It Works**

- Each expression is a struct of numeric parameters: eye width, eye height, pupil offset, brow angle, brow y-position, mouth type, mouth width, mouth curve.

- Transitions between expressions by lerping all parameters over a configurable duration (e.g., 200-500ms).

- Render loop: clear screen (black), draw eyes, draw brows, draw mouth. Target 30+ FPS for smooth animation.

- Tiny memory footprint (~2KB for all expression parameter sets). No flash storage pressure.

> *A reference image (cartoon eye expressions sheet) is provided alongside this document. The following description codifies the target style for Claude Code implementation.*

**Visual Reference: Eye Style Specification**

The target aesthetic is a classic cartoon eye style — bold, simple, high contrast. White on black. The face is eyes-and-brows only in most expressions, with mouth added only when an expression demands it. On the 240x280 LCD, the eyes should fill most of the screen width.

**Eye Whites:**

- Large filled ellipses, white or near-white. Should dominate the screen.

- The two eyes overlap or touch at center — they form a connected white mass, not two separated circles. Draw as two overlapping fillEllipse calls. No visible gap between them.

- Eye shape deformation is the primary expression tool: squish vertically for sleepy/angry, stretch for surprise, make asymmetric for skeptical/mischievous.

**Pupils:**

- Small filled circles, dark/black. Roughly 20-25% of eye white width.

- Pupil position within the white area drives look direction. IMU tilt maps to pupil offset. This is the main source of “aliveness” — the pupils should always be subtly moving.

- Both pupils move together (same direction, same offset). They should never look in different directions except for the “dizzy” special state.

**Eyebrows:**

- Thick arcs, not thin lines. Dark/black fill. They float above the eyes with a small gap.

- Brow angle and curvature are the main mood modifiers. Angled inward = angry/suspicious. Raised = surprised/happy. Asymmetric (one up, one down) = skeptical/mischievous.

- Implement as thick drawArc or as a filled rounded rectangle rotated to the target angle.

**Special Eye Modes (not parameter-driven — mode switches):**

- **X-eyes:** Two thick crossed lines replace the eye whites. Used for “dead” or extreme anger.

- **Spiral eyes:** Spiral drawn inside each eye white. Used for dizzy (post-shake reaction). Can animate rotation.

- **Caret eyes (^ ^):** Upward arc/caret shapes replace the eyes. Used for happy/content squint. No pupil visible.

- **Heart eyes:** Heart shapes replace the eye whites. Drawn procedurally from two arcs + triangle. Used for love expression.

**Color Rules:**

- Background: solid black (0x0000).

- Eye whites: white (0xFFFF) or configurable from palette (cyan, green, etc.).

- Pupils and brows: black (0x0000) — same as background, so they “cut into” the white.

- Maximum 4 colors on screen at any time (including background). Keep it clean.

## 6.4 Memory Budget

ESP32-S3 has 4MB flash and 2MB PSRAM. Approximate allocations for Bot Mode:

|                                |                    |             |
|--------------------------------|--------------------|-------------|
| **Asset**                      | **Estimated Size** | **Storage** |
| Expression parameter sets (20) | ~2KB               | PROGMEM     |
| Saying strings (100+ phrases)  | ~4KB               | PROGMEM     |
| Bot state variables            | <1KB              | RAM         |

# 7. Development Phases

### Phase 1: Core Face Engine

- Implement bot_mode.h with basic state machine (active, idle, sleeping).

- Implement bot_eyes.h with procedural eye rendering: idle look-around, blink, IMU pupil tracking.

- Implement bot_faces.h with 5 initial expressions: Neutral, Happy, Sad, Surprised, Sleepy.

- Add Bot Mode to mode enum and shake cycle in vizpow.ino.

- Basic LCD rendering pipeline in display_lcd.h for Bot Mode.

### Phase 2: Interactions & Sayings

- Implement touch reactions (tap = expression + saying).

- Implement bot_sayings.h with categorized phrase pools.

- Implement bot_overlays.h for text bubbles.

- Add IMU shake/tilt reactions beyond basic eye tracking.

- Idle timeout -> sleepy -> sleep state machine.

- Expand to 12 expressions.

### Phase 3: Web UI & Overlays

- Add Bot tab to web_server.h with expression picker and custom saying input.

- Add new API endpoints.

- Implement time overlay (NTP or uptime).

- Implement notification banner system.

- Implement weather overlay (Open-Meteo API, procedural weather icons, STA mode detection).

- Personality presets.

- Auto-cycle integration (bot gets slideshow slot).

### Phase 4: Polish & Expansion

- Expand to 20+ expressions.

- Background themes/palettes.

- Tune animation timing, easing curves, idle behavior.

- Prepare API surface for future cloud-based content management integration.

# 8. Future Cloud Integration Notes

Bot Mode should be built with the awareness that a cloud-based application control system is planned separately. Architectural decisions to keep in mind:

- **API-first:** All bot features controllable via HTTP endpoints. This makes the transition to cloud push/pull straightforward.

- **Content separation:** Sayings, expression configs, and personality profiles stored in structured data (arrays/structs), not hardcoded in rendering logic. Future cloud system can update these.

- **State reporting:** /bot/state endpoint should return full bot state (current expression, active saying, personality, overlay settings) for cloud dashboard sync.

- **OTA-ready:** New content (sayings, expressions) should be receivable without firmware reflash. Consider JSON config loading from SPIFFS/LittleFS as an intermediate step before full cloud sync.

> *The cloud application control system (central web app for managing VizPow client content) is a separate planning effort. Bot Mode simply needs clean API boundaries and data-driven content to be ready for it.*

# 9. Constraints & Decisions

|                        |                                  |                                                                                   |
|------------------------|----------------------------------|-----------------------------------------------------------------------------------|
| **Item**               | **Decision**                     | **Rationale**                                                                     |
| LED matrix interaction | None. Bot Mode is LCD-only.      | LED matrix runs independently. Keeps Bot Mode self-contained and avoids coupling. |
| TARGET_LED support     | No. Requires LCD.                | TARGET_LED has no display for character rendering.                                |
| Sound                  | Not in scope.                    | No speaker/DAC on current hardware. Could be added later with external module.    |
| Bluetooth control      | Not in scope.                    | Planned separately on the VizPow roadmap.                                         |
| Cloud sync             | Not in scope (designed for).     | Separate planning doc. Bot Mode exposes APIs for future integration.              |
