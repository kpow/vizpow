# Display Upgrade Plan — Modular Approach

## Context
The bot face, speech bubble, and ambient effects are all rendered with aliased Arduino_GFX primitives. Before adding more eye/mouth shapes and reworking the speech bubble, we should upgrade the graphics foundation. Each module below is independent and can be implemented on its own.

---

## Module 1: LovyanGFX Migration
**Goal:** Replace Arduino_GFX with LovyanGFX for TARGET_LCD. CoreS3 already uses it via M5Unified.

**Why first:** Every subsequent module benefits from AA primitives and DMA transfers.

### Changes
- **vizbot/display_lcd.h** — Replace `Arduino_DataBus` / `Arduino_GFX` init with `LGFX` class config for ST7789V2 (SPI pins from config.h). Replace `Arduino_Canvas` with `LGFX_Sprite` for double-buffering.
- **vizbot/display_lcd.h (DisplayProxy)** — Flip the abstraction: currently DisplayProxy wraps M5Unified → Arduino_GFX API. After migration, TARGET_LCD and TARGET_CORES3 both speak LovyanGFX natively, so DisplayProxy simplifies or disappears.
- **vizbot/config.h** — Remove `Arduino_GFX_Library.h` include for TARGET_LCD, add `LovyanGFX.h`.
- **All drawing call sites** — API is mostly compatible (`fillRect`, `fillCircle`, `drawLine` exist in both). Key renames:
  - `fillEllipse()` → `fillEllipseAA()` (AA version)
  - `drawArc()` → `drawSmoothArc()` (AA version)
  - `fillTriangle()` → `fillTriangleAA()` if available
  - Color format stays RGB565, no change needed
- **Font rendering** — Switch from bitmap font to VLW smooth font for speech bubble text. Generate a VLW font file (or use built-in) and load via `loadFont()`.
- **DMA** — Enable SPI DMA in LGFX config (`_cfg.dma_channel = SPI_DMA_CH_AUTO`). Canvas `pushSprite()` uses DMA automatically.

### Files Modified
- `vizbot/config.h` — library includes
- `vizbot/display_lcd.h` — LCD init, canvas, DisplayProxy
- `vizbot/bot_eyes.h` — AA ellipse/circle calls
- `vizbot/bot_faces.h` — AA arc/line calls for mouth/brows
- `vizbot/bot_overlays.h` — AA rounded rect, smooth font for speech bubble
- `vizbot/touch_control.h` — AA button rendering
- `vizbot/effects_ambient.h` — can keep as-is (pixel fills don't need AA)

### Dependencies
- None (first module)

### Verification
- Bot face renders with visibly smoother edges on TARGET_LCD
- CoreS3 target still compiles and works (should be unaffected)
- TARGET_LED still compiles (no LCD code involved)
- Speech bubble text is anti-aliased
- Frame rate stays at ~30 FPS or improves (DMA)
- Check heap/PSRAM usage before and after

---

## Module 2: Tween/Animation System
**Goal:** Replace scattered manual lerp code with a reusable tween utility.

### Current Problem
`bot_eyes.h` and `bot_faces.h` have manual lerp logic like:
```cpp
currentParam = currentParam + (targetParam - currentParam) * 0.15;
```
Repeated for every animated property. No easing curves, no sequencing.

### Design
A single-header `tween.h` utility:

```cpp
struct Tween {
  float* target;      // pointer to value being animated
  float from, to;
  unsigned long startMs, durationMs;
  EaseType easing;    // LINEAR, EASE_OUT_CUBIC, EASE_IN_OUT, BOUNCE, etc.
  bool active;

  void start(float* val, float toVal, unsigned long durMs, EaseType ease = EASE_OUT_CUBIC);
  bool update();      // call each frame, returns true while active
};

// Manager to update all active tweens
struct TweenManager {
  static const int MAX_TWEENS = 16;
  Tween tweens[MAX_TWEENS];
  void update();      // call once per frame
  Tween& animate(float* val, float toVal, unsigned long durMs, EaseType ease);
};
```

### Easing Functions (8 total)
LINEAR, EASE_IN_QUAD, EASE_OUT_QUAD, EASE_IN_OUT_QUAD, EASE_OUT_CUBIC, EASE_OUT_BOUNCE, EASE_OUT_ELASTIC, EASE_OUT_BACK

### Integration Points
- **bot_eyes.h** — pupil position, blink progress, eye size transitions
- **bot_faces.h** — expression parameter transitions (all the lerped values)
- **bot_overlays.h** — speech bubble pop-in/fade-out, notification slide-in

### Files
- `vizbot/tween.h` — NEW, ~100 lines
- `vizbot/bot_eyes.h` — replace manual lerp with TweenManager calls
- `vizbot/bot_faces.h` — replace expression transition lerp
- `vizbot/bot_overlays.h` — replace animation timing code
- `vizbot/vizbot.ino` — add `tweenManager.update()` in main loop

### Dependencies
- None (independent of Module 1, but nicer with it)

### Verification
- Expression transitions look smooth (same or better than current)
- Blink animation works correctly
- Speech bubble pop-in/fade-out preserved
- No jitter or timing issues
- Memory: ~200 bytes for TweenManager (16 tweens x ~12 bytes)

---

## Module 3: Enhanced Face Rendering
**Goal:** More eye/mouth shapes, bigger bolder speech bubble.

### New Eye Shapes (adding to existing 8)
- DIAMOND — rotated square shape
- HALF — half-closed/sleepy half-ellipse
- DOT — tiny dot pupils (shocked look)
- CURVED — crescent moon eyes (content/pleased)
- WINK — one eye open, one closed line
- GLITCH — offset/fragmented rectangles

### New Mouth Shapes (adding to existing 8)
- TEETH — grin with visible teeth segments
- TONGUE — open mouth with tongue
- ZIGZAG — jagged nervous mouth
- POUT — small pushed-out circle
- WHISTLE — tiny O shape
- FLAT_FROWN — straight angled line

### Speech Bubble Rework
- **Larger**: increase from current ~200px wide to ~220px, taller padding
- **Bold smooth font**: 18-24px anti-aliased font (via LovyanGFX VLW if Module 1 done, or larger bitmap font if not)
- **Rounded corners**: proper smooth rounded rect (AA if Module 1 done)
- **Optional semi-transparent background**: alpha-blended if LovyanGFX available
- **Word wrap**: support multi-line messages with proper line breaking
- **Tail/pointer**: small triangle pointing toward bot face

### Files
- `vizbot/bot_faces.h` — add new eye/mouth enums and expression definitions
- `vizbot/bot_eyes.h` — add rendering for new eye shapes
- `vizbot/bot_overlays.h` — reworked speech bubble rendering
- `vizbot/layout.h` — updated bubble dimensions/positioning

### Dependencies
- Best after Module 1 (AA primitives make shapes look good)
- Best after Module 2 (tweens for new shape transitions)
- CAN be done without them, just won't look as polished

### Verification
- All new eye/mouth shapes render correctly
- New expressions using new shapes work
- Speech bubble is visibly larger and bolder
- Word wrap works for longer messages
- WLED display still shows text correctly (separate 3x5 font, unaffected)

---

## Module 4: Dirty-Rect Rendering
**Goal:** Only redraw screen regions that changed. Reduce SPI bandwidth and CPU usage.

### Design
Track rectangular dirty regions per display element:

```cpp
struct DirtyRect {
  int16_t x, y, w, h;
  bool dirty;
  void mark(int16_t x, int16_t y, int16_t w, int16_t h);
  void clear();
};
```

### Strategy
- Each bot component (left eye, right eye, mouth, brows, bubble, time overlay) owns a DirtyRect
- On state change (expression switch, blink, pupil move), mark that component's rect dirty
- Render pass: only redraw dirty rects + push only those regions to LCD
- When background is ambient effect: full-frame redraw (effects change every pixel every frame), dirty-rect disabled
- When background is static black: dirty-rect gives big savings

### Files
- `vizbot/dirty_rect.h` — NEW, ~50 lines
- `vizbot/bot_mode.h` — integrate dirty tracking into render loop
- `vizbot/bot_eyes.h` — mark eye regions dirty on pupil/blink change
- `vizbot/bot_overlays.h` — mark overlay regions dirty on show/hide
- `vizbot/display_lcd.h` — partial pushSprite support

### Dependencies
- Module 1 helps (LovyanGFX `pushSprite(x,y,w,h)` supports partial push natively)
- Independent otherwise

### Verification
- Bot face looks identical (no rendering artifacts at dirty rect boundaries)
- Measurable FPS improvement on static-background bot mode
- Ambient effect mode unaffected (full redraw path preserved)
- No visual glitches during expression transitions

---

## Module 5: Scene Graph (Future/Optional)
**Goal:** Display-list pattern for cleaner rendering architecture.

### This module is speculative — only worth doing if Modules 1-4 reveal the need.

### Concept
```cpp
struct DisplayNode {
  int16_t x, y, w, h;
  float opacity;
  bool visible;
  DisplayNode* children[8];
  uint8_t childCount;
  DirtyRect bounds;

  virtual void render(LGFX_Sprite* canvas);
  void addChild(DisplayNode* child);
};

// Concrete nodes
struct EyeNode : DisplayNode { ... };
struct MouthNode : DisplayNode { ... };
struct BubbleNode : DisplayNode { ... };
```

### Benefits
- Natural z-ordering (background → face → overlays)
- Dirty-rect tracking per node (integrates with Module 4)
- Tween targets are node properties (integrates with Module 2)
- Adding new face elements = add a node, not modify render loop

### Risk
- Over-engineering for current scope
- Only pursue if Module 3 (enhanced face) reveals the render loop is getting unwieldy

---

## Suggested Implementation Order

```
Module 1 (LovyanGFX)  ←  Do first, foundation for everything
    ↓
Module 2 (Tweens)      ←  Small, independent, immediate code cleanup
    ↓
Module 3 (Face/Bubble) ←  The actual feature work you want
    ↓
Module 4 (Dirty-Rect)  ←  Performance polish
    ↓
Module 5 (Scene Graph)  ←  Only if needed
```

Modules 1 and 2 can be done in either order or in parallel. Module 3 is the payoff. Module 4 is optimization. Module 5 is future-proofing.
