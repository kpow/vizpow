#ifndef BATTERY_ADC_H
#define BATTERY_ADC_H

#ifdef BAT_ADC_PIN

#include <Arduino.h>
#include <driver/usb_serial_jtag.h>
#include "config.h"
#ifdef BAT_RUNDOWN_LOG
#include <LittleFS.h>
extern bool lowPowerMode;   // vizbot.ino
#endif

// ============================================================================
// Battery level (Waveshare 1.69)
// ============================================================================
// VBAT reaches BAT_ADC_PIN through a 200k/100k divider, so the pin sits at a
// third of the battery voltage (4.2 V -> 1.4 V). The pin is on ADC1, which
// keeps working with the radio up. analogReadMilliVolts() applies the eFuse
// calibration. It reads at the core's default 11 dB attenuation, and the curve
// below was measured that way: change one and the other has to be redone.
//
// The level is read off the voltage, so it is a rough gauge, not a fuel gauge.
// The curve below is this board's own run-down, so it already includes the sag
// from the backlight, LEDs and WiFi; the slow average hides the dips. A second
// run-down in low power mode (radio off, 500 min) gave the same curve to within
// 16 mV, so one table serves both modes.
//
// There is no charge-status or VBUS pin. `usb` is true while a USB host is
// sending frames (a computer); the pin then reads the cell under charge, so the
// UI shows a bolt instead of a level. A wall charger or power bank sends no
// frames and cannot be told apart from battery power — the level just reads
// high while it charges.
// ============================================================================

#define BAT_SAMPLE_COUNT   8
#define BAT_SAMPLE_MS      500
#define BAT_SMOOTH         0.03f   // weight of each new sample: ~17 s time constant
#define BAT_BAR_HYST       3       // % past a threshold before a bar comes back
#define BAT_LOG_MS         30000

// Run-down log (BAT_RUNDOWN_LOG): one CSV line per sample — uptime s, smoothed
// mV, raw mV, usb — with a "# boot" line at the start of each run.
#define BAT_RUNLOG_PATH    "/batlog.csv"
#define BAT_RUNLOG_MS      60000
#define BAT_RUNLOG_FAST_MS 10000   // below BAT_RUNLOG_FAST_V, where the voltage falls away quickly
#define BAT_RUNLOG_FAST_V  3.60f
#define BAT_RUNLOG_MAX     (256 * 1024)

// Battery voltage at 0, 10 .. 100 % of run time, measured on a 1.69 pendant
// (2026-10-12, WiFi on, 191 min from a full charge to dead). A full cell reads
// about 4.02 V under load. 0 % is 3.30 V, about 12 minutes before the board
// died at 2.6 V: below it the voltage falls away fast.
static const uint16_t BAT_CURVE_MV[11] = {3300, 3392, 3443, 3484, 3516, 3551, 3603, 3688, 3783, 3886, 4015};
// Lowest % that still shows 1, 2, 3 and 4 bars
static const uint8_t BAT_BAR_MIN[5] = {0, 10, 25, 50, 75};

struct BatteryAdc {
  float volts = 0;        // smoothed battery voltage
  uint8_t percent = 0;
  uint8_t bars = 0;       // 0..4
  bool usb = false;       // USB host attached
  unsigned long lastMs = 0;
  unsigned long logMs = 0;
#ifdef BAT_RUNDOWN_LOG
  unsigned long runLogMs = 0;
  bool runLogTried = false;     // first sample of this boot attempted
  bool runLogMarked = false;    // "# boot" line written
  int fakeMv = 0;               // /battery/log?fake=: stand-in battery reading, to exercise the cutoff
#endif

  float readVolts() {
#ifdef BAT_RUNDOWN_LOG
    if (fakeMv) return fakeMv / 1000.0f;
#endif
    uint32_t mv = 0;
    for (uint8_t i = 0; i < BAT_SAMPLE_COUNT; i++) mv += analogReadMilliVolts(BAT_ADC_PIN);
    return mv / (float)BAT_SAMPLE_COUNT * BAT_ADC_DIVIDER / 1000.0f;
  }

  static uint8_t percentFor(float v) {
    int mv = (int)(v * 1000.0f);
    if (mv <= BAT_CURVE_MV[0]) return 0;
    for (uint8_t i = 1; i <= 10; i++) {
      if (mv < BAT_CURVE_MV[i]) {
        int lo = BAT_CURVE_MV[i - 1], hi = BAT_CURVE_MV[i];
        return (i - 1) * 10 + (mv - lo) * 10 / (hi - lo);
      }
    }
    return 100;
  }

  // snap: take the level as read, skipping the hysteresis
  void refresh(bool snap) {
    percent = percentFor(volts);
    uint8_t b = 0;
    while (b < 4 && percent >= BAT_BAR_MIN[b + 1]) b++;
    if (snap || b < bars) bars = b;
    else if (b > bars) bars = percent >= BAT_BAR_MIN[b] + BAT_BAR_HYST ? b : max(bars, (uint8_t)(b - 1));
  }

#ifdef BAT_RUNDOWN_LOG
  // Opened and closed per line, so the last sample before a brownout is on flash
  void runLog(unsigned long now, float raw) {
    unsigned long every = (!usb && volts < BAT_RUNLOG_FAST_V) ? BAT_RUNLOG_FAST_MS : BAT_RUNLOG_MS;
    if (runLogTried && now - runLogMs < every) return;
    runLogTried = true;
    runLogMs = now;
    if (!LittleFS.begin(true)) return;
    File f = LittleFS.open(BAT_RUNLOG_PATH, "a");
    if (!f) return;
    if (f.size() < BAT_RUNLOG_MAX) {
      if (!runLogMarked) f.printf("# boot fw=%s lowpower=%d\n", FIRMWARE_VERSION, lowPowerMode ? 1 : 0);
      runLogMarked = true;
      f.printf("%lu,%d,%d,%d\n", now / 1000, (int)(volts * 1000.0f), (int)(raw * 1000.0f), usb ? 1 : 0);
    }
    f.close();
  }

  // A sample line right now, then "# <note>"
  void runLogNote(const char* note) {
    runLogTried = false;
    runLog(millis(), volts);
    File f = LittleFS.open(BAT_RUNLOG_PATH, "a");
    if (!f) return;
    f.printf("# %s\n", note);
    f.close();
  }
#endif

  void init() {
    volts = readVolts();
    refresh(true);
  }

  // Call every loop.
  void update() {
    unsigned long now = millis();
    if (now - lastMs < BAT_SAMPLE_MS) return;
    lastMs = now;

    bool nowUsb = usb_serial_jtag_is_connected();
#ifdef BAT_RUNDOWN_LOG
    if (fakeMv) nowUsb = false;
#endif
    float v = readVolts();
    if (nowUsb != usb) {
      // Charge current moves the reading at once; don't average across the change
      usb = nowUsb;
      volts = v;
      refresh(true);
    } else {
      volts += (v - volts) * BAT_SMOOTH;
      refresh(false);
    }

#ifdef BAT_RUNDOWN_LOG
    runLog(now, v);
#endif

    if (now - logMs >= BAT_LOG_MS) {
      logMs = now;
      DBG("BAT: ");
      DBG(volts, 3);
      DBG("V ");
      DBG(percent);
      DBG("% bars=");
      DBG(bars);
      DBGLN(usb ? " usb" : " battery");
    }
  }
};

BatteryAdc batteryAdc;

#endif // BAT_ADC_PIN
#endif // BATTERY_ADC_H
