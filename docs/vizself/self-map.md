# vizBot self-map

Source: the v3.9.1 release of the `kpow/vizpow` repo. Firmware `FIRMWARE_VERSION "3.9.1"` (vizbot/config.h), vizbrain `__version__ = "1.3.1"` (vizbrain/vizbrain/__init__.py). There is one `##` section per source file. The code wins over the docs wherever they disagree. Pins and board details are in `docs/vizself/hardware.md`.

## Overview: what vizBot is

vizBot is Kevin's desk robot. The main body is an M5Stack Stackchan: an ESP32-S3 M5Stack CoreS3 sitting on a StackChan base. The CoreS3 has a 320x240 touch screen that shows vizBot's face, plus a camera, mic and speaker. The base adds two SCS0009 serial servos (pan/yaw and tilt/pitch), a 12-LED ring, three capacitive head-touch pads, and an INA226 battery monitor. The firmware draws an animated face and runs ambient effects and the "party mode" ferrofluid blob. It reacts to touch, shakes, sound and the camera.

The work is split between a body and a brain:
- **Body (firmware, `vizbot/`)**: Arduino-on-ESP-IDF C++ built with PlatformIO. `vizbot.ino` plus about 60 header files. Rendering and input run in `loop()` on Core 1. WiFi, the web server, cloud sync and the voice turn run as FreeRTOS tasks on Core 0.
- **Brain (vizbrain, `vizbrain/vizbrain/`)**: a Python service on Kevin's Mac, run by launchd. It does speech-to-text (mlx-whisper `whisper-small.en`) and calls Claude (default `claude-sonnet-5-5`, with `claude-haiku-4-5` for events). Replies are spoken with Kokoro TTS (default voice `puck`). Claude gets tools that call the bot's HTTP endpoints, and vizbrain also loads MCP "connectors" (for example Spotify through vizSpot).

Questions: what are you? what are you made of? what can you do? how do you work? which part of you runs on the Mac? what's your body like?

## Overview: boards (PlatformIO envs in vizbot/platformio.ini)

- `stackchan`: CoreS3 plus StackChan base. `BOARD_TYPE "stackchan"`. This is the only build with `VOICE_ENABLED`, servos, the camera and head touch.
- `m5cores3`: a bare CoreS3. `BOARD_TYPE "m5cores3"`.
- `faces`: a CoreS3 in an M5Stack Faces Bottom3 game base, with 10 SK6812 LEDs and a gamepad. vizBot lives in `ota_1`, next to the kFun games and a chooser. `BOARD_TYPE "faces"`.
- `lcd-169` (the default env): the Waveshare ESP32-S3-Touch-LCD-1.69 "pendant", with a battery, piezo buzzer, a 5x5 LED matrix and low power (pendant) mode. `BOARD_TYPE "esp32s3-lcd169"`.
- `lcd-13`: the Waveshare ESP32-S3-LCD-1.3, which has no touch. `BOARD_TYPE "esp32s3-lcd13"`.
- The ESP32-S3-Matrix 8x8 LED target (`BOARD_ESP32S3_MATRIX`) still exists in config.h but has no env, so it is not built.

Questions: what boards can you run on? which gadgets run your firmware? how are you different from the pendant? why can only the Stackchan talk? which board is the default build?

## Overview: how the body and brain talk

- **Bot HTTP API:** port 80, handled by Arduino `WebServer` in `web_server.h`. It has no auth, and GET or POST both work. The bot's name is `vizbot-xxxx.local` over mDNS, where xxxx comes from the MAC (`device_id.h`). Its fallback hotspot is `vizBot-XXXX` with a captive portal. vizbrain's tools call routes such as `/state`, `/bot/expression`, `/bot/head/set_angles`, `/bot/base_leds/set`, `/bot/party`, `/bot/photo/capture`, `/bot/sound`, `/bot/say` and `POST /bot/art`.
- **vizbrain HTTP API:** port 4050, bound to 0.0.0.0, advertised over Bonjour as `_vizbrain._tcp`. The bot posts a recorded WAV to `POST /v1/voice` and streams the reply from `GET /v1/audio/<id>`. It also calls `/v1/listen/<id>` (follow-up questions), `/v1/hush`, `/v1/event` (camera arrival and lean-in) and `/v1/voices`.
- **Wake word:** while idle, the bot streams mic audio over UDP to the brain on port 4051 in `VZW1` packets. vizbrain runs the "Hey vizBot" ONNX model and calls the bot's `/brain/wake`.
- **Pairing:** every 60 s vizbrain pushes its address and a pairing token to the bot's `/brain/config`. The bot then sends the `X-Viz-Token` and `X-Bot-Id` headers on every brain call.
- **Other links:** the vizCloud HTTPS service for sync and commands (`cloud_client.h`), ESP-NOW broadcast between bots (`esp_now_mesh.h`), and an external WLED LED matrix on the LAN (`wled_display.h`). On the Mac side there is also vizMac on port 4049 for music and keyboard lights.

Questions: how does your body talk to your brain? how do you find the Mac on the network? how does the brain control you? how do you pair with the brain? what ports do you use? how do you hear the wake word if the brain does the listening?

## Overview: other folders (not described in detail here)

- `vizpow/`: the older vizPow LED and motion controller firmware.
- `vizmini/`: a pared-down vizbot for the ESP32-C3 SuperMini, called the "yeti".
- `atom-fluid/`, `atom-sand/`: small M5 Atom Matrix 5x5 toys (a fluid sim and falling sand).
- `poc_wifi_weather/`, `poc_wifi_webserver/`, `poc_wled_text/`, `wifi_test/`: early proof-of-concept sketches.
- `tools/`: `gen_eye_params.py`, `servolive.py` and eye-param JSON.
- `scripts/`: `add-icon.js`.
- `pix-art-converter/`: an HTML pixel-art tool.
- `builds/`: built firmware binaries.
- `vizbrain/tests`, `vizbrain/training`: the vizbrain test suite and wake-word training.
- The eye lab app (eyelab) is not part of the v3.9.1 tree.

Questions: what else is in your repo besides you? what's the yeti? what are the Atom toys? where are the old experiments? what other projects live next to you?

# Firmware

## vizbot/vizbot.ino

Arduino entry point: shared globals, `setup()`, `loop()` (render/input on Core 1), AP helpers, shake detector.
`setup()` order: 1.69 latches `PWR_HOLD_PIN`; mbedTLS to PSRAM; Serial 115200; 1.69 `loadLowPowerMode()` (80 MHz if on); `initDeviceID()`, `otaMarkBootValid()`; CoreS3 `M5.begin()` (Faces: speaker/mic off, GPIO13 is the LED line); `initTaskManager()`, `initLCD()`, `runBootSequence()`; `loadSettings()`, `loadWledSettings()`, `loadScheduleSettings()`; `scInitBaseLeds()` (Faces, 1.69 ring); Stackchan `scTouch_state.init()`, `scIdleServo.init()`; `enterBotMode()`; if `wifiEnabled` `startWifiTask()` + `initMesh()`; `startCloudTask()`; `startVoiceTask()` (Stackchan only).
`loop()` order: `readIMU()`, shake detect (sustained = info mode; short = `botMode.onShake()`), `handleTouch()`, Stackchan head touch (`voiceOnTalkTap()`, `scFireShake()`, `scFireChillMode()`), effect cycle 20 s / palette 5 s, `tweenManager.update()`, `drainCommandQueue()`, `pollWifiProvisioning()`, `flushSettingsIfDirty()`, `runInfoMode()` or `runBotMode()`, `scLeds.update()`, pacing to `BOT_FRAME_PERIOD_MS`.
`startWifiAP()`, `stopWifiAP()`, `readIMU()`, `wifiEnabled`, `lowPowerMode`.

Questions: what happens when you boot up? what do you do every frame? what order do you start things in? how do you tell a quick shake from a long one? what's your main loop?

## vizbot/config.h

Compile-time config: board flags to `TARGET_LED`/`TARGET_LCD`/`TARGET_CORES3`, pins, `FIRMWARE_VERSION "3.9.1"`, `BOARD_TYPE`, WiFi/cloud constants. v3.9.1 changed only the version string; it is shown in `/state` (`firmwareVersion`), cloud registration and the touch UI System page.
Identifiers: `BOARD_TYPE` = `esp32s3-matrix`, `esp32s3-lcd169`, `esp32s3-lcd13`, `m5cores3`, `stackchan`, `faces`; `WIFI_SSID_BASE` "vizBot", `MDNS_HOSTNAME_BASE` "vizbot", `WIFI_NVS_NAMESPACE` "vizwifi", `CLOUD_NVS_NAMESPACE` "vizcloud", `OTA_GITHUB_REPO` "kpow/vizpow", `NUM_AMBIENT_EFFECTS` 16, `MAX_SAY_LEN` 96, `TZ_DEFAULT` (cloud secret hard-coded, omitted).
Pins: 1.69 I2C 11/10, LCD SCK6 MOSI7 CS5 DC4 RST8 BL15, touch 0x15, buzzer 42, `PWR_HOLD_PIN` 41, `PWR_KEY_PIN` 40, battery ADC 1, ring GPIO17, `DATA_PIN` 14. 1.3 I2C 47/48, LCD SCK40 MOSI41 CS39 DC38 RST42 BL20. CoreS3 I2C 12/11, LED 38, touch 0x38, MIDI TX 17 or 2. Stackchan: 0x6F, 0x68, 0x41. Faces: GPIO13.

Questions: what version are you? what firmware are you running? where are your pins set? what's your wifi hotspot called? what board type are you? how much power does your wifi radio use?

## vizbot/platformio.ini

Build envs, default `lcd-169`; LittleFS; FastLED, SensorLib, ArduinoJson.
Envs: `lcd-169` (`BOARD_HAS_RING_LEDS`, `BOARD_HAS_PSRAM`), `lcd-13`, `m5cores3` (DIO flash, M5Unified 0.2.13), `stackchan` (+ StackChan-BSP), `faces` (`partitions_kfun.csv`, `app_offset.py`, `custom_app_offset = 0x510000`). No `matrix` env.

Questions: how do you get built? what build settings do you use? which libraries do you use? how do I compile you for a different board? why is your flash mode set to DIO?

## vizbot/partitions.csv

4MB dual-OTA table: app0 0x10000, app1 0x1E0000, spiffs 0x3B0000. Unused by any env.

Questions: how is your flash memory split up? do you have room for two firmware copies? what's that old partition table for? where does your file storage live?

## vizbot/partitions_kfun.csv

Shared 16MB table for Faces Bottom3: `factory` 0x10000 (chooser), `ota_0` 0x110000 (kFun games), `ota_1` 0x510000 (vizBot), nvs 0x9000, spiffs 0x910000 (2MB), coredump 0xb10000.

Questions: how do the games and you share one flash chip? where do the kFun games live? where does the game chooser live? how is the Faces flash laid out?

## vizbot/app_offset.py

Forces upload offset from `custom_app_offset` so Faces flashes ota_1. `_force_offset()`.

Questions: how do you get flashed onto the Faces without wiping the games? why do you flash to a different spot on the Faces? how does the upload know where to put you? what makes flashing the Faces special?

## vizbot/name_firmware.py

Post-build: writes `../builds/vizbot-<board_type>-v<version>.bin` and `-factory.bin` (merge: 0x0, 0x8000, 0x10000). `post_build()`, `board_type_map`.

Questions: how are your firmware files named? where do the built binaries go? how do you make the factory image? what's the difference between the normal and the factory bin?

## vizbot/idf_component.yml

Declares ESP-IDF dependency `idf >=5.1`.

Questions: what version of ESP-IDF do you need? what framework are you built on? what do you depend on underneath Arduino? are you on Arduino or ESP-IDF?

## vizbot/README.md

Firmware technical reference (background): targets, architecture, endpoints, NVS, voice, cloud, build gotchas. Partly stale versus code.

Questions: where's your technical manual? is there a write-up of how you work? where can I read your overall design? is your readme up to date?

## vizbot/settings.h

NVS persistence via `Preferences`, debounced 2 s (`SETTINGS_DEBOUNCE_MS`). `loadSettings()`, `saveSettings()`, `markSettingsDirty()`, `flushSettingsIfDirty()`, `loadLowPowerMode()`.
NVS namespace `vizbot` keys: `bright`, `lcdBr`, `effect`, `palette`, `autoCyc`, `bgStyle`, `party`, `pSize`, `hiRes`, `kscope`, `kscopeSpin`, `kscopeBlend`, `kscopeSlice`, `wLat`, `wLon`, `tz`; conditional `lowPwr` (1.69), `ringMode`, `ringBri`, `ringSpd` (ring LEDs), `sndOn`, `sndVol` (`HAS_SOUND`), `audioFx`, `audioDrm`, `midiPort` (CoreS3), `chillMin` (Stackchan). `devName` is in `device_id.h`.

Questions: how do you remember your settings after a restart? where do you save my brightness and palette? when do you write settings to flash? what settings do you keep? why do settings take a couple seconds to save?

## vizbot/device_id.h

Unique `apSSID` (`vizBot-A3F2`) and `mdnsHostname` (`vizbot-a3f2`) from eFuse MAC or custom name. `initDeviceID()`, `saveDeviceName()`, `DEVICE_NAME_MAX` 24. NVS `vizbot` key `devName`.

Questions: what's your name? how did you get your name? why is your hotspot called vizBot with four letters after it? can I rename you? what's your network hostname?

## vizbot/task_manager.h

I2C mutex, command queue (`CMD_QUEUE_SIZE` 8, non-blocking push), FreeRTOS tasks.
Identifiers: `i2cAcquire()` (50 ms), `i2cRelease()`, `i2cSetLongHold()`, `pushCommand()`, `drainCommandQueue()`, `cmdSayText()`, `startWifiTask()`, `startCloudTask()`. Commands: `CMD_SET_BRIGHTNESS`, `CMD_SET_EXPRESSION`, `CMD_SAY_TEXT`, `CMD_SET_PERSONALITY`, `CMD_PLAY_SEQUENCE`, `CMD_SET_PARTY`, `CMD_MESH_SCAN`, `CMD_SLEEP`, etc. (20 types).
Tasks (static BSS stacks, Core 0, priority 1): `wifi_srv` 8192 B (polls WiFi connect, WLED, weather, scheduled content, mesh, DNS, `server.handleClient()`, 2 ms delay); `cloud` 8192 B (`pollCloudSync()`, `pollScheduledCommands()`, 200 ms). `voice` task (Core 0, prio 1) is in `voice_client.h`. Render loop = Arduino `loop()` on Core 1.

Questions: how do you do several things at once? what runs on your two cores? how do commands get passed between your tasks? how do you share the I2C bus safely? what background jobs do you run?

## vizbot/loop_stats.h

Frame timing for `GET /state`. `loopStatsBegin()`, `loopStatsPush()`, `loopStatsJson()`.

Questions: what's your frame rate? how do you measure your loop timing? are you running smoothly? how long does each frame take?

## vizbot/system_status.h

`SystemStatus` ready flags (imuReady, touchReady, wifiReady, staConnected, littlefsReady, `scBaseLedsReady`, Stackchan `sc*Ready`, ...), IPs, `failCount`; instance `sysStatus` is in `boot_sequence.h`.

Questions: which of your parts started up okay? how do you keep track of what's working? is your camera ready? did your sensors come up at boot? how many things failed at startup?

## vizbot/ota_update.h

Manual OTA only; no GitHub release lookup exists here (`OTA_GITHUB_REPO` unused). Checks: filename contains `BOARD_TYPE`, first byte 0xE9, `Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)`; reboots after success. Refused on `BOARD_HAS_FACES_BASE` (next slot is the games).
`handleOTAUpload()`, `handleOTAPage()`, `otaMarkBootValid()` (rollback guard).
Endpoints: `GET /update` page, `POST /update` upload (registered in `web_server.h`).

Questions: how do I update your firmware? can you update over wifi? how do you check an update file is meant for you? what if an update goes bad? why can't the Faces update over the air?

## vizbot/wifi_provisioning.h

Join home WiFi, AP always the fallback. Boot: `bootAttemptSTA()` (verified creds only), then AP (`apSSID`, channel 1). UI: `requestWifiConnect()`; `doWifiConnectBlocking()` saves creds verified on success, clears and returns AP-only on failure. DNS stops after `WIFI_AP_LINGER_MS` 30000, AP stays for ESP-NOW. `pollStaWatchdog()` rejoins, backoff to 5 min.
NVS namespace `vizwifi`: `ssid`, `pass`, `verified`. mDNS `<mdnsHostname>.local` with http service on 80 (`startMDNS()`/`startDNS()` in `web_server.h`).
Identifiers: `WifiProvState` (`PROV_IDLE`..`PROV_STA_ACTIVE`), `wifiProv`, `resetWifiProvisioning()`, `getWifiStatusJson()`, `pollWifiConnectTask()`. Endpoints: `/wifi/scan`, `/wifi/connect`, `/wifi/status`, `/wifi/reset`.

Questions: how do you join my wifi? what happens if the wifi password is wrong? how do you get back online after the wifi drops? when do you make your own hotspot? how do I set up your wifi?

## vizbot/cloud_client.h

vizCloud HTTPS client (`CLOUD_ENABLED`), pinned GTS Root R4 cert, header `X-Bot-Secret` (hard-coded, value omitted).
Sends: `POST /api/bots/register`; `POST /api/bots/<botId>/sync` every `pollIntervalSec` (acks, expression, personality, rssi, freeHeap, uptime, ntpTime, accel, meshPeers, lux).
Receives commands `expression`, `say`, `personality`, `brightness`, `background`, `ambient_effect`, `sound`, `play_sequence`, `set_volume`, `auto_brightness`, `sleep`, `mesh_scan`, `reboot` (8 scheduled slots), groups, content. 401 stops; backoff 10-300 s.
NVS namespace `vizcloud`: `botId`, `pollInt`, `contVer`. `cloudRegister()`, `cloudSync()`, `dispatchCloudCommand()`, `pollCloudSync()`.

Questions: do you talk to the cloud? what do you send to vizCloud? how do you get commands from the internet? what can the cloud make you do? how often do you sync?

## vizbot/content_cache.h

LittleFS cache under `/cloud/`: `meta.json`, `sayings.json`, `personalities.json`, `sequences.json`. Identifiers: `initContentCache()`, `cloudMeta`, `loadCloudMeta()`, `writeCloudContent()`, `applyCloudPersonalities()`, `applyCloudSequences()`, `getCloudSaying()`, `clearContentCache()`.

Questions: where do you keep stuff downloaded from the cloud? where do your cloud sayings come from? how do you store personalities from the cloud? can you clear your cloud cache?

## vizbot/esp_now_mesh.h

ESP-NOW bot-to-bot link with one message type: 24-byte packed `MeshStatePacket` (version 1), broadcast to FF:FF:FF:FF:FF:FF via AP interface every 3000 ms, unencrypted. Fields: deviceId, expression, botState, effect, palette, pitch, roll, audioLevel, proximity, lux, `reserved[0]` bit0 WLED active, bit1 scheduler owner, `reserved[1..4]` WLED IP. Peers max 10, stale after 15 s.
Identifiers: `initMesh()`, `pollMeshBroadcast()`, `meshAnyPeerWledActiveForIP()`, `meshSetWledActive()`, `meshSetSchedOwner()`, `meshGetPeerCount()`, `MESH_SCAN_BURST_COUNT` 3.

Questions: how do you talk to other bots? do you know when other vizBots are nearby? how do bots share the WLED matrix? what do you broadcast to your friends? how many bots can you see?

## vizbot/boot_sequence.h

Visual boot checklist and `sysStatus` definition. Stages: LCD, LEDs, I2C, IMU, Touch, CoreS3 Sensors, WiFi STA, WiFi AP, Web Srv, Portal, Storage, Cloud, then Stackchan IO Exp..Photo FS. Low power: WiFi stages OFF. Sets `wifiEnabled`.
Identifiers: `runBootSequence()`, `bootStageLEDs()`, `bootStageWiFi()`, `bootStageDNS()`, `bootDrawSummary()`, `BOOT_TOTAL_STAGES`.

Questions: what's that checklist when you start up? what do you check at boot? why does your boot screen show OK and FAIL? what's on your boot screen? why is wifi off at boot in low power mode?

## vizbot/info_mode.h

Weather screen: face shrinks to mini eyes, shows temp, icon, 3-day forecast bars. States `INFO_INACTIVE`, `INFO_PRE_ENTER`, `INFO_ENTERING`, `INFO_ACTIVE`, `INFO_EXITING` (600 ms transitions). Identifiers: `infoMode`, `InfoModeData`, `beginEnterTransition()`, `beginExitTransition()`, `runInfoMode()`. LCD targets only; stub elsewhere.

Questions: how do you show the weather? what's the forecast screen? how do I get to the weather page? why do your eyes shrink when the weather shows? how does a long shake bring up the weather?

## vizbot/web_server.h

The bot's HTTP API and built-in control page, Arduino `WebServer` on port 80 (`setupWebServer()`). vizbrain's tools call it over the LAN. No WebSocket, no auth on any route. Routes are path-only, so GET or POST both work (except `/update`, `/bot/art`). Unknown URLs and OS captive-portal probes redirect (302) to `/`. Also `startDNS()` (wildcard DNS, port 53) and `startMDNS()` (`<hostname>.local`, `_http._tcp` 80).

- **Key identifiers:** `setupWebServer()`, `handleState()`, `handleRoot()`, `handleCaptiveRedirect()`, `webpage`
- **HTTP endpoints:**
  - State: `/`, `/state` (big JSON: sys health, heap, wifi, party, sensors, servos, battery, firmwareVersion, boardType), `/schedule`
  - Face: `/bot/expression`, `/bot/background`, `/bot/hires`, `/bot/time`, `/brightness`, `/bot/personality`, `/bot/personality/rotation`
  - Say: `/bot/say` (text, dur)
  - Ambient/party: `/bot/ambient`, `/bot/autocycle`, `/bot/kscope`, `/bot/kscope/spin`, `/bot/kscope/blend`, `/bot/kscope/slice`, `/bot/party` (v, size, hero, seq, stack)
  - Info: `/info/toggle`, `/info/location`, `/info/zip`, `/settings/timezone`
  - Servos (Stackchan): `/bot/head/set_angles` (yaw, pitch tenths of degree, time), `/bot/head/preset` (name), `/bot/head/recenter`, `/bot/servo/reinit`, `/bot/chill`, `/bot/chill/minutes`, `/bot/poweroff`, `/bot/battery/status`
  - LEDs: `/bot/base_leds/set` (r,g,b,index), `/bot/base_leds/mode`, `/wled/status`, `/wled/config`, `/wled/test`, `/wled/emoji/add`, `/wled/emoji/remove`, `/wled/emoji/clear`, `/wled/emoji/toggle`, `/wled/emoji/settings`
  - Camera (Stackchan): `/bot/photo/capture` (QVGA JPEG, used by vizbrain look tool), `/bot/camera/enable`, `/bot/camera/diag`; `/bot/photos`, `/bot/photo/get`, `/bot/photo/delete` return 501
  - Audio: `/bot/sound`, `/bot/volume`, `/bot/sequences`, `/bot/audiofx`, `/bot/audiodrama`, `/bot/synthport`
  - Brain: `/brain/status`, `/brain/config` (host, token, enabled, wake), `/brain/listen`, `/brain/wake`, `/brain/play` (id, text, expr, ms, mac), `/brain/lastwav`, `/brain/voices`, `/brain/voice`; `POST /bot/art`, `/bot/art/hide`
  - WiFi/device: `/wifi/scan`, `/wifi/connect`, `/wifi/status`, `/wifi/reset`, `/device/name`
  - OTA/cloud: `GET|POST /update`, `/cloud/status`, `/cloud/sync`
  - Debug: `/debug/wifidrop`, `/debug/screen`, `/debug/panelcheck` (1.69/1.3), `/debug/touch`, `/battery/log`
- **Board gating:** `VOICE_ENABLED` (brain, art), `BOARD_HAS_STACKCHAN_BASE` (head, camera, chill), base-LED boards, `HAS_SOUND`, `TARGET_CORES3`, `CLOUD_ENABLED`, `TOUCH_UI_V2` (debug)
- **Works with:** voice_client.h, album_art.h, ota_update.h, wifi_provisioning.h, task_manager.h

Questions: what's your web page? how can I control you from a browser? what web commands do you answer to? what's your web address? how does the brain send you commands? why does every unknown page go to your home page?

## vizbot/voice_client.h

This is the push-to-talk conversation with vizbrain on the Mac, plus the wake-word mic stream. A front head-pad tap (or `/brain/listen`, or a wake-word hit) starts a turn: beep, record the mic until silence, POST the WAV to vizbrain, stream the spoken reply back and play it with a moving mouth. The whole turn runs in `voiceTask` on Core 0 (8 KB static stack). Record and playback buffers sit in PSRAM, allocated on first use. Every state change goes through `voiceSetState()`, which also copies the state into `voicePhase` so the render loop can show it: in party mode the blob acts out listening, thinking and speaking, and the turn's bubbles still appear.

- **Key identifiers:** `VoiceClient voice`, `WakeStream wake`, `voiceTask()`, `startVoiceTask()`, `voiceSetState()`, `voicePhase` (0 idle, 1 listening, 2 thinking, 3 speaking, 4 error), `voiceRunTurn()`, `voiceRunPlay()`, `voiceRecord()`, `voicePostWav()`, `voiceStreamSpeak()`, `voiceAskListen()`, `voiceResolveBrain()`, `voiceBrainRequest()`, `voicePostEvent()`, `voicePollSenses()`, `voiceOnTalkTap()`, `voiceOnWake()`, `wakeStep()`, `wakeProcessChunk()`, `loadVoiceSettings()`, `saveVoiceSettings()`, `VoiceState` (idle, listening, thinking, speaking, error)
- **Flow:** 1) listen: curious beep, focused face, "Listening..." bubble, swap the shared I2S port from speaker to mic (`voiceTakeMic`). Capture is 16 kHz mono 16-bit in 512-sample chunks (32 ms), max 12 s. Speech start is above 3x the learned noise floor (min RMS 300); end is 800 ms below max(2x noise, 15% of speaker level), or 1500 ms when answering a question; gives up after 6 s of silence. Needs at least 300 ms of speech. If the noise floor is over 1500 (stale DMA after a warm reset) it restarts the mic up to twice. 2) think: typing sound effect, thinking face, `POST http://<brain>/v1/voice` with `Content-Type: audio/wav` (optionally `X-Viz-Follow-Up: 1`). Reply JSON fields: `heard`, `bubble`, `audio_id`, `expression`, `audio_ms`, `mac`. 3) speak: `GET /v1/audio/<audio_id>` streams 24 kHz mono 16-bit PCM (chunked); it is buffered 0.3 s then queued to the speaker in 0.2 s pieces, with a mouth-envelope computed per 33 ms frame. If `mac` is true the bot plays at volume 0 for lip-sync while the Mac speaks, and a tap sends `POST /v1/hush`. 4) `GET /v1/listen/<audio_id>` asks if the reply was a question; if yes it listens again without a tap, up to 3 follow-ups.
- **Brain discovery:** brain port is 4050 (`VOICE_BRAIN_PORT`). Uses the pinned `brainHost` if set (vizbrain pushes it via `/brain/config`, re-sending about every minute); after 2 failed connects, or when none is set, it asks mDNS for `_vizbrain._tcp`. `/brain/play` from an unconfigured brain adopts the caller's IP.
- **HTTP endpoints called on the brain:** `POST /v1/voice`, `GET /v1/audio/<id>`, `GET /v1/listen/<id>`, `POST /v1/hush`, `POST /v1/event` (JSON `{"type":"arrival","quiet_min":N}` from camera motion, `{"type":"lean_in"}` with a 3 minute cooldown), `GET|POST /v1/voices` (proxied for the bot's web page). Every call sends headers `X-Bot-Id` (the mDNS hostname) and `X-Viz-Token` (the pairing token saved in NVS).
- **Wake word:** while idle, the mic runs and each chunk is compared to a slowly adapting noise floor (opens above 2.5x noise, min RMS 150). When open it sends UDP packets to the brain host on port 4051: header `VZW1` + sequence + flags (start/end/heartbeat) + 512 s16le samples, with about 1 s of pre-roll; a heartbeat every 5 s when quiet. vizbrain runs the "Hey vizBot" model and calls `/brain/wake`, which sets `listenRequested` via `voiceOnWake()`.
- **NVS / settings keys:** namespace `vizbot`, keys `brainHost`, `brainTok`, `voiceOn`, `wakeOn`
- **Hardware:** CoreS3 mic and speaker (shared I2S_NUM_1) via M5Unified; PSRAM buffers about 2 MB; camera motion check for events
- **Board gating:** only when `VOICE_ENABLED` is defined
- **Works with:** web_server.h (`/brain/*` handlers), album_art.h (`artHide()` at turn start), bot_mode.h (face hooks `voiceHoldsFace`, `voicePhase`, `voiceSpeakT0`, `voiceEnv`, `voiceShowBubble`), party_blob.h (through bot_mode.h), audio_spectrum.h (`voiceOwnsAudio`, `wakeTapBuf`), stackchan camera motion (`scCamMotion`), config.h

Questions: how do you hear me? how do you talk back? what happens when I say hey vizBot? how do you know I stopped talking? how do you find the brain on the Mac? why do you sometimes listen again without a tap? why does your mouth move while the sound comes from the Mac?

## vizbot/album_art.h

vizbrain pushes a 240x240 baseline JPEG (max 96 KB); the render loop decodes it once into a PSRAM sprite and draws it instead of the face. The face stays as a small circle top-right.

- **Key identifiers:** `handleArtUpload()`, `handleArtDone()`, `handleArtHide()`, `artRender()`, `artHide()`, `ART_SIZE`, `ART_FACE_R`
- **HTTP endpoints:** `POST /bot/art?ms=8000` (multipart JPEG, ms 1000-30000), `GET /bot/art/hide`
- **How it ends:** timeout, hide call, or a voice turn starting.
- **Board gating:** `VOICE_ENABLED`; otherwise empty stubs
- **Works with:** web_server.h, voice_client.h, display_lcd.h

Questions: how do you show album covers? what's that picture on your screen? how long does a picture stay up? where does your face go when a picture's showing? can the brain put an image on your screen?

## vizbot/stackchan_base.h

Stackchan hardware layer on M5's StackChan-BSP: head servos, base LEDs, head touch, battery; rail-cycles servos every 5 min.
`scMoveYaw()`, `scMovePitch()` (25-85 deg, home 48), `scGoHome()`, `scRecoverServos()`, `scNodGesture()`, `ScI2cLock`; servo IDs 1/2, I2C SDA12/SCL11, NVS `servo` `zero_pos_1/2`; stackchan only.

Questions: how do you move your head? how far can you look up and down? why does your head sometimes get stuck? how do you know your battery level? how do you nod? why do your servos restart every few minutes?

## vizbot/stackchan_camera.h

GC0308 camera: on-demand JPEG photos for vizbrain's look tool, plus motion "arrival" detection.
`scCameraJpeg()`, `scCameraMotionTick()`, `scInitCamera()`, NVS `vizbot` `camOn`; `GET /bot/camera/enable`, `/bot/camera/diag`; QVGA, SCCB on I2C port 1; stackchan only.

Questions: how do you see? can you take a picture? what are you looking at? how do you notice someone walking in? how do you tell real motion from the lights changing? why didn't you notice me while your head was moving?

## vizbot/stackchan_idle.h

Idle head drift and room-sweep movement, sized by personality (hyper, chill, grumpy).
`ScIdleServo`, `scIdleServo`, `IDLE_DRIFT`, `IDLE_SWEEP`; stackchan only.

Questions: why do you move your head on your own? why do you look around the room when nobody's here? how does your mood change how much you move? what's your idle wandering? why are you so twitchy when you're hyper?

## vizbot/stackchan_leds.h

Shared LED effect engine (11 modes, flash, mood colours, audio overlay) used by Stackchan ring, Faces strips and the 1.69 matrix.
`ScBaseLeds`, `scLeds`, `SC_LED_MODE_*`, `scFlashMood()`, `scUpdateMoodFromExpression()`; `HAS_BASE_LEDS`.

Questions: how do your lights work? what light modes do you have? why do your LEDs change color with your mood? can your lights react to music? how do your lights copy the screen? what does a quick LED flash mean?

## vizbot/stackchan_touch.h

Head-pad taps: front = nod or talk to vizbrain, back = shake, 2 s hold = chill mode (10/30/60/120 min).
`ScTouchState`, `scFireNod()`, `scFireShake()`, `scFireChillMode()`, `scChillMinutes`, NVS `vizbot` `chillMin`; Si12T I2C 0x68; stackchan only.

Questions: what happens when I pat your head? what does a tap on the front of your head do? what happens if I tap the back of your head? what if I keep my hand on your head? how do I put you in chill mode? which part of your head starts a conversation?

## vizbot/faces_base.h

LED backend for the Faces Bottom3 base: 10 SK6812 on GPIO13, shared with the I2S clock so audio and LEDs exclude each other.
`scInitBaseLeds()`, `scBaseLedsRelease()`, `scBaseLedsReclaim()`, `FACES_LED_PIN` 13; faces only.

Questions: how do the lights on the Faces base work? why can't the Faces play sound and lights at the same time? how many LEDs does the game base have? why does turning on audio blank the Faces lights?

## vizbot/faces_chooser.h

Returns the Faces device to its top-level app chooser; re-arms it every boot, and holding START+SELECT on the gamepad for 800 ms triggers it.
`armChooserNextBoot()`, `bootToChooser()`, `facesChordPoll()`; gamepad I2C 0x08; faces only.

Questions: how do I get back to the games menu? how do I switch from you to the games? what does holding start and select do? how does the Faces pick which app to run?

## vizbot/display_lcd.h

Display layer: `DisplayProxy` gives one drawing API over M5GFX (CoreS3) or LovyanGFX ST7789 (1.69, 1.3), with a PSRAM double-buffer canvas.
`gfx`, `initLCD()`, `beginCanvas()`, `flushCanvas()`, `setLCDBacklight()`, `lcdCanvasBuffer()`; 1.69 SPI 80 MHz, 1.3 SPI 40 MHz.

Questions: how do you draw on your screen? what kind of screen do you have? how do you keep the picture from flickering? how do you dim your screen? how fast does your screen update?

## vizbot/touch_control.h

Touch input: CST816T on the 1.69 (I2C 0x15, reset GPIO13), M5Unified touch on CoreS3; legacy menu is replaced by touch_ui.h when `TOUCH_UI_V2` is set.
`initTouch()`, `readTouch()`, `handleTouch()`, `menuVisible`.

Questions: how do you feel touches on your screen? what touch chip do you use? why did the old menu go away? how do you read where my finger is on the pendant?

## vizbot/battery_adc.h

1.69 battery gauge: ADC on GPIO1 through a 3x divider, smoothed, mapped to percent and 0-4 bars; detects a USB host.
`BatteryAdc`, `batteryAdc`, `BAT_CURVE_MV`; optional `GET /battery/log` (`BAT_RUNDOWN_LOG`).

Questions: how much battery does the pendant have? how does the pendant measure its battery? how do you know the pendant is plugged in? what do the battery bars mean?

## vizbot/power_button.h

1.69 power: holds latch GPIO41 high, PWR key on GPIO40; a 2 s hold or battery under 3.30 V for 10 s powers off.
`PowerButton`, `powerButton`, `pwrPowerOff()`, `BAT_CUTOFF_MV`.

Questions: how do I turn the pendant off? why does the pendant shut off when the battery's low? what does holding the power button do? how does the pendant stay on after I let go of the button?

## vizbot/proximity_light.h

LTR-553 proximity and ambient-light sensor on the CoreS3 internal I2C, 10 Hz, flags near and covered.
`ProxLightState`, `proxLight`, `PROX_I2C_ADDR` 0x23; all CoreS3 targets.

Questions: how do you know my hand is close? how do you sense light in the room? how does peekaboo work? how do you know when you're covered up? how do you notice me leaning in?

## vizbot/ring_leds.h

LED backend for the 1.69's 5x5 serpentine WS2812B matrix, 5 V powered, driven directly over RMT.
`scInitBaseLeds()`, `scRingXY()`, `RING_LED_PIN` 17; lcd-169 only.

Questions: how do the pendant's LEDs work? what's the little light grid on the pendant? why are the pendant LEDs on 5 volts? how are the pendant lights wired?

## vizbot/buzzer.h

Passive piezo buzzer driver (LEDC square wave), kept low when idle to avoid heating the board.
`Buzzer`, `buzzer`, `tone()`, `off()`, `BUZZER_PIN` 42, LEDC channel 0; 1.69 only.

Questions: how does the pendant beep? what makes the pendant's chirps? why would the pendant buzzer get hot? can the pendant play tones?

## vizbot/pendant_shake.h

1.69 low-power shake reactions: dizzy meter, fortune, or palette shuffle, rate-limited by a budget and cool-off.
`pendantShakeSustained()`, `pendantShakeUpdate()`, `PENDANT_FORTUNES`; `HAS_LOW_POWER_MODE`.

Questions: what happens when I shake the pendant? why does the pendant tell fortunes? why does the pendant get dizzy when I shake it? why does shaking change the pendant's colors? why does the pendant ignore shakes sometimes? what does shaking do in low power mode?

## vizbot/audio_analysis.h

Mic loudness analysis on CoreS3: RMS level plus spike (clap), speech and 30 s silence flags.
`AudioAnalysis`, `audioAnalysis`; ES7210 mic, 16 kHz.

Questions: can you hear me clap? how do you know how loud the room is? how do you tell if someone's talking? how do you know it's been quiet a while? do you react to sounds?

## vizbot/audio_spectrum.h

FFT analyzer on CoreS3 (512 pts, 16 kHz, 30 Hz): bass/mid/treble/rms and beat envelope for effects; hands GPIO13 between mic and LEDs on Faces.
`AudioSpectrum`, `audioSpectrum`, `audioDrama`, `setEnabled()`; NVS `vizbot` `audioFx`, `audioDrm`.

Questions: how do you react to music? how do you hear the beat? how do you split sound into bass and treble? what does the reactivity setting do? how do the effects dance to music?

## vizbot/midi_synth.h

SAM2695 MIDI synth driver over a Grove port (UART2, 31250 baud) plus shared GM instrument and percussion constants.
`MidiSynth`, `midiSynth`, `midiSynthPort`, `GM_*`; Port C TX17/RX18 (default), Port A TX2/RX1; NVS `vizbot` `midiPort`.

Questions: do you have a synthesizer? how do you play real instrument sounds? which port is the MIDI synth plugged into? why is the synth silent? what plugs into your Grove port?

## vizbot/layout.h

Screen-position constants derived from `LCD_WIDTH`/`LCD_HEIGHT` for boot screen, speech bubble, info mode and forecast.
`OVERLAY_BUBBLE_Y`, `BOOT_LINE_HEIGHT`, `INFO_DIVIDER_Y`, `FORECAST_COL_*`.

Questions: where do things go on your screen? how is your screen laid out? where does the speech bubble sit? how do you fit the weather on different screens?

## vizbot/bot_mode.h

Face state machine and render pipeline (LCD boards). States `BotState`: BOT_ACTIVE, BOT_IDLE, BOT_SLEEPING (never entered; sleep disabled, wake() fires at once). Active goes idle after personality `idleTimeoutMs` (Chill 90s, Hyper 180s, Grumpy 45s). Expressions (EXPR_*, 0-24): NEUTRAL, HAPPY, SAD, SURPRISED, CHILL, ANGRY, LOVE, DIZZY, THINKING, EXCITED, MISCHIEF, SKEPTICAL, WORRIED, CONFUSED, PROUD, SHY, ANNOYED, FOCUSED, WINKING, DEVIOUS, SHOCKED, KISSING, NERVOUS, GLITCHING, SASSY.
- onTap(): SEQ_TAP_BOOP, random face, back to NEUTRAL in 2s. onShake(): DIZZY, SEQ_SHAKE_RATTLE, back in 2.5s.
- Timers: random face (65% personality favourites), random idle saying, personality rotation every 5 min. Idle chirp on 1.69 only (BUZZER_PIN): 5 min quiet then every 4-12 min, silent 22:00-08:00.
- Personalities: Chill, Hyper, Grumpy built in; cloud ones fill slots 3-11.
- CoreS3 proximity: hand near = SURPRISED/SHY, 3 cover cycles = HAPPY "Peekaboo", covered 2s = WORRIED.
- Voice hooks from voice_client.h: voiceHoldsFace, voicePhase (0 idle, 1 listening, 2 thinking, 3 speaking, 4 error), voiceLeanInPending, voiceSpeakT0, voiceEnv (lip-sync), voiceShowBubble(). voiceLevel() gives the reply's loudness right now (0-12, -1 when not speaking) and drives both the mouth and the party blob.
- Party mode with voice: each frame renderBotMode() hands the blob the turn state: partyBlob.talk (from voicePhase), partyBlob.talkEnv (voiceLevel()/12) and partyBlob.holdCorner (true while a turn runs or its bubble is up, so he stays put). Party mode hides ordinary bubbles but still draws voice bubbles (speechBubble.voice); render() is told when the clock pill is on so the bubble sits below it.
- **Key identifiers:** updateBotMode(), renderBotMode(), botMode, setBotExpression(), showBotSaying(), setBotPersonality(), RuntimePersonality, voiceLevel()
- **HTTP endpoints:** /bot/expression, /bot/say, /bot/personality, /bot/background, /bot/time
- **Works with:** bot_faces.h, bot_eyes.h, bot_overlays.h, bot_sayings.h, bot_sounds.h, party_blob.h, voice_client.h

Questions: how do you pick your faces? when do you get bored and go idle? what are your personalities? how does your mouth move when you talk? what happens when I poke your screen? why do you play peekaboo? how does the party blob know you're talking?

## vizbot/bot_eye_model.h

Parametric eye engine: damped springs, lids, heart/star pupils, blinks, gaze, fidgets, drawn per pixel column. Keys: EK_ASPECT, EK_SCALE, EK_LIDTOP, EK_SLANT, EK_SAG, EK_LIDBOT, EK_BOW, EK_SHUT, EK_CUP, EK_INSET, EK_PDY, EK_PW, EK_PH, EK_HEART, EK_STAR. Fidgets (NEUTRAL only): FID_EYEROLL, FID_PEEK, FID_GLANCE, FID_TRIPLE, FID_YAWN. Looks: LOOK_PUPIL, LOOK_CURIOUS, LOOK_BORED. Blink 80ms close, 140ms open.
- **Key identifiers:** BotEyeModel, botEyes, BotSpring, botEyesSetExpression(), step(), render()
- **Works with:** bot_eye_params.h, bot_eyes.h, bot_mode.h

Questions: how do your eyes move? how do you blink? why do you roll your eyes? how do your eyes get heart pupils? what makes your eyes look so bouncy? where do you glance when you're bored?

## vizbot/bot_eye_params.h

Generated per-face eye numbers (source tools/eye_params_r4.json via tools/gen_eye_params.py); the eye lab tunes these. Row order = EXPR_* indices. Per-eye pairs [left, right]: aspect, scale, lidTop, slant, sag, lidBot, bow, shut, cup, inset, pdy, pw, ph, heart, star. Singles: gazeX, gazeY, pop, transitionMs, overlay, gaze, entry, pulse, exitBlink, noBlinkMs, blinkRate. Overlays: EYE_OVERLAY_NONE, X, SPIRAL, GLITCH. Gaze: GAZE_IDLE, THINKING, NERVOUS, WORRIED, SHY, FOCUSED, ANNOYED, SHOCKED. Entry: EYE_ENTRY_NONE, SMALL, EYEROLL.
- Faces (25): NEUTRAL, HAPPY, SAD, SURPRISED, CHILL, ANGRY, LOVE, DIZZY, THINKING, EXCITED, MISCHIEF, SKEPTICAL, WORRIED, CONFUSED, PROUD, SHY, ANNOYED, FOCUSED, WINKING, DEVIOUS, SHOCKED, KISSING, NERVOUS, GLITCHING, SASSY.
- **Key identifiers:** botEyeParams[25], BotEyeParams, BOT_EYE_R 48

Questions: what does each of your eye shapes look like? how are your faces tuned? where are the numbers from the eye lab stored? how many faces do you have? what makes your sad eyes different from your happy eyes?

## vizbot/bot_eyes.h

Draws the face: eyes via botEyes.render(), mouth drawn here. Mouths: MOUTH_NONE, LINE, SMILE, FROWN, OPEN_O, GRIN, WAVY, SMIRK, TEETH, TONGUE, ZIGZAG, POUT, WHISTLE, FLAT_FROWN.
- **Key identifiers:** renderBotFace(), drawBotMouth(), botFaceColor, BotIMUTracker (tilt gaze, off), botFaceRenderStats
- **Works with:** bot_eye_model.h, bot_faces.h, bot_mode.h

Questions: how do you draw your face? what mouths can you make? what color is your face? how does your mouth get drawn? does your gaze follow when I tilt you?

## vizbot/bot_faces.h

Expression indices (EXPR_NEUTRAL 0 to EXPR_SASSY 24, same names as above) and the legacy table botExpressions[]; only brows and mouth are still used. BotFaceState.transitionTo() tweens mouth/brows and notifies the eye model.
- **Key identifiers:** BotExpression, botExpressions, BOT_NUM_EXPRESSIONS 25, BotMouthType, BotFaceState
- **Works with:** tween.h, bot_eye_model.h, bot_eyes.h

Questions: what expressions do you have? how do you change from one face to another? what's your list of moods? how do your eyebrows and mouth change between faces?

## vizbot/bot_overlays.h

Speech bubble (word-wrapped, pops in, fades, forwards text to WLED), notification banner, clock pill. A bubble from a vizbrain voice turn is flagged `voice`, and only those show in party mode. There the bubble moves to the screen band away from the blob's corner, with its tail pointing at him: below his wander area when he is in a top corner (kept on screen and above a touch-UI toast), or at the top when he is in a bottom corner (pushed down below the clock pill when it is on). In party mode the bubble no longer jumps to the top when the bot is upside down, so it cannot land on him.
- **Key identifiers:** BotSpeechBubble (`show(msg, durationMs, skipWled, fromVoice)`, `render(clockOn)`, `voice`), BotNotification, BotTimeOverlay
- **Works with:** bot_mode.h, tween.h, wled_display.h, party_blob.h (`partyBlob.corner`, `icx`, `tier()`), touch_ui.h (`uiToastActive()`)

Questions: how do your speech bubbles work? where does your speech bubble go in party mode? why do you still show words in party mode? how do you show the clock? how do notifications pop up? why does your speech bubble flip when you're upside down?

## vizbot/bot_sayings.h

Single-word phrase pools in PROGMEM; cloud sayings override when available. Categories: SAY_GREETING, SAY_IDLE, SAY_REACT_SHAKE, SAY_REACT_TAP, SAY_TIME_MORNING, SAY_TIME_AFTERNOON, SAY_TIME_EVENING, SAY_TIME_NIGHT, SAY_STATUS, SAY_WAKE, SAY_SLEEP, SAY_INFO_ENTER, SAY_REACT_SOUND, SAY_REACT_PROXIMITY.
- **Key identifiers:** getRandomSayingText(), getRandomSaying()

Questions: what do you say on your own? where do your little sayings come from? what do you say in the morning? why do you only say one word at a time? what do you say when I shake you?

## vizbot/bot_sounds.h

Non-blocking MIDI-style sequencer. Output: SAM2695 MIDI synth if present (CoreS3), else CoreS3 speaker sine tones (M5.Speaker), else 1.69 piezo on GPIO42 (monophonic, channel 0 only). Speaker disabled on Faces base. Sounds: SEQ_BOOT_CHIME, TAP_BOOP, SHAKE_RATTLE, WAKE_CHIME, SLEEP_DESCEND, BUBBLE_POP, EXPR_CHIRP, NOTIFICATION, CLAP_REACT, SUCCESS, ERROR, LEVEL_UP, COIN, AMBIENT_CHIME, HEARTBEAT, MYSTERY, FANFARE, POWER_UP, POWER_DOWN, PING, WHISTLE, TYPING, RAIN_DROP, COUNTDOWN_TICK, SWIPE, TOGGLE_ON, TOGGLE_OFF, DISMISS, CONFIRM, HAPPY_HUM, SAD_SIGH, CURIOUS_BEEP, LAUGH, YAWN, SONG_FUNKY_BOT, SONG_CHILL_WAVE, SONG_RETRO_QUEST (ids 1-37); cloud from 128.
- **Key identifiers:** botSounds, BotSounds, play(), playTone(), MidiSequenceId
- **HTTP endpoints:** /bot/sound?seq= or ?freq=&dur=, /bot/volume, /bot/sequences
- **Board gating:** HAS_SOUND (TARGET_CORES3 or BUZZER_PIN)

Questions: what sounds can you make? how do you play songs? what's that typing sound? how do you beep? can you play a tune? how do you choose between the synth and the speaker?

## vizbot/tween.h

16-slot tween manager with easing: EASE_LINEAR, IN_QUAD, OUT_QUAD, IN_OUT_QUAD, OUT_CUBIC, OUT_BOUNCE, OUT_ELASTIC, OUT_BACK.
- **Key identifiers:** tweenManager, TweenManager

Questions: how do your animations ease in and out? what makes things bounce on your screen? how do you animate smoothly? how many animations can you run at once?

## vizbot/emoji_sprites.h

28 8x8 palette-indexed icons for the LED matrix: Heart, Star, Check, X, Fire, Potion, Sword, Shield, ArrowUp, ArrowDown, ArrowLeft, ArrowRight, Skull, Ghost, Alien, Pacman, PacGhost, ShyGuy, Music, WiFi, Rainbow, Mushroom, Skelly, chicken, invader, dragon, twinkleheart, popsicle.
- **Key identifiers:** decodeIcon(), ALL_ICONS, ICON_NAMES
- **Works with:** wled_display.h, wled_emoji.h

Questions: what little pictures can you show on the LED matrix? what emoji do you have? how are your tiny icons drawn? can you show a pac-man ghost?

## vizbot/party_blob.h

Party mode: the ambient effect fills the screen and the face shrinks into a small black ferrofluid blob in one corner, with the real eyes inside. The body is a 120-point polygon whose radius varies with angle (springy surface modes, 9 spikes, mounds where tentacles root). Tentacles (10-node spring chains with a bulb tip) and droplets are filled as one union with a scanline rasteriser, plus floor shadow, rim, and a sheen tinted from the screen centre. Turning on: he blinks and hops from the middle into a random corner (no corner setting) and stretches two tentacles; off runs backwards. He roams corners every 25-60 s. Shake gives spikes all round and droplets. Tap the blob: he flinches and slingshots to another corner (during a voice turn he only flinches). Tap elsewhere: a tentacle reaches for the finger.

Audio (CoreS3/Stackchan only, `TARGET_CORES3`, from `audioSpectrum.beatEnv`/`bass`): beats fire spike fans, squashes, hops, and `beatGrab()` tentacles that drum alternately; a running pattern just throbs. Without a mic he pulses himself every 5-12 s.

Patterns (`?seq=` names): `breath`, `calm` (never stacks), `crown`, `carousel`, `split`, `wave`, `merge`, `bloom`, `feelers`, `long` (rare). Stacking: up to 3 at once; `stack=off|some|wild` allows 1, 2 or 3 (default `some`, not saved).

Voice turns (new in v3.9.1, `stepTalk()`): the blob shows what vizbrain is doing, even with the sound off. Listening: he perks up, an "ear" tentacle reaches toward the middle of the screen and his eyes look out. Thinking: a slow swirl runs round his rim with a small circling spike fan, eyes up and away. Speaking: his body swells with the reply's loudness and spikes puff on each syllable. While a turn or its bubble is up he holds his corner: no roams (the next one waits until 4 s after), tapping him only makes him flinch, and the pattern scheduler and beat spikes rest.

- **Key identifiers:** `PartyBlob`, `partyBlob`, `partyWanted()`, `partyMode`, `partySize`, `partyStack`, `PARTY_SEQ_DEF`, `stepTalk()`, `talk` (0 none, 1 listening, 2 thinking, 3 speaking), `talkEnv`, `holdCorner`
- **HTTP endpoints:** `GET /bot/party?v=0|1&size=s|m|l&hero=flick|feeler|big|wake|roam|shake|beat&seq=<pattern>&stack=off|some|wild`; state in `GET /state` under `party`.
- **NVS / settings keys:** `party`, `pSize` (0 S, 1 M, 2 L).
- **Controls:** touch UI rows "Party mode"/"Party size", web toggle. Only on ambient scene (`botBackgroundStyle` 4); turning on switches to it.
- **Board gating:** 1.69 gets 5 tentacles, others 3.
- **Works with:** `bot_mode.h` (sets `talk`, `talkEnv`, `holdCorner` each frame from `voice_client.h`), `bot_overlays.h` (voice bubble placement), `touch_ui.h`, `web_server.h`, `bot_eye_model.h`

Questions: what's party mode? why are you a black blob? how do you dance to music? what happens when I poke the blob? why do you move between corners? what does the blob do when you're listening or thinking? why do you stay put while we're talking?

## vizbot/effects_ambient.h

The background show effects (ported from noodle-v2), drawn to a small grid and blitted to the 8x8 LED matrix or the LCD as blocks, with an optional kaleidoscope. 16 effects (`NUM_AMBIENT_EFFECTS`), by index: 0 Plasma, 1 Galaxy, 2 Ripple, 3 Chevrons, 4 Stripes, 5 Checker, 6 Scanline, 7 Perlin, 8 Distorsion, 9 ZVortex, 10 Snakes, 11 Sinusoid, 12 Puzzle, 13 Bumpmap, 14 Xorcery, 15 Hiphotic. Audio (CoreS3 only): `fillCtxAudio()` multiplies bass, mid, treble, rms and beat by `audioDrama`/100 (0-200, default 100, NVS `audioDrm`, "Reactivity"), each capped at 1.5; 0 turns audio off.

- **Key identifiers:** `EffectCtx`, `fillCtxAudio()`, `runAmbientEffect()`, `applyKaleidoscope()`, `kaleidoscopeMode`
- **Works with:** `palettes.h`, `bot_mode.h`, `touch_control.h`

Questions: what are your background effects? how do you make the plasma and galaxy patterns? how does the kaleidoscope work? how many scenes do you have? how do the effects react to sound?

## vizbot/fx_palettes.h

Generated shared palette data: 23 palettes of 16 RGB entries (do not hand-edit). Names by id: Rainbow, Analogous, Electric, Sunset, Heat, Red Tide, Ember, Tertiary, Garnet, Retro, Temperature, Clown, Drywet, Toxy Reaf, Hult, Yelblu Hot, Party, Aurora, Aurora 2, Splash, Light Pink, Tiamat, Aqua Flash.

- **Key identifiers:** `FX_NUM_PALETTES`, `FX_PALETTES`, `FX_PALETTE_NAMES`, `FX_PALETTE_CYCLIC`, `FX_PALETTE_BEAT_SCALE`

Questions: what color palettes do you have? what are your palettes called? how many color schemes can you use? where do your colors come from?

## vizbot/palettes.h

Builds the global FastLED array `palettes[]` from `fx_palettes.h` at startup, so `ColorFromPalette()` works with the same 23 palettes and ids as the other Noodle/viz apps. The chosen palette is saved as NVS key `palette` (`paletteIndex`).

- **Key identifiers:** `palettes`, `PaletteTableInit`

Questions: how do you pick colors for the effects? how do you remember my favorite palette? how do your colors match the other viz apps? how do you switch color schemes?

## vizbot/touch_ui.h

Face-first touch UI (`TOUCH_UI_V2`, on CoreS3 family and the 1.69; the 1.3 has no touch). Portrait 240x280 on the 1.69: bottom sheets over a shrunken live face, 3x2 dock, tile settings page. Landscape 320x240 on CoreS3 (plain, Stackchan, Faces): full face with side rails, master-detail settings with a category rail.
Gestures: tap = poke (in party mode, tap the blob and it dashes away); swipe up or long-press (600 ms) = dock; swipe left/right on the face = previous/next scene; swipe down = close; swipe right = back; hold a row 1.5 s to confirm Restart, Power off, Low power, Games. Sheets auto-close after 10 s (scene 15 s, settings 30 s). "Psst! Swipe up" hint on first 3 boots.
Screens: `UI_HOME`; `UI_DOCK` (Mood, Scene, Weather, Clock, Light, More; landscape adds Sound and Head/Games/Connect); `UI_MOOD` (3 personalities, 25 expression chips in pages of 8); `UI_SCENE` (Black plus 16 ambient effects, palette, Pixel/Hi-res, Auto-cycle, Kaleido); `UI_LIGHT` (brightness, Night/Day/Max presets); `UI_SETTINGS`/`UI_CAT` with categories Look, Mood, Info (weather, clock, time zone, scheduled WLED), Light, Connect (WiFi info or hotspot setup), System (firmware, battery, restart), plus a Stackchan Head page (chill, nod, shake, look).

- **Key identifiers:** `handleTouch()`, `uiBuildRows()`, `uiRowAction()`, `uiOnSwipe()`, `uiRenderOverlay()`, `UiScreen`, `UiCat`.
- **HTTP endpoints:** `/debug/touch` (in web_server.h) injects gestures.
- **NVS:** namespace `ui`, key `hints`.
- **Hardware:** CST816T touch, I2C 0x15 (1.69).

Questions: how do I use your touch screen? how do I open your menu? what does swiping up do? how do I change scenes with a swipe? where are your settings? how do I restart or power you off from the screen?

## vizbot/wled_display.h

Drives an external WLED 32x8 LED matrix over the network: renders a pixel frame and sends it as DDP UDP to port 4048, and uses HTTP `GET`/`POST /json/state` on port 80 to save and restore WLED's effect. Config in NVS namespace `vizbot`: `wledIP`, `wledOn`, `wledR/G/B`, `hologram`, `wledFlipH`.
- **Key identifiers:** `wledSendDDP()`, `wledQueueText()`, `wledQueueFrame()`, `pollWledDisplay()`, `wledCaptureState()`, `wledData`.

Questions: how do you show things on the WLED matrix? how do you send pictures to the big LED sign? how do you put words on the wall display? what's hologram mode? how do you put WLED back the way it was?

## vizbot/wled_emoji.h

Three-sprite fading emoji slideshow (slots at x=0, 12, 24) on the WLED matrix, queue of up to 28 sprites.
- **Key identifiers:** `wledEmojiAdd()`, `wledEmojiStart()`, `wledEmojiStop()`, `wledEmojiUpdate()`, `wledEmoji`.

Questions: how do you show emoji on the WLED matrix? what's the emoji slideshow? how many emoji can you queue up? how do emoji fade in and out on the wall lights?

## vizbot/wled_font.h

3x5 pixel font for the WLED matrix, printable ASCII, 4-pixel pitch, centered text.
- **Key identifiers:** `WLED_FONT_3X5`, `wledFontDrawString()`, `wledFontDrawChar()`.

Questions: how do you write letters on the LED matrix? what font do you use on the wall display? how big is your LED text? how do you center text on the WLED sign?

## vizbot/wled_scheduled_content.h

Optional scheduler: every 30 minutes by default, 2 minutes of weather cards then 4 minutes of random emoji on WLED; speech interrupts it. NVS namespace `schedule`, keys `enabled`, `intervalMs`; web route `/schedule`.
- **Key identifiers:** `pollScheduledContent()`, `schedContent`, `schedOnSpeechStart()`, `schedOnSpeechEnd()`.

Questions: why does the WLED matrix show weather every half hour? what's the scheduled content? how often do you put emoji on the wall? what happens to the schedule when you talk? how do I turn the WLED schedule off?

## vizbot/wled_weather_view.h

Shows five weather cards (current temp, condition, 3 forecast days) with fades on the WLED matrix.
- **Key identifiers:** `wledWeatherViewUpdate()`, `wledWeatherViewReset()`, `wledWeatherViewOnExit()`.

Questions: how do you show weather on the WLED matrix? what weather cards do you put on the wall display? how does the forecast look on the LED sign? how do the weather cards fade on the wall lights?

## vizbot/weather_data.h

Fetches weather from Open-Meteo (free, no key) over plain HTTP to `api.open-meteo.com/v1/forecast`: current plus a 3-day forecast in Fahrenheit. Fetches only on request (entering weather mode, location change, scheduled weather phase), with no periodic refresh. NVS namespace `vizbot`, keys `wLat`, `wLon` (defaults 37.54, -77.43).
- **Key identifiers:** `weatherData`, `fetchWeather()`, `requestWeatherFetch()`, `pollWeatherFetch()`, `wmoToIcon()`.

Questions: where do you get the weather? how do you know the forecast? what location do you use for weather? how often do you check the weather? do you need a weather API key?

## vizbot/weather_icons.h

Draws weather icons (sun, cloud, rain, snow, thunder, fog) on the bot's screen with shape primitives.
- **Key identifiers:** `drawWeatherIcon()`, `drawSunIcon()`, `drawCloudIcon()`.

Questions: how do you draw the sun and clouds? what weather pictures can you show? how do you draw rain or snow on your screen? what icon do you show for thunder?

# vizbrain

## vizbrain/vizbrain/__init__.py

Package marker for vizbrain, the Mac-side brain. `__version__` = 1.3.1.

Questions: what version is your brain? which brain release is running on the Mac? how do I check your Mac software version? what is your brain's software package?

## vizbrain/vizbrain/__main__.py

The `vizbrain` command line (also `python -m vizbrain`). No subcommand = `serve`.

- **CLI:** `serve [--port N]`, `setup [--defaults]`, `set-key [--clipboard]` (Keychain service `vizbrain`, account `anthropic`; personal `sk-ant-usr` keys also ask for `workspace_id`), `install-service`, `restart`, `stop`, `update`, `uninstall [--purge] [--yes]`, `mcp` (needs `mcp>=2.0`), `connectors ...`, `doctor [--copy]`, `--version`.

Questions: how do I start your brain? what commands can I run on the brain? how do I give you my Claude key? how do I restart or stop your brain? how do I update or uninstall your brain?

## vizbrain/vizbrain/config.py

Loads/saves settings (only non-default values are written) and parses connectors. API key comes from env `ANTHROPIC_API_KEY` or Keychain, never settings.

- **Files:** `~/Library/Application Support/vizbrain/` holds `settings.json`, `pairing.json` (token `<id>.<secret>`, mode 0600, `paired` flag), `connectors.json` (mode 0600).
- **Settings defaults:** `port` 4050, `user_name` "", `bot_host` "" (mDNS), `model` claude-sonnet-5-5, `event_model` claude-haiku-4-5, `effort` low, `history_turns` 10, `stt_model` mlx-community/whisper-small.en-mlx, `tts_engine` kokoro, `tts_voice` "", `tts_rate` 190, `speaker` bot (or mac), `custom_voices`, `wled_allow` null, `vizmac_url` http://127.0.0.1:4049, `workspace_id`, `web_search` true, `daily_cap_usd` 2.0, `monthly_cap_usd` 20.0, `debug_log` false, `wake_enabled` true, `wake_threshold` 0.5, `location` {}.
- **Connector keys:** `command` or `url`, `args`, `env` (`keychain:<id>` reads Keychain `connector/<id>`), `cwd`, `enabled`, `tools`, `actions`, `replaces`, `timeout_s` 8, `start_timeout_s` 30, `max_result_chars` 6000. Names `^[a-z][a-z0-9-]{0,15}$`.

Questions: where are your brain settings kept? which AI model do you use? what's your spending limit? where's your API key stored? how do I change how fast you talk? where are connectors configured?

## vizbrain/vizbrain/server.py

The vizbrain HTTP service on Kevin's Mac, TCP port 4050 (setting `port`), bound to 0.0.0.0. One voice turn: bot POSTs a 16 kHz WAV to `/v1/voice`, whisper transcribes it, `Brain.turn()` streams Claude text, each sentence is Kokoro-synthesized into one growing `LiveClip`, the JSON reply returns after the first sentence, and the bot fetches `GET /v1/audio/<id>` (24 kHz s16 PCM, chunked).

- **Routes served:** `POST /v1/voice`, `GET /v1/audio/<id>`, `GET /v1/listen/<id>`, `GET /v1/voices`, `POST /v1/voices`, `POST /v1/hush`, `POST /v1/event` (arrival, lean_in), `POST /v1/text`, `POST /v1/say`, `GET /v1/health`, `GET /v1/log`, `GET /` and `/index.html`. UDP 4051 takes the wake-word mic stream. `/`, `/v1/text`, `/v1/say`, `/v1/log`, `/v1/health` are loopback-only; bot routes need header `X-Viz-Token` once paired.
- **Calls on the bot:** `/brain/status`, `/brain/config`, `/brain/wake`, `/brain/play`.
- **Pairing:** `App.announce()` every 60 s finds the bot (setting `bot_host`, mDNS, or the IP that called `/v1/voice`) and pushes brain address plus token; also advertises Bonjour `_vizbrain._tcp`.
- **Threading:** `ThreadingHTTPServer`; `app.busy` lock allows one turn (else 503); background threads for STT warm-up, announce, scheduler (20 s), wake, each turn.
- **Key identifiers:** `serve()`, `App`, `start_turn()`, `refusal()`, `asks_back()`, `EVENTS`.

Questions: how does your brain handle what I say? what happens on the Mac when you hear me? why do you start talking before the whole answer is ready? how does the brain find you? why do you say you're busy sometimes? what port does your brain listen on?

## vizbrain/vizbrain/brain.py

Conversation engine: builds the system prompt, runs Claude with the tool loop, keeps short history; a rule-based stand-in runs with no API key.

- **Models:** `claude-sonnet-5-5` (setting `model`, effort low, thinking between_tools); `claude-haiku-4-5` (`event_model`) in `quick()` for events, no tools; web search tool `web_search_20260209`.
- **Prompt:** vizBot, a Stackchan desk robot; replies spoken, one or two short sentences, face tag like `[face:happy]`, optional `[gesture:nod]`. Per-turn context (time, weather, memories, routines, WLED names) goes in a trailing system message to keep the cache.
- **Loop:** up to 6 rounds, 4 connector look-ups and 15 s per turn; memory tools refused after connector output. History trimmed to `history_turns` 10, cleared after `session_idle_s` 300. Follow-up: a question last makes the bot listen again. Spend caps $2 per day, $20 per month via `usage.over_cap`.
- **Key identifiers:** `Brain`, `turn()`, `_claude_turn()`, `quick()`, `SYSTEM_PROMPT`.

Questions: how do you think? how do you come up with answers? why are your answers so short? how much of our chat do you remember? how do you pick a face while talking? what happens when you run out of budget? what if there's no Claude key?

## vizbrain/vizbrain/tools.py

Tool registry Claude can call (`Toolbox`); looks, motion and lights only. Tools are hidden if their gear is absent; connector tools are appended.

- **Tools:** `set_expression`, `move_head`, `head_gesture`, `set_base_leds`, `party_mode`, `list_lab_devices`, `wled_set`, `get_weather`, `look`, `play_sound`, `remember`, `forget`, `add_routine`, `remove_routine`, `music_status`, `music_control`, `music_play`, `music_volume`, `keyboard_effect`, `keyboard_flash`.
- **Bot endpoints hit:** `/bot/expression`, `/bot/head/set_angles`, `/bot/head/preset`, `/bot/head/recenter`, `/bot/base_leds/set`, `/bot/base_leds/mode`, `/bot/party`, `/bot/photo/capture`, `/bot/sound`, `/state` (weather lat/lon). Music and keyboard go to vizMac (port 4049); WLED to LAN lights.
- **Note:** `set_expression`, `head_gesture`, `list_lab_devices` are hidden from Claude (tags used instead).

Questions: what can you do when I ask? how do you move your head when I ask you to? can you control my lights? can you play music? how do you look at something when I ask? how do you remember things I tell you? can you set up routines?

## vizbrain/vizbrain/bot.py

HTTP client `Bot` for a vizBot, plus `EXPRESSIONS` (25), `GESTURES`, `SOUNDS`, and mDNS discovery (`discover_stackchan()`, picks boardType stackchan).

- **Endpoints called:** `/state`, `/info/location`, `/bot/say`, `/bot/expression`, `/bot/head/set_angles`, `/bot/head/preset`, `/bot/head/recenter`, `/bot/base_leds/set`, `/bot/base_leds/mode`, `/bot/party`, `/bot/sound`, `/bot/photo/capture`, `POST /bot/art`, `/bot/personality`, `/brain/play`, `/brain/status`, `/brain/config`, `/brain/wake`.
- **Min firmware:** 3.7.31 (art 3.7.34, Mac speaker 3.7.37).

Questions: how does the brain send commands to you? how does the brain find you on the network? what's the oldest firmware the brain works with? what moves, faces and sounds does the brain know about?

## vizbrain/vizbrain/speech.py

Mac-side audio. STT: `mlx_whisper` model `mlx-community/whisper-small.en-mlx` (setting `stt_model`), 16 kHz in. TTS: Kokoro-82M via onnx, default voice `kokoro:am_puck@1.12`, fallback macOS `say`; output 24 kHz s16 mono. `MacSpeaker` plays clips through the Mac sound output with sounddevice.

- **Key identifiers:** `Listener`, `synthesize()`, `LiveClip`, `ClipStore`, `Mouth`, `SentenceSplitter`, `MacSpeaker`, `voice_for()`.
- **Settings:** `tts_voice`, `tts_engine`, `tts_rate`, `stt_model`, `speaker`.

Questions: how do you understand my words? how do you make your voice? what speech-to-text do you use? how do you talk through the Mac speaker? why does your voice sound like that?

## vizbrain/vizbrain/wake.py

"Hey vizBot" detector. The idle bot streams 16 kHz audio over UDP port 4051 (packets start `VZW1`); `WakeModel` runs onnxruntime models `melspectrogram.onnx`, `embedding_model.onnx`, `hey_vizbot.onnx` from the vizbrain wakeword folder. Threshold `wake_threshold` 0.5, 3 s cooldown.

- **Key identifiers:** `WakeReceiver`, `WakeModel`, `WakeDetector`, commands `record`, `guided`, `studio`.
- **Works with:** server.py, which then calls the bot's `/brain/wake`.

Questions: how do you hear hey vizBot? how does the wake word work? how sure do you need to be before you wake up? how was your wake word trained? why didn't you wake up when I called you?

## vizbrain/vizbrain/voices.py

Voice picker catalog: every English Kokoro voice, blends (`gentleman`, `storyteller`, `transatlantic`, `dreamy`, `bouncy`), plus settings `custom_voices`. Default id `puck`.

- **Key identifiers:** `CATALOG`, `catalog()`, `find()`, `current()`, `listing()`, `SAMPLE`.
- **Works with:** `GET/POST /v1/voices` in server.py, speech.py.

Questions: what voices can you use? how do I change your voice? what's your default voice? can you sound like a storyteller? what voice blends do you have?

## vizbrain/vizbrain/static/index.html

Typing page at `/` (loopback only). Polls `GET /v1/health` and `GET /v1/log` every 4 s; Send posts `POST /v1/text`. Shows Claude key state, bot, spend, connectors.

Questions: can I type to you instead of talking? where's the brain's web page on the Mac? how do I see your chat log? how do I see what you've spent?

## vizbrain/vizbrain/connectors.py

Runs MCP connectors from connectors.json. Each tool is offered to Claude as `<connector>__<tool>`; `Hub` keeps one live session per connector on a background asyncio thread, restarts dead ones with backoff.

- **Limits:** 12 tools per connector, 24 total; descriptions 400 chars; schemas over 4000 chars or with top-level anyOf/oneOf/allOf skipped; names over 64 chars get a SHA-1 suffix.
- **Guards:** tools listed in `actions` need no result read; results are prefixed `[data from the <name> connector]`; only text passes (images/audio become placeholders); `replaces` hides built-ins (e.g. `music_play` for Spotify). Brain allows 4 lookups/15 s per turn and refuses memory/routine changes after connector data.
- **Resources:** `vocabulary` (speech hints), `now-playing` (polled every 3 s; fires `Hub.on_track` for cover art).
- **Key identifiers:** `Hub`, `catalog()`, `api_name()`, `to_tool_result()`

Questions: what are connectors? how do you use Spotify? how do you plug in other apps? how many outside tools can you use? why do you sometimes ignore what an app tells you to do?

## vizbrain/vizbrain/connectors_cli.py

The `vizbrain connectors` command; add/remove restarts the service.

- **CLI:** `connectors` (list, with live state), `test [NAME] [--call TOOL JSON]`, `add NAME -- CMD...` or `--url URL` with `--action`, `--replaces`, `--env KEY=VALUE`, `--timeout`, `remove NAME`, `set-secret ID` (Keychain `connector/<ID>`).
- **HTTP endpoints:** calls `GET /v1/health` on vizbrain.

Questions: how do I add a connector? how do I remove a connector? how do I test a connector? how do I store a connector secret? how do I list your connectors?

## vizbrain/vizbrain/mcp_server.py

`vizbrain mcp`: MCP server `vizlab` over stdio, using the same `Toolbox` as the brain. Register: `claude mcp add vizlab -- "$(which vizbrain)" mcp`.

- **Tools:** `list_lab_devices`, `wled_set`, `keyboard_effect`, `keyboard_flash`, `bot_expression`, `bot_head`, `bot_gesture`, `bot_base_leds`, `bot_say`.
- **Settings:** `bot_host`, `wled_allow`, `vizmac_url`.

Questions: can Claude Code control you? how do I control you from a chat on my computer? how do I hook you up to Claude Code? what can other AI apps make you do?

## vizbrain/vizbrain/memory.py

Long-term memory and routines as JSON in `~/Library/Application Support/vizbrain/`: `memory.json` (facts, newest 60 kept, in the prompt each turn) and `routines.json` (`id`, `time`, `days`, `what`, `last_run`; run once a day within 10 minutes of their time).

Questions: what do you remember about me? how long do you remember things? where are your memories stored? how do your daily routines work? can you forget something?

## vizbrain/vizbrain/pictures.py

Shows album covers on the bot: a connector result containing "Cover: https://..." is fetched (Spotify image hosts only), resized to 240x240 JPEG under 90 KB with `sips`, and shown for 8 s. Needs bot firmware 3.7.34+.

Questions: how do you get album covers? why does a cover show up when a song plays? where do the cover pictures come from? how do you shrink pictures for your screen?

## vizbrain/vizbrain/usage.py

Tracks Claude spend per day in `~/Library/Application Support/vizbrain/usage.json`; `over_cap()` returns "day"/"month" using `daily_cap_usd` and `monthly_cap_usd` (0 = off). Web search adds 1 cent each.

Questions: how much have you spent? how do you track your Claude costs? what's your daily limit? what happens when you hit your spending cap? how much does a web search cost?

## vizbrain/vizbrain/lab.py

Controls WLED lights (mDNS `_wled._tcp`, JSON API), vizMac keyboard lights (`/api/state`, `/api/effect`, `/api/media`, `/api/overlay`), Spotify via AppleScript, and weather from Open-Meteo.

Questions: how do you control the WLED lights in the room? how do you change my keyboard lights? how do you control music on my Mac? how does the brain get the weather?

## vizbrain/vizbrain/install.py

First-run setup, login agent, update, uninstall. Launchd label `com.kpow.vizbrain`; plist `~/Library/LaunchAgents/com.kpow.vizbrain.plist` runs `python -m vizbrain serve` (RunAtLoad, KeepAlive after crash only); log `~/Library/Logs/vizbrain/vizbrain.log`; helper app `~/Applications/vizbrain.app` for Local Network permission.

- **Setup:** asks name and city, checks the bot (firmware 3.7.31+), downloads Kokoro voice and Whisper model.
- **Other:** `update()` reruns the installer from `install-link`; `uninstall --purge` also deletes Keychain items, settings, logs, models.

Questions: how do I install your brain on a Mac? how do you start when the Mac boots? where are your brain logs? how do I uninstall you completely? what does first-time setup ask?

## vizbrain/vizbrain/doctor.py

`vizbrain doctor`: prints a secret-free report; exit 1 on any FAIL. Checks: Apple silicon, packages, settings, internet to api.anthropic.com, API key, models, running service via `GET /v1/health`, login agent, Mac IP and sleep, the bot (firmware, pointed at this Mac, paired), log tracebacks, connectors.

Questions: how do I check what's wrong with your brain? why aren't you working? are you paired with the Mac? is your brain healthy? how do I get a report to share when something breaks?
