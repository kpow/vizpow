# vizBot hardware reference (firmware 3.9.1)

Facts come from vizbot/config.h, platformio.ini and the other vizbot sources; citations are file:line. "Unclear from code" means the code does not say (for example, what M5Unified or the StackChan-BSP library handle internally).

## Stackchan
Environment `stackchan`, BOARD_TYPE "stackchan" (platformio.ini:70, config.h:208-211). It is a CoreS3 plus the StackChan base, built with BOARD_M5CORES3 and BOARD_HAS_STACKCHAN_BASE (platformio.ini:73-74).

- **Chip, flash, PSRAM:** ESP32-S3 on the m5stack-cores3 board definition (platformio.ini:71). Flash size 16MB, flash mode forced to DIO (platformio.ini:77-78). PSRAM is 8MB per comments (display_lcd.h:121, voice_client.h:33); memory type not set in platformio.ini, unclear from code.
- **Display:** 320x240 landscape, ILI9342C, brought up by M5.begin() through M5Unified (config.h:168-170, display_lcd.h:185, vizbot.ino:404-405). Backlight and reset go through the AW9523 I2C expander (config.h:164). SPI pins and clock: unclear from code. Rendering goes to a 16-bit full-screen sprite in PSRAM (display_lcd.h:123-125).
- **Touch (screen):** FT6336 capacitive at I2C 0x38, interrupt on GPIO21, driven by M5Unified (config.h:165-166).
- **I2C bus:** internal bus SDA GPIO12, SCL GPIO11 (config.h:162-163), shared by touch, IMU, IO expander, head touch, INA226 and the camera control bus, so a mutex guards it (stackchan_base.h:40-53, stackchan_camera.h:22-25).
- **IMU:** BMI270 through M5Unified (boot_sequence.h:458, vizbot.ino:405). Its I2C address is unclear from code.
- **Microphone:** dual MEMS mic on the ES7210 codec via M5.Mic (audio_analysis.h:10). 16 kHz samples (audio_analysis.h:72, voice_client.h:43). Mic and speaker share I2S_NUM_1, so voice turns swap Speaker.end() and Mic.begin() (voice_client.h:27-30).
- **Speaker:** M5.Speaker (bot_sounds.h:846-858). Fallback tones play at 48000 Hz (bot_sounds.h:847), voice replies at 24000 Hz (voice_client.h:55). The amplifier part number is unclear from code.
- **Camera:** GC0308, QVGA RGB565, one frame buffer in PSRAM, 20 MHz xclk. Data pins D0-D7 are GPIO 39, 40, 41, 42, 15, 16, 48, 47; VSYNC 46, HREF 38, PCLK 45 (stackchan_camera.h:98-109, 112-116). Control (SCCB) shares the internal I2C bus on port 1 (stackchan_camera.h:96, 118).
- **Proximity and light:** LTR-553 at I2C 0x23 (proximity_light.h:17).
- **Servos:** SCS0009 servos, yaw ID 1 and pitch ID 2 (config.h:296-297), driven by the BSP's `Motion` object, which owns the serial servo bus from a 20 ms task (stackchan_base.h:30-35). Pitch is clamped to 25-85 degrees, home 48 degrees (config.h:298-300, stackchan_base.h:245-248). Yaw range is documented as plus or minus 128 degrees (web_server.h:2174). Zero positions come from NVS namespace "servo", seeded to yaw 460 and pitch 500 raw steps when absent (stackchan_base.h:58-65). Servo power rail is VM_EN on the IO expander and must be on before servo_init() (stackchan_base.h:91-98).
- **IO expander:** PY32L020 at I2C 0x6F (config.h:292).
- **Head touch:** Si12T at I2C 0x68 with three pads, front, middle, back (config.h:293, stackchan_touch.h:14-17). A tap needs full strength, level 3 (stackchan_touch.h:110-115).
- **LEDs:** a 12-LED WS2812C ring behind the IO expander (config.h:295, stackchan_base.h:160-166); expander pin is IO14 in config.h but "pin 13" in a stackchan_base.h comment. A 64-pixel WS2812B buffer is also registered on DATA_PIN GPIO38, labelled "RGB LED GPIO38" at boot (config.h:161, boot_sequence.h:248, 438).
- **Buttons:** unclear from code; head touch pads are the physical input.
- **Power and battery:** INA226 monitor at I2C 0x41 gives battery voltage and current (config.h:294, stackchan_base.h:387-395). No ADC pin, divider or low-battery cutoff is defined for this board.
- **Radio:** WiFi TX power 19.5 dBm, "USB powered, needs range" (config.h:65), applied with esp_wifi_set_max_tx_power (boot_sequence.h:348).
- **Grove and MIDI:** SAM2695 MIDI synth at 31250 baud. Port C TX GPIO17, RX GPIO18 (default); Port A TX GPIO2, RX GPIO1; chosen at runtime (config.h:366-380).
- **Voice:** VOICE_ENABLED is set only on this board, for push-to-talk with vizbrain (config.h:304).

Questions: what hardware are you made of? what chip is in you? what servos do you have? where's your camera wired? what sensors do you have? how is your battery measured? how much memory do you have?

## M5Stack CoreS3
Environment `m5cores3`, BOARD_TYPE "m5cores3" (platformio.ini:53, config.h:203-204). Same CoreS3 core as the Stackchan without the base.

- **Chip, flash, PSRAM:** ESP32-S3, board m5stack-cores3, flash 16MB, flash mode DIO (platformio.ini:54, 60-61). PSRAM 8MB per comment (display_lcd.h:121).
- **Display and touch:** 320x240 ILI9342C and FT6336 (0x38), same as Stackchan, via M5Unified (config.h:164-170, display_lcd.h:185).
- **I2C and IMU:** internal bus SDA 12, SCL 11 (config.h:162-163). IMU is BMI270 via M5Unified (boot_sequence.h:458).
- **Mic and speaker:** same as Stackchan (ES7210 mic, M5.Speaker); the sound engine falls back to M5.Speaker when no MIDI synth is found (bot_sounds.h:845-858).
- **Camera, servos, head touch:** none; the camera code compiles only with BOARD_HAS_STACKCHAN_BASE (stackchan_camera.h:4). Voice is also off (config.h:291-305).
- **Proximity and light:** LTR-553 at 0x23 (proximity_light.h:4, 17).
- **LEDs:** only the placeholder 64-pixel buffer on GPIO38; no base LED strip (config.h:161, 281-283).
- **Buttons, battery:** unclear from code; no pins or monitor defined.
- **Radio:** 19.5 dBm (config.h:65).
- **Grove and MIDI:** same SAM2695 ports as Stackchan (config.h:364-381).

Questions: what's a plain CoreS3? how is the bare CoreS3 different from you? does the plain CoreS3 have a camera? what screen does the CoreS3 have?

## CoreS3 in Faces Bottom3
Environment `faces`, BOARD_TYPE "faces" (platformio.ini:91, config.h:225-227). A CoreS3 in an M5Stack Faces Bottom3 game base.

- **Chip, flash, partitions:** ESP32-S3, flash 16MB, mode DIO (platformio.ini:109, 115-116). Custom partition table partitions_kfun.csv with the app at offset 0x510000 (ota_1); a chooser sits in the factory partition and the kFun games in ota_0 (platformio.ini:93-95, 117-121, faces_chooser.h:9-11).
- **Display, touch, IMU, proximity:** identical to the plain CoreS3 (320x240, FT6336, BMI270, LTR-553).
- **LEDs:** ten SK6812 (two rows of five), GRB, on GPIO13, driven by FastLED/RMT (config.h:228-231, faces_base.h:24-27, 54).
- **Mic and speaker:** the ES7210 mic and speaker are kept off at boot, because their I2S bit clock is GPIO13, the same pin as the LED data line (vizbot.ino:408-419). Turning on Audio FX hands GPIO13 to the mic and blanks the strips; turning it off gives them back (audio_spectrum.h:139-167). The built-in speaker sound path is disabled on this base (bot_sounds.h:849-854). An external MIDI synth on Port A or C still works (bot_sounds.h:851-852).
- **Gamepad panel:** Gamepad3 at I2C 0x08, register 0x00, active-low bits; START plus SELECT held 800 ms returns to the chooser (faces_chooser.h:62-66, 88-102).
- **Servos, head touch, power monitor, IO expander:** none (faces_base.h:9-13). Camera and voice are not built.
- **Radio:** 19.5 dBm (config.h:65).

Questions: what's the Faces game base? why can't the Faces base make sound and light at once? how many lights does the Faces base have? how does the Faces gamepad work? where do the games live on the Faces?

## Waveshare ESP32-S3-Touch-LCD-1.69 (pendant)
Environment `lcd-169` (the default env), BOARD_TYPE "esp32s3-lcd169" (platformio.ini:12, 26; config.h:199-200). Flags: BOARD_ESP32S3_LCD_169, BOARD_HAS_RING_LEDS, BOARD_HAS_PSRAM (platformio.ini:29-34).

- **Chip, flash, PSRAM:** ESP32-S3 on esp32-s3-devkitc-1, flash 16MB, memory type qio_opi, 8MB OPI PSRAM (platformio.ini:27, 31-37).
- **Display:** ST7789V2, 240x280, offset_y 20, SPI2_HOST. Pins SCK 6, MOSI 7, CS 5, DC 4, RST 8, backlight GPIO15 (config.h:96-106). SPI write clock 80 MHz, read 16 MHz, 3-wire, colour inversion on, RGB order normal; backlight PWM 44.1 kHz on LEDC channel 7 (display_lcd.h:214-218, 220, 239-243, 249-252).
- **Touch:** CST816T at I2C 0x15, reset on GPIO13, auto-sleep disabled through register 0xFE (config.h:112, touch_control.h:28, 146-158).
- **I2C bus:** SDA GPIO11, SCL GPIO10, shared by touch and IMU (config.h:92-93, 111).
- **IMU:** QMI8658 at 0x6B, accelerometer 4 g at 250 Hz, gyro 512 dps at 896.8 Hz (boot_sequence.h:298-311, 460).
- **Mic:** none ("No mic on the 1.69", stackchan_leds.h:430).
- **Speaker:** none; a passive piezo buzzer on GPIO42, LEDC channel 0, 10-bit duty, started at 2000 Hz (config.h:116-118, buzzer.h:20-32).
- **Camera, servos:** none.
- **LEDs:** 5x5 WS2812B matrix, GRB, serpentine, on GPIO17, powered from 5 V (config.h:245-251, ring_leds.h:21-24). A 64-pixel WS2812B buffer is also registered on DATA_PIN GPIO14, described as an external matrix "if used" (config.h:91, boot_sequence.h:248, 440). The matrix defaults to screen mode (stackchan_leds.h:155-157).
- **Buttons:** PWR button on SYS_OUT GPIO40 (low while down); holding it 2 s powers off. GPIO41 (SYS_EN) is held high at start to keep power latched (config.h:122-123, power_button.h:12-14, 27, vizbot.ino:349-351).
- **Battery and power:** battery sense on GPIO1 (ADC1) through a 200k/100k divider, ratio 3.0 (config.h:125-128, battery_adc.h:17-21). Low-battery cutoff 3300 mV held 10 s, re-arm 100 mV above it (config.h:131-133). No charge-status or VBUS pin; USB host presence uses usb_serial_jtag_is_connected() (battery_adc.h:29-33, 142).
- **Low power mode:** CPU 80 MHz, backlight 60 percent, radio off (config.h:257-266).
- **Radio:** WiFi TX power 15 dBm (config.h:48-50).
- **Grove and MIDI:** none; MIDI is CoreS3-only (config.h:364).

Questions: what's inside the pendant? how big is the pendant screen? does the pendant have a microphone? how does the pendant's battery work? why is the pendant's wifi turned down? what buttons does the pendant have?

## Waveshare ESP32-S3-LCD-1.3
Environment `lcd-13`, BOARD_TYPE "esp32s3-lcd13" (platformio.ini:42, config.h:201-202). Flag BOARD_ESP32S3_LCD_13.

- **Chip, flash, PSRAM:** ESP32-S3 on esp32-s3-devkitc-1, flash 16MB, memory type qio_opi (platformio.ini:43-48). Unlike lcd-169, the env does not set BOARD_HAS_PSRAM (platformio.ini:44-46).
- **Display:** ST7789VW, 240x240, no row offset. Pins SCK 40, MOSI 41, CS 39, DC 38, RST 42, backlight GPIO20 (config.h:144-155). SPI write clock 40 MHz (display_lcd.h:216).
- **Touch:** none (config.h:157).
- **I2C and IMU:** SDA GPIO47, SCL GPIO48 (config.h:141-142). The IMU path is the same as the 1.69 (QMI8658 at 0x6B in boot_sequence.h:298, 460); the on-screen boot label wrongly says SDA 11 / SCL 10 (boot_sequence.h:450).
- **Mic, speaker, camera, servos, buzzer:** none defined.
- **LEDs:** DATA_PIN GPIO14 external 8x8 matrix "if used" (config.h:140). No ring matrix.
- **Buttons:** unclear from code.
- **Battery:** described as battery powered (config.h:9, 139) but no ADC pin or power latch is defined.
- **Radio:** TARGET_LCD profile, 15 dBm (config.h:50).

Questions: what's the 1.3 inch board? does the little square screen have touch? what pins does the 1.3 screen use? does the 1.3 have a battery?

## ESP32-S3-Matrix 8x8
Waveshare ESP32-S3-Matrix, BOARD_ESP32S3_MATRIX, BOARD_TYPE "esp32s3-matrix" (config.h:7, 197-198). There is no platformio env for it, so it cannot be built from platformio.ini as shipped (platformio.ini has only lcd-169, lcd-13, m5cores3, stackchan and faces).

- **Chip, flash, PSRAM:** unclear from code.
- **Display:** the 8x8 LED matrix only (DISPLAY_LED_ONLY, config.h:29). 64 WS2812B on DATA_PIN GPIO14, RGB colour order (config.h:85, boot_sequence.h:248).
- **I2C and IMU:** SDA GPIO11, SCL GPIO12 (config.h:86-87). IMU follows the generic non-CoreS3 path (QMI8658, boot_sequence.h:298); the exact chip on this board is not stated, unclear from code.
- **Touch, mic, speaker, camera, servos:** none.
- **Power:** power-save profile, with a WiFi-on-when-USB-detected flag and a 1500 ms USB wait (config.h:30-33).
- **Radio:** WiFi TX power 8.5 dBm, "phone is nearby" (config.h:36). Cloud sync is not enabled for this target (config.h:387-389).

Questions: what's the 8x8 matrix board? can you still run on the LED matrix? why isn't the matrix board built anymore? how strong is the matrix board's wifi?

## Known quirks
1. CoreS3 flash chips can hang the bootloader in QIO mode (reset loop, black screen), so the m5cores3, stackchan and faces envs force DIO (platformio.ini:58-60, 76-77, 114-115).
2. The 1.69 fails to associate or get DHCP at 19.5 dBm, so it runs at 15 dBm (config.h:48-50). WiFi.setTxPower no-ops early, so the code uses esp_wifi_set_max_tx_power (vizbot.ino:25) and re-applies it after begin() (wifi_provisioning.h:212).
3. The 1.69 needs BOARD_HAS_PSRAM because the devkitc-1 board definition is the no-PSRAM variant, so otherwise the Arduino core reports no PSRAM even though the 8MB OPI PSRAM is up (platformio.ini:31-34).
4. The 1.69's regulator already looks marginal under full WiFi TX, so the LED matrix is powered from 5 V instead of 3V3 (ring_leds.h:21-24).
5. 1.69 buzzer: a floating buzzer pin keeps drawing current and heats the board, so it is driven low first (buzzer.h:12-14, vizbot.ino:371).
6. 1.69 power latch: the PWR button only powers the board while held, so firmware must hold SYS_EN high (config.h:120-121, power_button.h:12-14). There is no charge-status or VBUS pin, so a wall charger looks like battery power (battery_adc.h:29-33).
7. 1.69 touch chip drops into standby 2 s after the last touch and then NACKs its address, so auto-sleep is disabled (touch_control.h:152-154).
8. Faces base: GPIO13 is both the LED data line and the CoreS3 I2S bit clock, so audio and LEDs are mutually exclusive, with the mic and speaker off at boot (config.h:229-230, faces_base.h:29-32, vizbot.ino:408-415, audio_spectrum.h:129-132).
9. Faces: partitions_kfun.csv must stay byte-identical to the copy in the tinyfun project (platformio.ini:102-104). The gamepad panel sends garbage right after power-on, so the chord poller waits for one clean read (faces_chooser.h:80-82).
10. FastLED trap: the 8x8 matrix array owns FastLED[0], so the Faces strips and the 1.69 matrix must call their own controller, never FastLED.show() (faces_base.h:45-50, ring_leds.h:26-28).
11. Stackchan servo power: servo power (VM_EN) must come up before servo_init(), or the head never moves while /state still reports perfect angles (stackchan_base.h:91-96).
12. Stackchan head stalls after hours; the bus still answers reads, a rail cycle recovers it, so the rail is cycled every 5 minutes as a workaround (stackchan_base.h:328-345). After a rail cycle the code waits until reads are sane, because commanding a move while the servo is booting can slam the head across its range (stackchan_base.h:295-302).
13. BSP default pitch zero of 620 sits this unit's head about 120 steps too high, so zero is seeded to 500 only when absent (stackchan_base.h:58-71).
14. Camera: esp32-camera leaves a GDMA channel wired to LCD_CAM after stop, which breaks the next start, so the code clears the channel selection after every stop (stackchan_camera.h:126-132).
15. Something during boot reclaims MIDI GPIO17/18, so the UART is re-initialized afterwards (midi_synth.h:106-107). If the synth is silent on Port A the Grove data lines may be reversed (config.h:371-374).

Questions: what are your hardware gotchas? why did your screen go black after flashing? why does the pendant's wifi fail at full power? why does your head get stuck? why do the Faces lights and sound fight? why did the camera stop working after a restart?
