#ifndef DEVICE_NAME_H
#define DEVICE_NAME_H

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

// ============================================================================
// Device name — one name per vizMini (mirrors vizbot/device_id.h)
// ============================================================================
// Several vizMinis on one network need different hotspot names and addresses.
// With no saved name it is the base plus four hex digits of the MAC:
//   name "vizMini-8DCC"  ->  hotspot "vizMini-8DCC", http://vizmini-8dcc.local
// A name set on the web page (/device/name) or from VizCloud (sync
// config.deviceName) is saved in NVS ("devName") and used the same way:
//   name "Desk Yeti"     ->  hotspot "Desk Yeti",    http://desk-yeti.local
// The name is also what he reports to VizCloud as `name`.
//
// Rules: 1 to 23 characters of printable ASCII, no double quote or backslash,
// no space at either end. An empty name goes back to the MAC default.
// Unlike vizbot (which applies a new name at the next restart) the name and
// the .local address change at once; the hotspot keeps its old name until it
// next starts, so a phone on it is not thrown off.
//
// deviceNameLoad() runs first in setup(). Only the loop task changes the name;
// the cloud task reads it through deviceNameCopy().
// ============================================================================

#define DEVICE_NAME_MAX 24   // 23 characters + NUL, the same limit as vizbot

static char g_devName[DEVICE_NAME_MAX];   // as typed: hotspot SSID, cloud name
static char g_devHost[DEVICE_NAME_MAX];   // mDNS label: lowercase letters, digits, hyphens
static portMUX_TYPE g_devNameMux = portMUX_INITIALIZER_UNLOCKED;

inline bool deviceNameValid(const char *n) {
  size_t len = strlen(n);
  if (len < 1 || len >= DEVICE_NAME_MAX) return false;
  if (n[0] == ' ' || n[len - 1] == ' ') return false;
  for (size_t i = 0; i < len; i++) {
    char c = n[i];
    if (c < 0x20 || c > 0x7E || c == '"' || c == '\\') return false;
  }
  return true;
}

inline void deviceNameDefault(char *out, size_t cap) {
  snprintf(out, cap, "%s-%04X", DEVICE_NAME_BASE, (unsigned)(ESP.getEfuseMac() & 0xFFFF));
}

// name -> mDNS label: lowercase, anything but a letter or digit becomes one hyphen
inline void deviceHostFrom(const char *name, char *out, size_t cap) {
  size_t n = 0;
  for (const char *p = name; *p && n + 1 < cap; p++) {
    char c = (char)tolower((unsigned char)*p);
    bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (keep) out[n++] = c;
    else if (n && out[n - 1] != '-') out[n++] = '-';
  }
  while (n && out[n - 1] == '-') n--;
  out[n] = 0;
  if (!n) {                                   // a name of only symbols
    char d[DEVICE_NAME_MAX];
    deviceNameDefault(d, sizeof(d));
    for (n = 0; d[n] && n + 1 < cap; n++) out[n] = (char)tolower((unsigned char)d[n]);
    out[n] = 0;
  }
}

static void deviceNameApply(const char *name) {
  char nm[DEVICE_NAME_MAX], host[DEVICE_NAME_MAX];
  if (name[0] && deviceNameValid(name)) { strncpy(nm, name, sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0; }
  else deviceNameDefault(nm, sizeof(nm));
  deviceHostFrom(nm, host, sizeof(host));
  portENTER_CRITICAL(&g_devNameMux);
  memcpy(g_devName, nm, sizeof(g_devName));
  memcpy(g_devHost, host, sizeof(g_devHost));
  portEXIT_CRITICAL(&g_devNameMux);
}

// Call first in setup(), before the WiFi starts
inline void deviceNameLoad() {
  Preferences p;
  p.begin(NVS_NAMESPACE, true);
  String n = p.getString("devName", "");
  p.end();
  deviceNameApply(n.c_str());
  Serial.printf("[name] %s  ->  %s.local\n", g_devName, g_devHost);
}

// Save and use a new name; "" clears it (back to the MAC default). False if the
// name breaks the rules, and nothing changes. The caller restarts mDNS.
inline bool deviceNameSet(const char *name) {
  if (name[0] && !deviceNameValid(name)) return false;
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  if (name[0]) p.putString("devName", name);
  else p.remove("devName");
  p.end();
  deviceNameApply(name);
  Serial.printf("[name] now %s  ->  %s.local\n", g_devName, g_devHost);
  return true;
}

// A consistent copy for another task
inline void deviceNameCopy(char *out, size_t cap) {
  portENTER_CRITICAL(&g_devNameMux);
  strncpy(out, g_devName, cap - 1);
  portEXIT_CRITICAL(&g_devNameMux);
  out[cap - 1] = 0;
}

#endif // DEVICE_NAME_H
