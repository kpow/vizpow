# TARGET_AMOLED — Waveshare ESP32-S3-Touch-AMOLED-1.8 Support

## Hardware Overview

- **Board**: Waveshare ESP32-S3-Touch-AMOLED-1.8 (ESP32-S3R8, 16MB Flash, 8MB PSRAM)
- **Display**: 1.8" AMOLED, 368x448, SH8601 driver, QSPI interface
- **Touch**: FT3168 capacitive touch (I2C)
- **IMU**: QMI8658 6-axis (same as current LCD target)
- **Audio**: ES8311 codec + speaker + dual microphones
- **Power**: AXP2101 PMIC, 3.7V LiPo battery support
- **RTC**: PCF85063
- **Other**: SD card slot, programmable button (BOOT/GPIO0)
- **Product page**: https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8

## Pin Configuration

Source: [Waveshare GitHub pin_config.h](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8)

### QSPI Display (SH8601)

| Function | GPIO |
|----------|------|
| SDIO0    | 4    |
| SDIO1    | 5    |
| SDIO2    | 6    |
| SDIO3    | 7    |
| SCLK     | 11   |
| CS       | 12   |

### I2C Bus (Touch / IMU / RTC / PMIC)

| Function | GPIO |
|----------|------|
| SDA      | 15   |
| SCL      | 14   |
| Touch INT| 21   |

### I2S Audio (ES8311 Codec)

| Function | GPIO |
|----------|------|
| MCK      | 16   |
| BCK      | 9    |
| WS       | 45   |
| DO (speaker out) | 8 |
| DI (mic in)      | 10 |
| PA (amp enable)  | 46 |

### SD Card (SDMMC)

| Function | GPIO |
|----------|------|
| CLK      | 2    |
| CMD      | 1    |
| DATA     | 3    |

### Other

| Function | GPIO |
|----------|------|
| BOOT button | 0 |
| USB D+   | 20   |
| USB D-   | 19   |

## Implementation Plan

### Phase 1: Display + IMU (Core Functionality)

**config.h** — Add `TARGET_AMOLED` block:
- `#define BOARD_ESP32S3_AMOLED`
- `#define DISPLAY_LCD_ONLY` (reuses existing LCD rendering path)
- `#define HIRES_ENABLED` (8MB PSRAM)
- `#define TOUCH_ENABLED`
- I2C pins: SDA=15, SCL=14
- LCD_WIDTH=368, LCD_HEIGHT=448
- Touch: `TOUCH_I2C_ADDR 0x38` (FT3168)
- No separate LCD_SCK/MOSI/DC/RST/BL — QSPI + AMOLED self-emitting (no backlight pin)

**display_lcd.h** — Add `#elif defined(TARGET_AMOLED)` LGFX class:
```cpp
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_SH8601Z _panel_instance;
  lgfx::Bus_SPI       _bus_instance;

public:
  LGFX(void) {
    { // QSPI bus configuration
      auto cfg = _bus_instance.config();
      cfg.spi_host    = SPI2_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;
      cfg.freq_read   = 16000000;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = 11;
      cfg.pin_io0     = 4;   // QSPI data 0
      cfg.pin_io1     = 5;   // QSPI data 1
      cfg.pin_io2     = 6;   // QSPI data 2
      cfg.pin_io3     = 7;   // QSPI data 3
      cfg.pin_dc      = -1;  // No DC pin (QSPI encodes cmd/data)
      cfg.pin_mosi    = -1;
      cfg.pin_miso    = -1;
      cfg.spi_3wire   = true;
      cfg.use_lock    = true;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    { // Panel configuration
      auto cfg = _panel_instance.config();
      cfg.pin_cs       = 12;
      cfg.pin_rst      = -1;  // No hardware reset pin (PMIC controls)
      cfg.pin_busy     = -1;
      cfg.panel_width  = 368;
      cfg.panel_height = 448;
      cfg.offset_x     = 0;
      cfg.offset_y     = 0;
      cfg.invert       = false;  // AMOLED — verify on hardware
      cfg.rgb_order    = false;  // Verify on hardware
      cfg.bus_shared   = false;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};
```

**DisplayProxy** — Reuses existing pattern. `beginCanvas()`/`flushCanvas()` double-buffering works unchanged. The 368x448x2 = 322KB framebuffer fits easily in 8MB PSRAM.

**IMU** — Works immediately. Same QMI8658 chip, same SensorLib. Only the I2C pins differ (14/15 vs 10/11), handled by config.h `I2C_SDA`/`I2C_SCL`.

**Face/Bot layout** — 368x448 is taller and wider than 240x280. May need `BOT_FACE_CX`/`BOT_FACE_CY` overrides in config.h for centered face placement. Speech bubbles and overlays may need position tuning.

### Phase 2: Touch Controller (FT3168)

The existing `touch_control.h` is CST816T-specific (I2C addr 0x15, specific register map for gestures/coordinates). FT3168 uses:
- I2C address: **0x38**
- Different register map (FocalTech FT series)
- Touch INT pin: GPIO21 (same pin as CST816T reset — collision, needs care)

Options:
1. Abstract touch behind an interface with CST816T and FT3168 backends
2. Use SensorLib's touch support (it may already have FT3168/FT6336 driver — check `TouchDrvFT6X36.hpp`)

### Phase 3: Audio (ES8311 + Speaker)

Current sound system is `#ifdef TARGET_CORES3` using M5.Speaker (M5Unified). The AMOLED board needs:
- ES8311 codec initialization over I2C
- I2S configuration (MCK=16, BCK=9, WS=45, DO=8, DI=10)
- PA enable on GPIO46
- New `#ifdef TARGET_AMOLED` sound path, or refactor to a generic I2S interface

Consider using Espressif's `esp_codec_dev` component or the `es8311` Arduino library.

### Phase 4: Power Management (AXP2101)

- Battery voltage/percentage monitoring via I2C
- Could expose battery level in web UI and bot info mode
- XPowersLib already in Waveshare examples
- Sleep/wake via AXP2101 power control

### Phase 5: Additional Peripherals

- **RTC (PCF85063)**: Real-time clock for timestamping, scheduled behaviors
- **SD Card**: Logging, custom sprite storage, configuration files
- **Microphone**: Audio-reactive effects, voice detection

## Key Differences from TARGET_LCD

| Aspect | TARGET_LCD (ST7789V2) | TARGET_AMOLED (SH8601Z) |
|--------|----------------------|-------------------------|
| Display type | IPS LCD | AMOLED |
| Interface | SPI (1 data line) | QSPI (4 data lines) |
| Resolution | 240x280 | 368x448 |
| Backlight | PWM on GPIO15 | Self-emitting (brightness via SH8601 register) |
| Touch IC | CST816T (0x15) | FT3168 (0x38) |
| I2C SDA/SCL | GPIO11/10 | GPIO15/14 |
| Audio | None | ES8311 codec + speaker |
| Battery | None | AXP2101 PMIC + LiPo |
| PSRAM | 8MB OPI | 8MB OPI |

## LovyanGFX Compatibility

Confirmed: LovyanGFX v1.2.19 (currently installed) includes:
- `Panel_SH8601Z` — full QSPI AMOLED panel driver (`Panel_SH8601Z.hpp/.cpp`)
- `Bus_SPI` with `pin_io0`-`pin_io3` for QSPI mode
- No custom driver needed. No additional library dependencies.

## References

- [Waveshare Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8)
- [Waveshare GitHub (example code + pin_config.h)](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8)
- [LovyanGFX QSPI Discussion #663](https://github.com/lovyan03/LovyanGFX/discussions/663)
- [LovyanGFX Panel_SH8601Z source](https://github.com/lovyan03/LovyanGFX/blob/develop/src/lgfx/v1/panel/Panel_SH8601Z.hpp)
- [Schematic PDF](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8/ESP32-S3-Touch-AMOLED-1.8.pdf)
