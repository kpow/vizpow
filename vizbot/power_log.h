#ifndef POWER_LOG_H
#define POWER_LOG_H

#ifdef BOARD_HAS_STACKCHAN_BASE

// ============================================================================
// Power log: a black box for a Stackchan that switches itself off
// ============================================================================
// Appends to /powerlog.csv on LittleFS so the record survives a power cut:
//   BOOT        once per boot: why the unit last switched on and off (AXP2101
//               registers 0x20 / 0x21, read before anything else touches the
//               power chip) and the ESP32's own reset reason
//   beat        every 15 s: battery, USB and system voltage now, plus the
//               lowest voltages and highest current seen since the last beat
//               (sampled once a second)
//   RAIL_CYCLE  just before the servo watchdog cuts the servo rail, then
//   RAIL_OK / RAIL_FAIL after it
// The last row before a BOOT row is the unit's state within 15 s of dying.
// Read it at GET /debug/powerlog (web_server.h).
// ============================================================================

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_system.h>
#include "stackchan_base.h"

#define PLOG_PATH       "/powerlog.csv"
#define PLOG_OLD_PATH   "/powerlog.old.csv"
#define PLOG_MAX_BYTES  (384 * 1024)   // then it rotates to PLOG_OLD_PATH
#define PLOG_BEAT_MS    15000
#define PLOG_SAMPLE_MS  1000

static const char* PLOG_HEADER =
  "boot,up_s,clock,event,batt_v,batt_a,vbus_mv,vsys_mv,axp00,axp01,batt_pct,"
  "min_batt_v,max_batt_a,min_vsys_mv,rail_cycles,heap,note";

static uint8_t  plogOnSrc = 0, plogOffSrc = 0;
static int      plogReset = 0;
static uint32_t plogBoot = 0;
static bool     plogBootLogged = false;
static uint32_t plogNextBeatMs = 0, plogNextSampleMs = 0;
// Extremes since the last beat
static float    plogMinBattV = 99, plogMaxBattA = -99;
static int      plogMinVsysMv = 99999;

// Call right after M5.begin(), before any other task or driver exists.
inline void plogCaptureBoot() {
  plogOnSrc  = M5.Power.Axp2101.readRegister8(0x20);
  plogOffSrc = M5.Power.Axp2101.readRegister8(0x21);
  plogReset  = (int)esp_reset_reason();
}

inline const char* plogOffReason(uint8_t off) {
  return off & 0x08 ? "pmu: system voltage too low"
       : off & 0x80 ? "pmu: over temperature"
       : off & 0x60 ? "pmu: regulator fault"
       : off & 0x10 ? "pmu: usb over voltage"
       : off & 0x05 ? "power button"
       : off & 0x02 ? "software"
       : "not recorded";
}

inline int plogVsysMv() {   // caller holds the I2C lock
  return ((M5.Power.Axp2101.readRegister8(0x3A) & 0x3F) << 8) | M5.Power.Axp2101.readRegister8(0x3B);
}

void plogRow(const char* event, const char* note) {
  char clock[24] = "";
  struct tm t;
  if (getLocalTime(&t, 0) && t.tm_year > 100) strftime(clock, sizeof(clock), "%Y-%m-%d %H:%M:%S", &t);

  char power[96] = ",,,,,,";   // left empty if the I2C bus is busy
  {
    ScI2cLock lock(150);
    if (lock.held) {
      if (!clock[0]) {
        m5::rtc_datetime_t dt;
        if (M5.Rtc.isEnabled() && M5.Rtc.getDateTime(&dt) && dt.date.year >= 2026)
          snprintf(clock, sizeof(clock), "%04d-%02d-%02d %02d:%02d:%02d", dt.date.year, dt.date.month,
                   dt.date.date, dt.time.hours, dt.time.minutes, dt.time.seconds);
      }
      snprintf(power, sizeof(power), "%.3f,%.3f,%d,%d,%02X,%02X,%d",
               scChan.getBatteryVoltage(), scChan.getBatteryCurrent(),
               (int)(M5.Power.Axp2101.getVBUSVoltage() * 1000), plogVsysMv(),
               M5.Power.Axp2101.readRegister8(0x00), M5.Power.Axp2101.readRegister8(0x01),
               (int)M5.Power.getBatteryLevel());
    }
  }

  char extremes[48] = ",,";
  if (plogMinBattV < 90)
    snprintf(extremes, sizeof(extremes), "%.3f,%.3f,%d", plogMinBattV, plogMaxBattA, plogMinVsysMv);

  char line[360];
  snprintf(line, sizeof(line), "%lu,%lu,%s,%s,%s,%s,%u,%u,%s",
           (unsigned long)plogBoot, (unsigned long)(millis() / 1000), clock, event, power, extremes,
           (unsigned)scServoReboots, (unsigned)ESP.getFreeHeap(), note);

  File f = LittleFS.open(PLOG_PATH, "a");
  if (!f) return;   // filesystem not mounted yet
  if (f.size() > PLOG_MAX_BYTES) {
    f.close();
    LittleFS.remove(PLOG_OLD_PATH);
    LittleFS.rename(PLOG_PATH, PLOG_OLD_PATH);
    f = LittleFS.open(PLOG_PATH, "a");
    if (!f) return;
  }
  if (f.size() == 0) f.println(PLOG_HEADER);
  f.println(line);
  f.close();   // close every time: the next row may never come
}

// Call once per loop.
inline void plogTick() {
  uint32_t now = millis();

  if (!plogBootLogged) {
    if (!LittleFS.exists("/cloud")) return;   // content cache mounts the filesystem during boot
    plogBootLogged = true;
    Preferences p;
    p.begin("powerlog", false);
    plogBoot = p.getUInt("boot", 0) + 1;
    p.putUInt("boot", plogBoot);
    p.end();
    char note[112];
    snprintf(note, sizeof(note), "fw %s on_src %02X off_src %02X reset %d last shutdown: %s",
             FIRMWARE_VERSION, plogOnSrc, plogOffSrc, plogReset, plogOffReason(plogOffSrc));
    plogRow("BOOT", note);
    plogNextBeatMs = now + PLOG_BEAT_MS;
  }

  if ((int32_t)(now - plogNextSampleMs) >= 0) {
    plogNextSampleMs = now + PLOG_SAMPLE_MS;
    ScI2cLock lock(20);
    if (lock.held) {
      float v = scChan.getBatteryVoltage(), a = scChan.getBatteryCurrent();
      int vsys = plogVsysMv();
      if (v > 0.5f && v < plogMinBattV) plogMinBattV = v;
      if (a > plogMaxBattA) plogMaxBattA = a;
      if (vsys > 500 && vsys < plogMinVsysMv) plogMinVsysMv = vsys;
    }
  }

  if ((int32_t)(now - plogNextBeatMs) >= 0) {
    plogNextBeatMs = now + PLOG_BEAT_MS;
    plogRow("beat", "");
    plogMinBattV = 99; plogMaxBattA = -99; plogMinVsysMv = 99999;
  }
}

#endif // BOARD_HAS_STACKCHAN_BASE
#endif // POWER_LOG_H
