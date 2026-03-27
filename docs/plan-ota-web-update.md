# Web-Based OTA Firmware Updates for vizBot

## Context
Friends receiving vizBot devices need a way to update firmware without USB cables or technical knowledge. WLED-style auto-update: the device checks GitHub for new versions, shows a notification, and the user just clicks "Update Now."

The project already has OTA partition metadata in `partitions.csv`, a web server on Core 0, and HTTPS client code (`viz_cloud_client.h`) — the main missing pieces are the update check, download, and UI.

## Key Decision: Dual OTA Partitions

**Switch from single to dual OTA slots.** Currently the partition table has one 3.9MB app slot with no rollback. If an OTA update fails mid-write, the device is bricked — friends can't recover via USB serial.

With dual OTA:
- Each slot is ~1.9MB (firmware is ~800KB-1.2MB, fits easily now)
- Failed updates automatically roll back to the previous working firmware
- NVS credentials survive (offset unchanged at 0x9000)
- LittleFS increased from 128KB to 256KB (preparing for future on-device content storage)

**Future work (separate PR):** Migrate PROGMEM icons/sprites/patterns to LittleFS files. This keeps firmware small as content grows and avoids hitting the 1.9MB slot limit. Not part of this OTA PR — it's a rendering pipeline change that needs its own testing.

**The first flash after this partition change must be via USB** since the layout itself is changing.

## Implementation Steps

### 1. Update `partitions.csv`
Replace single app slot with dual OTA layout:
```
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xe000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x1D0000,
app1,     app,  ota_1,   0x1E0000, 0x1D0000,
spiffs,   data, spiffs,  0x3B0000, 0x50000,
```
Each app slot: 0x1D0000 = ~1.8MB. LittleFS: 0x50000 = 320KB. Total = 4MB.

### 2. Add version + board identity to `config.h`
Move `FIRMWARE_VERSION` out of the `#ifdef CLOUD_ENABLED` block (line 214) to global scope. Add `BOARD_TYPE` string per board variant for OTA safety checks:
```cpp
#define FIRMWARE_VERSION "2.1.0"

#if defined(BOARD_ESP32S3_MATRIX)
  #define BOARD_TYPE "esp32s3-matrix"
#elif defined(BOARD_ESP32S3_LCD_169)
  #define BOARD_TYPE "esp32s3-lcd169"
#elif defined(BOARD_ESP32S3_LCD_13)
  #define BOARD_TYPE "esp32s3-lcd13"
#elif defined(BOARD_M5CORES3)
  #define BOARD_TYPE "m5cores3"
#endif
```

### 3. Create `vizbot/ota_update.h` (new file)
Self-contained OTA module following existing project pattern. Two update paths:

**Path A: Auto-update from GitHub (primary — requires internet)**
- `checkForUpdate()` — hits GitHub Releases API (`api.github.com/repos/kpow/esp32-led-matrix/releases/latest`) over HTTPS
- Parses JSON response for `tag_name` (version) and `assets[]` array
- Compares remote version to `FIRMWARE_VERSION` using semver comparison
- Finds the matching `.bin` asset by looking for `BOARD_TYPE` in the asset filename
- Stores result: `updateAvailable`, `updateVersion`, `updateURL`, `updateSize`
- `performUpdate()` — downloads `.bin` from GitHub asset URL, streams directly to `Update.write()` in chunks (no full-file buffering needed)
- TLS: Uses `esp_http_client` (same pattern as `cloud_client.h`) with pinned DigiCert Global Root G2 cert (GitHub's CA). Note: `esp_crt_bundle` crashes on generic ESP32-S3 boards per existing project findings, so we pin the specific CA.
- Runs on Core 0 wifi task, same as cloud client — TLS serialized naturally

**Path B: Manual file upload (fallback — works on AP-only WiFi)**
- **PROGMEM HTML page** at `/update` with file picker + upload button
- `handleOTAUpload()` — chunked upload handler using `Update` library
- Same safety checks: magic byte (0xE9), size vs partition, MD5 verification
- This is the escape hatch when the device has no internet

**Shared logic:**
- `handleOTAPage()` — serves the `/update` page (shows both auto-update status and manual upload form)
- `handleOTAResult()` — completion callback, sends JSON success/error, triggers `ESP.restart()`

### 4. Integrate into `web_server.h`
- Add `#include "ota_update.h"`
- Register endpoints in `setupWebServer()`:
  ```cpp
  server.on("/update", HTTP_GET, handleOTAPage);           // Update page
  server.on("/update", HTTP_POST, handleOTAResult, handleOTAUpload);  // Manual upload
  server.on("/update/check", HTTP_GET, handleUpdateCheck);  // Check GitHub for updates
  server.on("/update/start", HTTP_GET, handleUpdateStart);  // Trigger auto-download + flash
  ```
- Add `firmwareVersion`, `boardType`, and `updateAvailable` to `handleState()` JSON response
- Add firmware version display in Device section HTML
- If update available, show notification banner in web UI with "Update Now" button
- Add JS in `getState()` to populate version display and show/hide update notification

### 5. Add boot confirmation in `vizbot.ino`
After basic initialization in `setup()`:
```cpp
#include <esp_ota_ops.h>
esp_ota_mark_app_valid_cancel_rollback();
```
This tells the bootloader "new firmware booted OK, don't roll back."

### 6. GitHub Release Naming Convention
Binary assets in GitHub Releases must follow this naming pattern:
```
vizbot-{BOARD_TYPE}-{version}.bin
```
Examples: `vizbot-m5cores3-2.1.0.bin`, `vizbot-esp32s3-matrix-2.1.0.bin`

The device matches assets by checking if the filename contains its `BOARD_TYPE` string.

## Safety Checks

| Check | When | On Failure |
|-------|------|------------|
| ESP32 magic byte (0xE9) | First chunk | Abort, return error |
| Filename contains BOARD_TYPE | Upload start | Abort, return error |
| Size vs partition capacity | Upload start | Abort, return error |
| Update.write() byte count | Each chunk | Set error flag, skip rest |
| Update.end() MD5 | After final chunk | Don't commit, return error |
| Dual OTA rollback | After reboot | Bootloader auto-reverts on crash loop |

## User Workflow (for friends)
**Normal update (device has internet via STA WiFi):**
1. Open vizbot's web UI in browser
2. See "Update available: v2.2.0" notification in the Device section
3. Click "Update Now"
4. Device downloads and flashes automatically, reboots
5. Done

**Fallback (AP-only, no internet):**
1. Download `.bin` from link you share
2. Open vizbot web UI → Device section → "Manual Update"
3. Pick the `.bin` file, click Upload
4. Device flashes and reboots

## Files Modified
| File | Change |
|------|--------|
| `vizbot/partitions.csv` | Dual OTA slot layout |
| `vizbot/config.h` | Move FIRMWARE_VERSION global, add BOARD_TYPE per board |
| `vizbot/ota_update.h` | **NEW** — GitHub version check, auto-download+flash, manual upload handler, update page HTML |
| `vizbot/web_server.h` | Include ota_update.h, register 4 endpoints, version+updateAvailable in state JSON, update notification in Device UI |
| `vizbot/vizbot.ino` | Add esp_ota_ops boot confirmation |

## Git Workflow
1. Create GitHub issue documenting this feature
2. Create feature branch `feature/ota-web-update` from `main`
3. Implement all changes on the feature branch
4. Open a PR against `main` for review before merging

## Verification
1. Build for one board target in Arduino IDE, export binary
2. Flash via USB (required first time after partition change)
3. Confirm web UI shows firmware version in Device section
4. Navigate to `/update`, upload the same `.bin` — verify progress bar, success message, reboot
5. After reboot, confirm version still shows correctly (proving OTA wrote to second slot)
6. Test wrong board type `.bin` — verify rejection with clear error message
7. Test uploading a non-firmware file — verify magic byte check rejects it
