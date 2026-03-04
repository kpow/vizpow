# Stage 3: Core S3 Audio System & Sensors — Testing Checklist

**Branch:** `feature/stage3-cores3-sensors`
**Target:** `TARGET_CORES3` (M5Stack Core S3)

---

## Boot

- [ ] Boot screen shows "Sensors" stage with "Spkr+Mic+Prox" (or "no prox" if LTR-553 not found)
- [ ] Boot screen fits all 12 stages on screen without overlap
- [ ] Boot chime plays (C5→E5→G5 ascending triad) when face appears
- [ ] Greeting speech bubble appears with pop sound

## Speaker / Sound Effects

- [ ] **Tap** the screen → boop sound + expression change
- [ ] **Shake** the device → rattle sound (descending notes) + dizzy face
- [ ] **Speech bubbles** have a subtle pop sound when appearing
- [ ] **Expression changes** have a quick chirp
- [ ] Sounds are non-blocking — face animation stays smooth during playback
- [ ] `/bot/sound?freq=440&dur=500` plays a 440Hz tone for 500ms
- [ ] `/bot/volume?v=100` changes speaker volume (try 0 for mute, 255 for max)

## Microphone

- [ ] `/bot/mic` returns JSON with rms, smooth, peak values (not all zeros)
- [ ] **Clap loudly** near the device → surprised face + "Whoa!"/"Loud!" saying + reaction sound
- [ ] **Talk continuously** for ~2 seconds → focused expression (squinty, no mouth)
- [ ] **Wait 30+ seconds in silence** while idle → sleepy expression
- [ ] No feedback loop — sounds played by speaker don't trigger mic reactions
- [ ] 3-second cooldown between audio reactions (can't spam clap reactions)

## Proximity / Light

- [ ] **Move hand toward screen** (~5cm) → surprised or shy face + "Hi!" saying
- [ ] **Cover and uncover 3 times quickly** → happy face + "Peekaboo!"
- [ ] **Cover sensor for >2 seconds** → worried face + "Dark" saying
- [ ] Screen is full brightness (255) on boot — no auto-brightness
- [ ] `/state` JSON includes `"sensors"` object with proximity/lux readings

## Web API

- [ ] `/state` response includes `sensors.speaker`, `sensors.mic`, `sensors.proxLight`
- [ ] `/state` response includes `sensors.proximity` (number) and `sensors.lux` (number)
- [ ] `/bot/mic` returns live updating values when you make noise

## Settings Persistence

- [ ] Change volume via `/bot/volume?v=150`, reboot → volume stays at 150
- [ ] Screen stays full brightness across reboots (no stale dim NVS values)

## Non-regression

- [ ] Touch tap/swipe still works normally
- [ ] Shake still triggers dizzy + info mode on sustained shake
- [ ] WiFi AP works, web UI loads
- [ ] Cloud sync still works (if STA connected)
- [ ] Frame rate stays smooth (~30fps) with all sensors running
