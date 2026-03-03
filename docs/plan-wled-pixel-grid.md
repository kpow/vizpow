# Plan: Direct Pixel Grid Control for WLED Display

## Context

Currently, VizBot sends **text strings** to a WLED-controlled 32x8 LED matrix via HTTP POST using WLED's built-in scrolling text effect (FX 122). WLED renders the font internally — the ESP32 has no control over what appears pixel-by-pixel.

This plan replaces that with **direct pixel control**: the ESP32 renders its own 32x8 pixel buffer and transmits raw RGB data to WLED via the DDP (Distributed Display Protocol) over UDP. This gives full creative freedom — custom fonts, pixel art, sprites, animations, anything.

## Approach: DDP Protocol over UDP

**DDP wins over the HTTP JSON `"i"` array approach:**

| | HTTP JSON `"i"` | DDP (UDP) |
|---|---|---|
| Payload | ~3000 bytes (hex strings + JSON) | ~778 bytes (10-byte header + 768 RGB) |
| Latency | ~20-50ms (TCP + JSON parsing) | ~1-3ms (single UDP packet) |
| Animation | Impractical above 5 FPS | Comfortable at 30+ FPS |
| Stack usage | ~3KB buffer for JSON building | ~10 bytes (zero-copy send) |

DDP is first-class in WLED (enabled by default since 0.13). WLED auto-enters "realtime mode" when it receives DDP frames and auto-resumes its previous effect when frames stop (2.5s timeout).

## Memory Impact

| Component | Size | Where |
|---|---|---|
| Pixel buffer | 768 bytes | BSS (global RAM) |
| 3x5 font data | 285 bytes | PROGMEM (flash) |
| DDP code | ~500 bytes | Flash (code) |
| WiFiUDP object | ~16 bytes | BSS |
| **Total RAM** | **~784 bytes** | Out of ~512KB available |
| **Total flash** | **~785 bytes** | Out of ~4MB available |

No partition adjustments needed. Net RAM increase is ~600 bytes (replacing old JSON buffer code).

## Files to Change

### 1. New: `vizbot/wled_font.h` — 3x5 pixel font + rendering

- PROGMEM bitmap font for printable ASCII (95 chars × 3 bytes = 285 bytes flash)
- 3 pixels wide × 5 pixels tall per character; at 4px pitch, fits **8 chars** in 32px width
- Text is vertically centered on the 8-pixel-tall display (1px top padding, 5px glyph, 2px bottom)
- Functions: `wledFontDrawChar()`, `wledFontDrawString()`, `wledFontStringWidth()`
- Renders into a `uint8_t[]` RGB buffer, no FastLED dependency

### 2. Modify: `vizbot/wled_display.h` — Pixel buffer + DDP transport

**Add to struct `WledDisplayData`:**
- `uint8_t pixelBuffer[768]` — 256 pixels × 3 bytes RGB (global/BSS, not stack)
- `WiFiUDP udp` + `uint8_t ddpSequence`
- `uint16_t frameDurationMs`

**Add pixel drawing functions (called from Core 1):**
- `wledPixelClear()` / `wledPixelSet(x,y,r,g,b)` / `wledPixelFill(r,g,b)`
- `wledPixelDrawText(text, r,g,b)` — renders centered text via `wled_font.h`

**Add DDP transport (called from Core 0):**
- `wledSendDDP()` — builds 10-byte header, writes header + pixelBuffer via `WiFiUDP` (zero-copy, ~10 bytes stack)

**Refactor the queue system:**
- Add `WLED_FRAME_REQUESTED` to `WledSendState` enum
- New `wledQueueFrame(durationMs)` — signals Core 0 to send pixel buffer
- **Refactor `wledQueueText()`** to render text into pixel buffer then call `wledQueueFrame()` — all existing callers (`speechBubble.show()`, `handleWledTest()`) continue working unchanged
- Modify `pollWledDisplay()` to handle `WLED_FRAME_REQUESTED` state: send DDP, manage hold timer, restore via existing HTTP mechanism

**State save/restore:** Keep existing HTTP-based `wledCaptureState()` + restore POST. Capture happens once on first frame, restore happens once when hold expires. HTTP restore forces WLED out of realtime mode instantly (vs waiting 2.5s DDP timeout).

### 3. No changes needed

- `bot_overlays.h` — Still calls `wledQueueText()`, works as before
- `web_server.h` — `handleWledTest()` still calls `wledQueueText()`, works as before
- `task_manager.h` — WiFi task stack (6144 bytes) is ample for zero-copy DDP
- `bot_sayings.h`, `bot_mode.h`, `vizbot.ino`, `config.h` — untouched

## DDP Protocol Details

One UDP packet per frame to port 4048:

```
Header (10 bytes):
  Byte 0: 0x41 (version 1 + push flag)
  Byte 1: Sequence number (0-15, wrapping)
  Byte 2: 0x01 (RGB, 8-bit per channel)
  Byte 3: 0x01 (device ID)
  Bytes 4-7: 0x00000000 (data offset, big-endian)
  Bytes 8-9: 0x0300 (data length = 768, big-endian)

Pixel Data (768 bytes):
  3 bytes per pixel: R, G, B
  256 pixels in row-major order (pixel 0 = top-left)
```

Total packet: 778 bytes — well within UDP MTU of 1472 bytes.

## Implementation Steps

1. Create `wled_font.h` with 3×5 PROGMEM font data and render functions
2. Add pixel buffer + drawing functions to `wled_display.h`
3. Add DDP transport (`WiFiUDP` + `wledSendDDP()`)
4. Wire up new send path: `WLED_FRAME_REQUESTED` enum, `wledQueueFrame()`, refactored `wledQueueText()`, updated `pollWledDisplay()`
5. Remove old FX 122 text-specific code (the "fx bounce through 0" workaround, etc.)
6. Test end-to-end

## Verification

- Upload to ESP32-S3-Touch-LCD board (TARGET_LCD)
- Connect to home WiFi where WLED 32x8 matrix is on the LAN
- Trigger bot sayings (shake, touch, idle) → confirm text appears on WLED matrix with correct centering and color
- Use web UI "Test WLED" button → confirm "Hello" renders
- Verify WLED's previous effect restores after display duration
- Verify WLED unreachable gracefully backs off (disconnect WLED, check serial)
- Test long words (6-8 chars) to verify they fit and render legibly with 3x5 font

## Future Expansion (out of scope, but enabled by this architecture)

- Send emoji sprites / pixel art to the WLED matrix
- Scrolling text animation (render wider-than-32px text, shift viewport)
- Weather icons + temperature display
- Continuous streaming mode for real-time animations
- Web UI pixel editor
