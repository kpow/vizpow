# vizBot 3.0 — StackChan Flagship Planning

**Status:** In planning
**Started:** May 21, 2026
**Current production version:** `vizbot-m5cores3-v2.2.1`
**Target next version:** `vizbot-stackchan-v3.0.0` (new flagship) + `vizbot-m5cores3-v3.0.0` (bumped existing)

---

## Premise

M5Stack StackChan (K151-R) is the new flagship hardware target for vizBot. We keep our firmware, our face engine, our personalities, our WLED integration, our captive portal, our mesh — and add the StackChan body's capabilities (servo head, base LEDs, head touch, camera, etc.) on top.

We do NOT adopt M5's factory firmware (xiaozhi-esp32 fork). We pillage from it:
- SCS0009 servo driver code (accepting GPL-3.0 contamination on the StackChan binary only)
- Servo timing constants and Y-axis safety clamp (5°–85°)
- PY32L020 IO expander init for VM_EN
- GC0308 camera driver + JPEG capture pipeline
- Head angle control endpoints and safety logic
- Si12T touch gesture thresholds (study, build fresh)

We explicitly do NOT pillage in 3.0:
- Voice/audio pipeline (deferred indefinitely)
- MCP device server (deferred until LLM activation)
- ESP-NOW remote (deferred to 3.1)
- Motion detection via camera (deferred to 3.1)
- iOS app / Go server / XiaoZhi cloud protocol (rejected — vizBot has its own better-fitting equivalents)

---

## Planning status

All 15 planning questions resolved. See "Decisions made" below for each. Summary table and phase plan at end of document.

---

## Decisions made

### Naming & versioning
- **vizBot 3.0** is the umbrella for this work.
- Existing `vizbot/` folder in the vizpow monorepo continues to be the home — no firmware fork.
- Per-board version scheme stays: `vizbot-{board}-vX.Y.Z`.
- StackChan target build flag: `BOARD_HAS_STACKCHAN_BASE` (additive on top of `BOARD_M5CORES3`)
- Existing `BOARD_M5CORES3` continues to be supported; gets the 3.0 architectural changes too.

---

## Resolved questions

1. ✅ Name and scope — vizBot 3.0, in-place upgrade of `vizbot/` folder
2. ✅ Build system — already on PlatformIO; no migration concern
3. ✅ Board target model — Option B: `BOARD_M5CORES3` base + `BOARD_HAS_STACKCHAN_BASE` extension flag. Separate binaries maintained for bare CoreS3 vs CoreS3-in-StackChan-body. Only base-specific code (servos, IO expander, base LEDs, head touch) gated by extension flag.
4. ✅ Servo motion philosophy — **in scope for 3.0:** emotion-driven (TweenManager extension), command-driven (HTTP API), idle behavior (drift/doze), touch-reactive (head pat response). **Deferred to 3.x:** IMU look-toward, sound-reactive (dual-mic direction), remote-driven (ESP-NOW), audio-sync dancing. Priority stack when sources conflict: explicit commands > reactions > idle.
5. ✅ Base LED (12x WS2812C) role — **Two roles, user-switchable:** (A) Mood ring tied to emotion/personality (default, ambient); (D) Glow mode — user-selectable effect library decoupled from emotion (breathing, chase, fire, twinkle, etc.). **TBD:** menu placement — top-level mode alongside viz/info/clock/bot, or sub-selection under bot mode. Defer to web UI / menu design pass.
6. ✅ Head touch panel (Si12T) — **Option B: dedicated "pet the bot" gesture surface.** Decoupled from menu navigation. Single pat fires reaction + speech bubble + base LED flash, with personality-specific responses (Chill purrs, Hyper goes wild, Grumpy annoyed). **Special gesture:** double-tap on center pad → smooth tween back to home position (yaw 0°, pitch neutral). Detection: two center presses within ~400ms with a release between. Single-tap pet reactions held in suspense during the double-tap window (accepted ~400ms latency cost). Recenter motion personality-varied (Chill smooth, Hyper snap+overshoot, Grumpy reluctant pause).
7. ✅ ESP-NOW remote — **deferred to 3.1.** Approach: A2 (reflash bundled StickC-Plus with vizBot remote firmware, use vizBot's own ESP-NOW packet format that fits existing mesh patterns). When implementing, study M5's `remote/` directory (`github.com/m5stack/StackChan/remote`) as reference for peer discovery, polling cadence, JoyC dead-zone handling, and remote-side display layout. Don't adopt their packet format — adopt their patterns.
8. ✅ MCP device server — **deferred (Option C).** Defer until LLM/voice activation is actually being pursued. Rationale: demo-grade value without vizCloud isn't worth implementation cost; MCP is purely additive and can land anytime. **Constraint on 3.0 HTTP API design:** new endpoints (servo, base LEDs, head touch config) must be designed with future MCP-wrapping in mind — single-purpose endpoints, named like tools (`/bot/head/set_angles` not `/bot/control?mode=...`), JSON in/out, clear side-effect semantics. This costs nothing now and makes the eventual MCP server a wrapper-only task instead of an API rewrite.
9. ✅ Voice — **deferred indefinitely (Option C).** Framing: voice is inevitable long-term given where LLM-driven devices are heading, but not on the vizBot roadmap right now and not what 3.0 is about. When it comes back, it'll be a deliberate project. **3.0 design constraints from this decision:** don't build anything that pre-supposes voice landing soon. No "voice mode" placeholder in the menu. No reserved audio buffers / memory regions for voice. No pre-emptive libopus port or ES7210 mic wiring. DO keep API/MCP surface designs voice-friendly so it can plug in cleanly later (same principle as Q8). Leave the door open, don't pour a foundation.
10. ✅ SCS0009 driver — **lift from M5's factory firmware** (`firmware/main/boards/stackchan/` in `github.com/m5stack/StackChan`). Accept GPL-3.0 contamination on the StackChan build target only. Isolate via `BOARD_HAS_STACKCHAN_BASE` guards so other board targets (1.69, Matrix, bare CoreS3) stay license-clean. Rationale: project is hobby/giveaway with source already public on GitHub — GPL's "must provide source" obligation is satisfied by the existing public repo. Code is well-tested, motion is good and safe per kpow's hands-on testing. Rewriting from scratch saves no real value.
    - **Required housekeeping items spawned by this decision:**
      - Add a LICENSE file at vizpow repo root (recommend MIT for permissiveness + hobby fit)
      - Add `THIRD_PARTY_LICENSES.md` (or similar) documenting vendored GPL components
      - Note GPL-3.0 obligation on StackChan binary in the StackChan target's README section
      - Preserve original copyright headers in all vendored servo files
      - Cite both mongonta0716 (original) and M5Stack (port) in attribution
11. ✅ StackChan base hardware scope for 3.0:
    - **NFC** — skipped, defer indefinitely
    - **IR** — skipped, defer indefinitely
    - **Camera (GC0308)** — **IN SCOPE for 3.0** (revised from earlier plan)
      - Lift camera driver + JPEG capture pipeline from factory firmware (same GPL caveat as servo driver, same `BOARD_HAS_STACKCHAN_BASE` isolation)
      - Lift head angle control + safety clamps from factory firmware (bundled with SCS0009 work)
      - Build fresh: photo storage, web UI gallery, web UI head control
      - **Motion detection: DEFERRED** to 3.1 or later. Cleaner scope; self-contained add-on, doesn't change reaction priority stack
      - **Photo storage:** LittleFS to start (kpow doesn't have microSD yet); design as thin abstraction (`save_photo`, `list_photos`, `read_photo`, `delete_photo`) so backend can swap to microSD later without changing web UI code
        - Practical constraints: ~4-6 MB available on CoreS3 LittleFS after firmware/OTA partitions, GC0308 JPEG ~15-25 KB per photo at quality 10, ~200-400 photo capacity before pruning. LRU or "max N" eviction policy required.
      - **Web UI head control:** both sliders AND preset buttons
        - 5 preset buttons (Left/Up/Center/Down/Right), one-tap tween to fixed angles
        - Yaw slider (-180° to +180°), Pitch slider (5° to 85°)
        - Commit-on-release for sliders (avoid HTTP flood)
        - "Take Photo" button colocated with controls
      - **Future (post-3.0):** photo sharing between bots via vizCloud — design photo storage layer with a "new photo captured" hook point so vizCloud uploader can attach later without rework
12. ✅ Boot sequence — **Option A: each subsystem its own boot stage.** Guiding principle from kpow: stability and simplicity over speed. 9 stages on bare CoreS3 (unchanged from 2.x), ~17 stages on StackChan builds.
    - **New stages added under `BOARD_HAS_STACKCHAN_BASE`:**
      - PY32L020 IO expander (I2C 0x6F)
      - VM_EN servo power rail enable
      - SCS0009 Servo X (yaw) — ping, verify, read position
      - SCS0009 Servo Y (pitch) — ping, verify, read position
      - WS2812C base LED strip (12 LEDs, IO expander IO14)
      - Si12T head touch panel (I2C 0x68)
      - INA226 battery monitor (I2C 0x41)
      - GC0308 camera (capture test frame)
      - LittleFS photo storage (mount, free space check, writable test)
    - **Rationale for A over hybrid:** each stage is a flat single-concern init function — no grouping logic, no detail extraction, simplest implementation per stage. Failure is precisely diagnostic ("Servo Y failed" beats "Base I/O failed"). Boot time cost (~2-3s) accepted explicitly under the stated principle. Critical for hand-off to friends who can read a boot screen back to kpow during remote debugging.
    - **Conditional rendering:** boot stages gated on `BOARD_HAS_STACKCHAN_BASE` — bare CoreS3 boot is unchanged from 2.x. No surprise for existing users.
13. ✅ Camera framing / preview — **Option B: bot's LCD shows live preview in dedicated camera mode.** Web UI does NOT show video stream (avoids MJPEG/streaming infrastructure complexity).
    - **New "camera" mode** added to mode menu, StackChan-only (gated on `BOARD_HAS_STACKCHAN_BASE`)
    - LCD shows live preview from GC0308 in RGB565 at native 320x240, target ~10-15 fps
    - **Web UI head control** (sliders + preset buttons) drives head positioning while in camera mode — separation of concerns: web UI = control, bot LCD = viewfinder
    - **Shutter triggers:** web UI "Take Photo" button AND head center single-tap (pet-reaction affordance is suspended in camera mode and replaced with shutter — pet reaction makes no sense during framing)
    - **Double-tap center still recenters head** in camera mode (continues to be useful for framing)
    - **LCD overlay** while in camera mode (minimal, doesn't obscure subject):
      - Current head yaw/pitch readout in a corner
      - Battery percentage
      - "Tap head to capture" hint
      - Small center reticle/crosshair
    - **Tradeoff explicitly accepted:** cannot frame remotely (must be in line-of-sight of bot LCD). The "control from anywhere" web UI affordance is scoped back specifically for camera framing. Face-as-identity is also paused during camera mode — accepted because the mode is opt-in and exits cleanly.
    - **Exit:** returning to previous mode restores face and resumes normal pet-reaction behavior on head touch.
14. ✅ vizCloud integration — **minimum viable hooks only for 3.0; real integration deferred to vizCloud's own release cycle.**
    - **3.0 must do:**
      - Continue reporting whatever 2.x reports (MAC, IP, firmware version, mode, etc. — no regressions)
      - Bump firmware version string per board target (`vizbot-stackchan-v3.0.0`, `vizbot-m5cores3-v3.0.0`)
      - Keep existing HTTP API surface shape-compatible (new endpoints additive only)
    - **3.0 does NOT do:**
      - No capability manifest endpoint (`/bot/capabilities`) — deferred to first vizCloud release
      - No bot-to-bot photo sharing (deferred; storage-layer hook point from Q11 is sufficient)
      - No new heartbeat fields for new subsystems
      - No vizCloud-specific admin-control endpoints
      - No telemetry pipeline (personality, expression, usage data)
    - **Practical implication:** the new HTTP endpoints built for web UI consumption in 3.0 (head control, photo gallery, mode switching, base LED control) become "things vizCloud can call later for free" once it exists. Q8's MCP-readiness constraint (single-purpose, JSON in/out, tool-named) already enforces this. No additional firmware work needed for vizCloud during 3.0.
15. ✅ Documentation — **Option A: single comprehensive README**, optimized as agent-readable technical reference (not user onboarding / marketing).
    - **Purpose:** README must be detailed enough to hand to an agent for feature planning. Optimization target is technical reference, not project welcome page.
    - **Required content categories:**
      - Complete board target matrix — every variant, hardware, build flags
      - Full peripheral inventory — I2C addresses, pin assignments, libraries used
      - Mode list — what each mode does, what subsystems it touches
      - HTTP API surface — endpoint, method, params, return shape (tabular)
      - Web UI page inventory — what each page controls
      - Architecture overview — main loop structure, task model, state location, rendering path, mode dispatch
      - Configuration system — what's persisted, where, what's runtime-mutable
      - Per-board build instructions and known gotchas
      - Per-board version history
      - Licenses section (in lieu of separate THIRD_PARTY_LICENSES.md)
    - **Explicitly excluded:**
      - Marketing language
      - Step-by-step new-user onboarding
      - Tutorial-style intros
      - Storytelling
    - **Structure:** heavy headings, TOC at top, consistent per-board formatting, tables for tabular data, short self-contained code examples
    - **Process:** README updated continuously during 3.0 dev — every phase's PR includes README updates in the same commit. No end-of-cycle doc sprint.
    - **Separate files retained:** `LICENSE` (MIT) at repo root because GitHub UI surfaces it specifically. All other docs collapse into README.

---

## Summary of all decisions

| # | Topic | Decision |
|---|---|---|
| 1 | Name & scope | vizBot 3.0, in-place upgrade of `vizbot/` folder |
| 2 | Build system | Already on PlatformIO — no migration concern |
| 3 | Board target model | `BOARD_M5CORES3` base + `BOARD_HAS_STACKCHAN_BASE` extension flag, separate binaries |
| 4 | Servo motion sources (3.0) | Emotion, command, idle, touch-reactive. Deferred: IMU, sound, remote, audio-sync |
| 5 | Base LEDs role | User-switchable mood ring (A) OR glow library (D) |
| 6 | Head touch | Pet-the-bot gesture surface (B). Double-tap center = recenter head. |
| 7 | ESP-NOW remote | Deferred to 3.1, A2 approach (reflash StickC-Plus), pillage M5's `remote/` patterns |
| 8 | MCP device server | Deferred (Option C) until LLM activation. 3.0 API surface designed MCP-friendly. |
| 9 | Voice | Deferred indefinitely (Option C). Inevitable long-term, not in 3.0 scope. |
| 10 | SCS0009 driver | Lift from M5's factory firmware. Accept GPL-3.0 on StackChan binary, isolate via flag. |
| 11 | Base hardware scope | Camera IN (lifted from M5). Motion detection, NFC, IR deferred. LittleFS storage. Web UI head control: sliders AND buttons. |
| 12 | Boot sequence | Option A — each subsystem its own boot stage. Stability/simplicity over speed. |
| 13 | Camera framing display | Option B — bot's LCD shows live preview in new camera mode. With overlay (yaw/pitch, battery, hint, reticle). |
| 14 | vizCloud integration | Minimum viable hooks only. Real integration deferred to vizCloud's release cycle. |
| 15 | Documentation | Single comprehensive README, agent-readable technical reference style. LICENSE separate at root. |

---

## Provisional phases (now informed by all decisions)

**Phase 0 — Housekeeping (pre-3.0 work, ~1 week)**
- Add `LICENSE` file (MIT) at repo root
- Add "Licenses" section to README explaining GPL-3.0 obligation that will apply to StackChan build target once lifted code lands
- Bump version strings to `vizbot-{board}-v3.0.0-dev` per board
- Restructure README toward agent-readable technical reference format (per Q15)

**Phase 1 — Minimum Viable StackChan (early flashable milestone, ~1 week)**

Goal: a `.bin` that can be flashed to a StackChan today and runs vizBot with face / personality / WLED / modes all working, even though the new peripherals are not yet wired up. This is the "vizBot lives in a StackChan body" moment — proves the build works on the hardware, gives kpow a real device to iterate against, surfaces any architectural surprises early.

- Add `BOARD_HAS_STACKCHAN_BASE` build flag to `platformio.ini`
- Create the StackChan build environment (CoreS3 base + extension flag)
- Add all new peripheral subsystems as **stubs** that report "not initialized" or "deferred" in boot diagnostics — present but inert
- Add boot stages per Q12 Option A: each new subsystem stage exists but is a placeholder that always reports "deferred" (or "stub") — this gets the visual boot flow right early without committing to real init code
- **What actually works:** existing face engine, all 25 expressions, personalities, WLED forwarding, captive portal, mesh, info/clock/viz modes, web UI for existing controls
- **What is intentionally deferred to Phase 2+:** servos do nothing, base LEDs dark or off, head touch ignored, camera not active
- README updated to document the new build target and the "stub" status of each subsystem
- Tag this milestone as `vizbot-stackchan-v3.0.0-alpha.1` so it's identifiable

**Exit criteria for Phase 1:**
- `pio run -e stackchan` builds without errors
- Resulting `.bin` flashes to StackChan hardware and boots cleanly
- vizBot face shows on LCD
- Existing modes (viz/info/clock/bot) all work
- WiFi captive portal works
- Boot diagnostic screen shows all new subsystems with clear "stub/deferred" status
- No crash, no watchdog reset, no I2C hang from missing peripherals
- Bare CoreS3 build (no extension flag) still works identically to 2.x

**Phase 2 — StackChan hardware bring-up (~3-4 weeks)**

Goal: replace each stub with real driver code. After Phase 2, the StackChan body's peripherals all function but aren't yet integrated into the personality / expression system.

- Lift SCS0009 servo driver from M5 factory firmware (replace servo stub)
- PY32L020 IO expander driver + VM_EN gating (replace IO expander stub)
- Si12T head touch driver — raw event reporting first, no semantics yet (replace touch stub)
- WS2812C base LED strip — basic FastLED init + simple test pattern (replace LED stub)
- INA226 battery monitor integration (replace battery monitor stub)
- Y-axis safety clamp (5°–85°) baked into servo abstraction
- Each subsystem's boot stage flips from "stub" to real init with pass/fail
- HTTP endpoints added for each peripheral so they can be exercised individually from the web UI for debugging (single-purpose, JSON, MCP-friendly per Q8)
- README updated continuously per subsystem
- Tag `vizbot-stackchan-v3.0.0-alpha.2` when all peripherals function in isolation

**Phase 3 — Servo-augmented expressivity (~1-2 weeks)**
- Tie head motion to existing emotion/personality system via TweenManager
- Personality-driven idle behavior (Hyper twitchy, Chill calm, Grumpy still)
- Head touch single-tap pet reactions (personality-specific)
- Head touch double-tap center → home position (with personality-flavored recenter motion)
- Base LED mood ring mode (tied to emotion/personality)
- Base LED glow library mode (decoupled from emotion, user-selectable effects)
- Priority stack: explicit commands > reactions > idle
- Tag `vizbot-stackchan-v3.0.0-beta.1`

**Phase 4 — Camera + photo gallery (~1 week)**
- Lift GC0308 camera driver + capture pipeline from M5 factory firmware
- LittleFS photo storage with thin abstraction layer (`save_photo`, `list_photos`, `read_photo`, `delete_photo`)
- New "camera" mode on LCD with live RGB565 preview + overlay (yaw/pitch, battery, hint, reticle)
- Shutter triggers: web UI button + head center single-tap (pet reaction suspended in camera mode)
- Web UI gallery view (thumbnail grid, download, delete)
- Web UI head control panel (sliders + preset buttons + Take Photo)
- "New photo captured" hook point for future vizCloud uploader (no-op now)
- LRU or max-N photo eviction policy
- Tag `vizbot-stackchan-v3.0.0-beta.2`

**Phase 5 — Polish and ship (~1 week)**
- Final README pass
- Per-board version stamps to `v3.0.0`
- Burn-in testing on actual hardware (both bare CoreS3 and CoreS3-in-StackChan-body)
- Tag and release `vizbot-stackchan-v3.0.0` and `vizbot-m5cores3-v3.0.0`

**Total: ~8-9 weeks of focused work for 3.0**
**First flashable milestone: ~2 weeks in (after Phase 0 + Phase 1)**

---

## Explicitly deferred / out of scope for 3.0

- ESP-NOW remote support → 3.1
- Motion detection via camera → 3.1 or later
- MCP device server → whenever LLM activation begins
- Voice/audio pipeline → indefinitely
- Bot-to-bot photo sharing → vizCloud roadmap
- NFC, IR → indefinitely
- IMU look-toward head movement → 3.1
- Sound-reactive head tracking → indefinitely
- Capability manifest endpoint → first vizCloud release
- Migration guide for 2.x users → not built; commit history is sufficient

---

## Open items requiring future decision

These came up during planning but weren't blockers for 3.0 scope. To be resolved during implementation:

1. **Glow mode menu placement** — top-level mode alongside viz/info/clock/bot/camera, or sub-selection under bot mode? (Per Q5)
2. **Exact home position for head double-tap recenter** — yaw 0°, but what pitch? (Q6) Probably ~45° (middle of safe range 5°-85°) but verify on hardware.
3. **Specific pet-reaction mappings per personality per zone** — Chill/Hyper/Grumpy × left/center/right pad combinations. To be designed during Phase 2.
4. **LittleFS photo eviction policy** — LRU vs max-N vs both. Decide during Phase 3 based on real capture/usage patterns.
5. **Exact base LED effects in glow library** — start with breathing, chase, fire, twinkle; add more during dev.
6. **Slider commit-on-release vs live update behavior** for head control web UI — start with commit-on-release per Q11 decision; revisit if it feels laggy in practice.

