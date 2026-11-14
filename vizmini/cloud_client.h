#ifndef CLOUD_CLIENT_H
#define CLOUD_CLIENT_H

#include <Arduino.h>
#include "config.h"
#include "device_name.h"

// ============================================================================
// VizCloud client — bot protocol v2 (vizCloud docs/rebuild/BOT-API-V2.md)
// ============================================================================
// vizMini registers once, then syncs every poll interval: he reports his name,
// firmware and state, acks the commands he got last time, and gets back
// commands, settings (config) and, when one is assigned, a firmware update.
//
// How it fits this board (ESP32-C3: one core, no PSRAM, ~100 KB of free heap):
//  - Its own FreeRTOS task (cloudTask). Nothing here runs in the render loop
//    except cloudLoop(), which only hands finished work to the same functions
//    the web page calls. A dead network or server costs the loop nothing.
//  - One network job at a time (weather.h's lock): never beside the weather
//    fetch or a firmware upload from the web page. A TLS session takes two
//    16.7 KB record buffers (this core's mbedTLS has no small or dynamic
//    buffers) plus ~12 KB during the handshake, so a call is skipped while
//    the heap is short (CLOUD_TLS_MIN_FREE / _BLOCK) and tried again later.
//  - Fixed buffers, no heap of its own: the request is built with snprintf, the
//    reply is capped at CLOUD_RESPONSE_MAX and read by the small JSON reader
//    below. Not ArduinoJson: the reply is tiny and has one known shape, the
//    reader allocates nothing while TLS holds the heap, it costs ~1.5 KB of
//    flash instead of ~10 KB plus a new dependency, and it still finds botId
//    and the commands in a reply that was cut off at the cap (a v1 server
//    sends its whole content library on register).
//  - HTTPS with the pinned Google Trust Services root, as vizbot/cloud_client.h
//    does over esp_http_client (the PEM is copied here, not included across).
//
// Identity: botId, botToken and the applied config revision live in NVS. With
// a token he sends X-Bot-Token; without one, the public v1 value as
// X-Bot-Secret (see CLOUD_LEGACY_SECRET). A 401 means wait an hour, then
// register again; nothing ever stops the client for good.
// A test server set on the web page keeps its own identity under separate NVS
// keys, so testing never disturbs the production pairing.
//
// Threads: the cloud task owns everything in g_cl except what is marked
// volatile. Commands and settings cross to the loop task through g_cq.
// ============================================================================

#if WIFI_ENABLED && CLOUD_ENABLED

#include <WiFi.h>
#include <Preferences.h>
#include <Update.h>
#include <time.h>
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_heap_caps.h"
#include "mbedtls/sha256.h"
#include "strip_leds.h"
#include "weather.h"        // the network lock

// ---- hooks into the rest of the firmware (the web page uses the same ones) --
extern void    ctlSetExpression(uint8_t v);
extern void    ctlSay(const char *text);
extern void    ctlSetLeds(int mode, int bri);
extern uint8_t g_expr;
inline bool    webIsAP();
inline void    webApplyName();

// GTS Root R4 — the Google Trust Services root behind DigitalOcean App
// Platform. Chain: server cert -> WE1 -> GTS Root R4. Valid until 2036-06-22.
// Same pin as vizbot/cloud_client.h.
static const char CLOUD_ROOT_CA_PEM[] = R"PEM(-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)PEM";

// ============================================================================
// A small JSON reader
// ============================================================================
// Reads a NUL-terminated reply in place. No allocation, no recursion. Every
// function returns nullptr / false when the text ends early, so a reply cut
// off at the cap gives up cleanly at the cut instead of misreading it.

static const char *jsWs(const char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return p;
}

// p on an opening quote -> just past the closing one
static const char *jsSkipString(const char *p) {
  p++;
  while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
  return *p == '"' ? p + 1 : nullptr;
}

// p on any value -> just past it (nullptr if it is cut off)
static const char *jsSkip(const char *p) {
  if (!p) return nullptr;
  p = jsWs(p);
  if (*p == '"') return jsSkipString(p);
  if (*p == '{' || *p == '[') {
    int depth = 0;
    while (*p) {
      if (*p == '"') { p = jsSkipString(p); if (!p) return nullptr; continue; }
      if (*p == '{' || *p == '[') depth++;
      else if ((*p == '}' || *p == ']') && --depth == 0) return p + 1;
      p++;
    }
    return nullptr;
  }
  const char *s = p;                             // number, true, false, null
  while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\r' && *p != '\n' && *p != '\t') p++;
  return (*p && p > s) ? p : nullptr;
}

// The value of `key` in the object at obj (its own members only, not nested ones)
static const char *jsGet(const char *obj, const char *key) {
  if (!obj) return nullptr;
  const char *p = jsWs(obj);
  if (*p != '{') return nullptr;
  p = jsWs(p + 1);
  size_t klen = strlen(key);
  while (*p == '"') {
    const char *ks = p + 1;
    const char *ke = jsSkipString(p);
    if (!ke) return nullptr;
    p = jsWs(ke);
    if (*p != ':') return nullptr;
    p = jsWs(p + 1);
    if ((size_t)(ke - 1 - ks) == klen && memcmp(ks, key, klen) == 0) return p;
    p = jsSkip(p);
    if (!p) return nullptr;
    p = jsWs(p);
    if (*p != ',') return nullptr;
    p = jsWs(p + 1);
  }
  return nullptr;
}

static bool jsIsObject(const char *v) { return v && *v == '{'; }

// First element of the array at arr, then the one after el; nullptr at the end
static const char *jsFirst(const char *arr) {
  if (!arr || *arr != '[') return nullptr;
  const char *p = jsWs(arr + 1);
  return (*p && *p != ']') ? p : nullptr;
}
static const char *jsNext(const char *el) {
  const char *p = jsSkip(el);
  if (!p) return nullptr;
  p = jsWs(p);
  return *p == ',' ? jsWs(p + 1) : nullptr;
}

// A number as a long (12.7 reads as 12). False if v is not a number
static bool jsInt(const char *v, long &out) {
  if (!v || !(*v == '-' || (*v >= '0' && *v <= '9'))) return false;
  out = strtol(v, nullptr, 10);
  return true;
}

// A string into out: at most cap-1 characters, unescaped, one byte each.
// UTF-8 and \u escapes up to U+00FF become Latin-1 (what the OLED font has),
// anything beyond becomes '?', control characters become a space. So cap
// counts characters, not UTF-8 bytes. False if v is not a string
static bool jsStr(const char *v, char *out, size_t cap) {
  if (!v || *v != '"') return false;
  const unsigned char *p = (const unsigned char *)v + 1;
  size_t n = 0;
  while (*p && *p != '"') {
    uint32_t c = *p++;
    if (c == '\\') {
      c = *p;
      if (!c) break;
      p++;
      if (c == 'u') {
        c = 0;
        for (int i = 0; i < 4 && isxdigit(*p); i++, p++)
          c = c * 16 + (*p <= '9' ? *p - '0' : (*p | 0x20) - 'a' + 10);
        if (c >= 0xDC00 && c <= 0xDFFF) continue;        // second half of a pair: one '?' is enough
      } else if (c == 'n' || c == 'r' || c == 't' || c == 'b' || c == 'f') c = ' ';
    } else if (c >= 0xC0) {                                // UTF-8 lead byte
      int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
      c &= 0x3F >> more;
      for (; more && (*p & 0xC0) == 0x80; more--) c = (c << 6) | (*p++ & 0x3F);
    }
    if (c < 0x20) c = ' ';
    else if (c > 0xFF || (c >= 0x7F && c < 0xA0)) c = '?';
    if (n + 1 < cap) out[n++] = (char)c;
  }
  out[n] = 0;
  return true;
}

// A string, or a number as its digits (firmware.buildId may be either)
static bool jsStrOrNum(const char *v, char *out, size_t cap) {
  if (jsStr(v, out, cap)) return true;
  if (!v || !(*v == '-' || (*v >= '0' && *v <= '9'))) return false;
  size_t n = 0;
  while ((*v == '-' || *v == '.' || (*v >= '0' && *v <= '9')) && n + 1 < cap) out[n++] = *v++;
  out[n] = 0;
  return n > 0;
}

// "2026-11-13T20:15:00.000Z" (UTC) -> seconds since 1970, 0 if it is not a time
static uint32_t cloudIsoToEpoch(const char *s) {
  auto d2 = [](const char *p) { return (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])) ? (p[0] - '0') * 10 + (p[1] - '0') : -1; };
  if (strlen(s) < 19 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':') return 0;
  int c = d2(s), yy = d2(s + 2), M = d2(s + 5), D = d2(s + 8), h = d2(s + 11), m = d2(s + 14), sec = d2(s + 17);
  if (c < 0 || yy < 0 || M < 1 || M > 12 || D < 1 || D > 31 || h < 0 || h > 23 || m < 0 || m > 59 || sec < 0 || sec > 60) return 0;
  int Y = c * 100 + yy;
  if (Y < 2020 || Y > 2099) return 0;
  int y = Y - (M <= 2);                                    // days from the civil date
  int era = y / 400, yoe = y - era * 400;
  int doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  uint32_t days = (uint32_t)(era * 146097L + doe - 719468L);
  return days * 86400UL + (uint32_t)h * 3600UL + (uint32_t)m * 60UL + (uint32_t)sec;
}

// ============================================================================
// State
// ============================================================================
enum CloudApproval : uint8_t { CA_UNKNOWN = 0, CA_APPROVED, CA_PENDING, CA_DISABLED };

struct CloudState {
  // ---- saved in NVS ----
  char     custom[96] = "";          // test server URL from the web page, "" = production
  char     botId[40] = "";
  char     token[64] = "";
  int32_t  cfgRev = 0;               // last config revision applied
  int32_t  contentVer = 0;           // echoed so a v1 server sends no content
  uint16_t pollS = CLOUD_POLL_DEFAULT_S;
  // ---- running: written by the cloud task, read by the web page ----
  volatile uint8_t  approval = CA_UNKNOWN;
  volatile bool     online = false;        // on WiFi as a station
  volatile bool     reauth = false;        // token refused: register again when the back-off ends
  volatile uint8_t  fails = 0;             // failed calls in a row
  volatile uint8_t  slow = 0;              // 429s in a row: the poll interval doubles this many times
  volatile int      lastCode = 0;          // HTTP status of the last call, < 0 = no connection
  const char *volatile err = "";           // what went wrong last, in a few words
  volatile uint32_t lastOkMs = 0;          // millis() of the last good sync, 0 = none yet
  volatile uint32_t nextAt = 0;            // millis() of the next call
  volatile uint16_t stackFree = 0;         // least stack the task has ever had left, bytes
  volatile uint8_t  otaPct = 255;          // firmware download progress, 255 = none running
  // ---- written by the loop task ----
  volatile uint8_t  kick = 0;              // 1 = the server setting changed: reload and start over
  // ---- cloud task only ----
  bool     regTried = false;               // registered against this server since boot
  uint32_t regAt = 0;
  bool     resetSent = false;              // resetReason goes on syncs until one that is not pending succeeds
  char     otaErr[65] = "";                // reported as otaError (max 64 characters) until a sync takes it
  uint32_t otaAt = 0;                      // millis() of the last update attempt, 0 = none
  bool     otaSameWarned = false;
};
static CloudState g_cl;

// Set at boot when this image came from a cloud update and has yet to prove
// itself: the first good sync keeps it, CLOUD_OTA_CONFIRM_MS without one
// rolls back to the firmware before it
static volatile bool g_cloudOtaUnconfirmed = false;
static volatile uint32_t g_cloudRebootAt = 0;    // loop task restarts at this millis()
// A reboot command waits for one more sync, so its ack is out before he
// restarts (a server that resends unacked commands would restart him forever)
static volatile bool g_cloudRebootAfterSync = false;

static char g_cReq[1024];                        // request body
static char g_cResp[CLOUD_RESPONSE_MAX];         // reply body; also the firmware download buffer
static char g_cUrl[208];

// ---- command acks: ids from the last sync, sent with the next ---------------
#define CLOUD_ACK_SLOTS 8
static char    g_cAcks[CLOUD_ACK_SLOTS][40];
static uint8_t g_cAckN = 0;

// ---- work for the loop task --------------------------------------------------
enum CloudJob : uint8_t {
  CJ_NONE = 0, CJ_EXPRESSION, CJ_SAY, CJ_BRIGHTNESS, CJ_REBOOT,
  CJ_LED_MODE, CJ_LED_PATTERN, CJ_LED_PALETTE, CJ_NAME
};
struct CloudJobSlot {
  uint8_t  job;          // CloudJob, CJ_NONE = free
  int16_t  value;
  uint32_t due;          // run at this UTC second (execute_at), 0 = now
  char     text[49];     // say text, LED pattern / palette name, device name
};
#define CLOUD_JOB_SLOTS 10                       // 8 commands a sync + a name and a brightness from config
static CloudJobSlot g_cq[CLOUD_JOB_SLOTS];
static volatile uint8_t g_cqUsed = 0;
static portMUX_TYPE g_cqMux = portMUX_INITIALIZER_UNLOCKED;

// Ids and tokens go into URLs, headers and hand-built JSON: letters, digits, - and _ only
static bool cloudPlainId(const char *s) {
  if (!s[0]) return false;
  for (; *s; s++) if (!isalnum((unsigned char)*s) && *s != '-' && *s != '_') return false;
  return true;
}

static bool cloudQueue(uint8_t job, int value, const char *text, uint32_t due) {
  bool ok = false;
  portENTER_CRITICAL(&g_cqMux);
  for (uint8_t i = 0; i < CLOUD_JOB_SLOTS; i++) {
    if (g_cq[i].job != CJ_NONE) continue;
    g_cq[i].job = job;
    g_cq[i].value = (int16_t)value;
    g_cq[i].due = due;
    strncpy(g_cq[i].text, text ? text : "", sizeof(g_cq[i].text) - 1);
    g_cq[i].text[sizeof(g_cq[i].text) - 1] = 0;
    g_cqUsed = g_cqUsed + 1;
    ok = true;
    break;
  }
  portEXIT_CRITICAL(&g_cqMux);
  return ok;
}

// ============================================================================
// NVS
// ============================================================================
// Production identity: cId cTok cRev cCv cPoll. Test server: tId tTok tRev tCv
// tPoll, wiped whenever the test server setting is saved.
static const char *cloudKey(const char *what) {
  static char k[8];
  snprintf(k, sizeof(k), "%c%s", g_cl.custom[0] ? 't' : 'c', what);
  return k;
}

static void cloudLoadIdentity() {
  Preferences p;
  p.begin(NVS_NAMESPACE, true);
  String id = p.getString(cloudKey("Id"), ""), tok = p.getString(cloudKey("Tok"), "");
  g_cl.cfgRev = p.getInt(cloudKey("Rev"), 0);
  g_cl.contentVer = p.getInt(cloudKey("Cv"), 0);
  g_cl.pollS = p.getUShort(cloudKey("Poll"), CLOUD_POLL_DEFAULT_S);
  p.end();
  g_cl.botId[0] = g_cl.token[0] = 0;
  if (id.length() < sizeof(g_cl.botId) && cloudPlainId(id.c_str())) strcpy(g_cl.botId, id.c_str());
  if (tok.length() < sizeof(g_cl.token) && cloudPlainId(tok.c_str())) strcpy(g_cl.token, tok.c_str());
  g_cl.pollS = constrain(g_cl.pollS, 5, CLOUD_POLL_MAX_S);
}

static void cloudSaveIdentity() {
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putString(cloudKey("Id"), g_cl.botId);
  p.putString(cloudKey("Tok"), g_cl.token);
  p.putInt(cloudKey("Rev"), g_cl.cfgRev);
  p.putInt(cloudKey("Cv"), g_cl.contentVer);
  p.putUShort(cloudKey("Poll"), g_cl.pollS);
  p.end();
}

static void cloudLoadServer() {
  Preferences p;
  p.begin(NVS_NAMESPACE, true);
  String u = p.getString("cloudUrl", "");
  p.end();
  g_cl.custom[0] = 0;
  if (u.length() < sizeof(g_cl.custom)) strcpy(g_cl.custom, u.c_str());
}

static const char *cloudBase() { return g_cl.custom[0] ? g_cl.custom : CLOUD_SERVER_URL; }
static bool cloudIsHttps() { return strncmp(cloudBase(), "https://", 8) == 0; }

// ---- the "Cloud server" setting (BOT-API-V2 "Local testing") ------------------
// "" = the built-in production URL. Otherwise scheme://host[:port] with nothing
// after it. https:// may name any host (it must chain to the pinned root).
// Plain http:// only reaches a private address: 10.x, 172.16-31.x, 192.168.x
// or a .local name. Returns nullptr and fills out, or says what is wrong.
inline const char *cloudCheckUrl(String in, char *out, size_t cap) {
  in.trim();
  while (in.endsWith("/")) in.remove(in.length() - 1);
  out[0] = 0;
  if (in.length() == 0) return nullptr;
  bool https = in.startsWith("https://"), http = in.startsWith("http://");
  if (!https && !http) return "Start it with http:// or https://";
  if (in.length() >= cap) return "That address is too long";
  String host = in.substring(https ? 8 : 7);
  int colon = host.indexOf(':');
  String port = colon >= 0 ? host.substring(colon + 1) : String("1");
  if (colon >= 0) host = host.substring(0, colon);
  if (host.length() == 0 || port.length() == 0 || port.length() > 5) return "That address is missing a host or port";
  for (size_t i = 0; i < host.length(); i++)
    if (!isalnum((unsigned char)host[i]) && host[i] != '.' && host[i] != '-') return "Only the server address: no path, no spaces";
  for (size_t i = 0; i < port.length(); i++)
    if (!isdigit((unsigned char)port[i])) return "Only the server address: no path, no spaces";
  if (http) {
    String h = host;
    h.toLowerCase();
    IPAddress ip;
    bool priv = h.endsWith(".local") && h.length() > 6;
    if (!priv && ip.fromString(h))
      priv = ip[0] == 10 || (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) || (ip[0] == 192 && ip[1] == 168);
    if (!priv) return "Plain http:// only works for a server on your own network (10.x, 172.16-31.x, 192.168.x or a .local name)";
  }
  strcpy(out, in.c_str());
  return nullptr;
}

// From the web page (loop task). Saves the setting; the cloud task picks it up
// within a second, forgets any test-server identity and registers afresh.
// Saving the same test server again is how to re-register after its database
// was reset. Returns nullptr, or what is wrong with the address.
inline const char *cloudSetServer(const String &url) {
  char norm[sizeof(g_cl.custom)];
  const char *why = cloudCheckUrl(url, norm, sizeof(norm));
  if (why) return why;
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  if (norm[0]) p.putString("cloudUrl", norm);
  else p.remove("cloudUrl");
  p.end();
  g_cl.kick = 1;
  return nullptr;
}

// ============================================================================
// Firmware image state (cloud updates and rollback)
// ============================================================================
// The Arduino core normally marks a freshly flashed image valid before setup()
// runs. Returning true here leaves that to cloudBootCheck(), so an image that
// came from the cloud stays on probation until it has synced once.
extern "C" bool verifyRollbackLater() { return true; }

// Call first in setup(). An image from the web page or USB is accepted at once,
// as before. One from a cloud update is accepted by its first good sync.
inline void cloudBootCheck() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
  bool pending = running && esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;

  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  uint8_t  otaSt = p.getUChar("otaSt", 0);      // 2 = a cloud update was written and switched to
  uint32_t addr  = p.getUInt("otaAddr", 0);
  if (otaSt == 2 && running && running->address == addr && pending) {
    g_cloudOtaUnconfirmed = true;                // we are that update: prove it
    Serial.println("[cloud] new firmware from the cloud: on probation until it syncs");
  } else if (otaSt == 2 && running && running->address == addr) {
    p.putUChar("otaSt", 0);                      // already accepted (a bootloader without rollback)
    p.putString("otaFw", FIRMWARE_VERSION);
  } else if (otaSt == 2) {
    // The update was switched to but this is the firmware from before it
    strncpy(g_cl.otaErr, "rolled back: the new firmware did not sync", sizeof(g_cl.otaErr) - 1);
    g_cl.otaAt = millis() | 1;                   // and no new attempt for an hour
    p.putUChar("otaSt", 0);
    p.remove("otaBuild");
    Serial.println("[cloud] the last cloud update was rolled back");
  }
  p.end();
  if (pending && !g_cloudOtaUnconfirmed) esp_ota_mark_app_valid_cancel_rollback();
}

static void cloudOtaConfirm() {
  if (!g_cloudOtaUnconfirmed) return;
  esp_ota_mark_app_valid_cancel_rollback();
  g_cloudOtaUnconfirmed = false;
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putUChar("otaSt", 0);
  p.putString("otaFw", FIRMWARE_VERSION);
  p.end();
  Serial.println("[cloud] new firmware synced: keeping it");
}

// A firmware upload from the web page is the owner's own choice: stop waiting
// on the probation of a cloud update so the spare slot can be written
inline void cloudAcceptRunningImage() { cloudOtaConfirm(); }
inline bool cloudOtaRunning() { return g_cl.otaPct != 255; }

// ============================================================================
// HTTP
// ============================================================================
static const char *cloudResetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "poweron";
    case ESP_RST_EXT:       return "external";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int_wdt";
    case ESP_RST_TASK_WDT:  return "task_wdt";
    case ESP_RST_WDT:       return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    default:                return "unknown";
  }
}

// TLS needs two 16.7 KB record buffers and ~12 KB more while it shakes hands
static bool cloudHeapOk() {
  if (!cloudIsHttps()) return true;
  return ESP.getFreeHeap() >= CLOUD_TLS_MIN_FREE &&
         heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= CLOUD_TLS_MIN_BLOCK;
}

// Open a request and read its status line and headers. Returns the client with
// the body still to read, or nullptr (status < 0: no connection). The caller
// closes it with cloudClose().
static esp_http_client_handle_t cloudOpen(esp_http_client_method_t method, const char *path, const char *body, int &status) {
  status = -1;
  snprintf(g_cUrl, sizeof(g_cUrl), "%s%s", cloudBase(), path);
  esp_http_client_config_t cfg = {};
  cfg.url = g_cUrl;
  cfg.method = method;
  cfg.timeout_ms = CLOUD_TIMEOUT_MS;
  cfg.buffer_size = 1024;
  cfg.buffer_size_tx = 512;
  cfg.user_agent = "vizMini/" FIRMWARE_VERSION;
  cfg.disable_auto_redirect = true;
  cfg.max_authorization_retries = -1;
  cfg.keep_alive_enable = false;
  if (cloudIsHttps()) cfg.cert_pem = CLOUD_ROOT_CA_PEM;

  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return nullptr;
  // His token once he has one. To register, and for as long as he has none
  // (a v1 server issues none), the public v1 value instead
  if (g_cl.token[0] && strcmp(path, "/api/bots/register") != 0) esp_http_client_set_header(c, "X-Bot-Token", g_cl.token);
  else esp_http_client_set_header(c, "X-Bot-Secret", CLOUD_LEGACY_SECRET);
  int len = body ? (int)strlen(body) : 0;
  if (body) esp_http_client_set_header(c, "Content-Type", "application/json");

  if (esp_http_client_open(c, len) != ESP_OK) { esp_http_client_cleanup(c); return nullptr; }
  if (len && esp_http_client_write(c, body, len) != len) { status = -2; esp_http_client_close(c); esp_http_client_cleanup(c); return nullptr; }
  if (esp_http_client_fetch_headers(c) < 0) { status = -3; esp_http_client_close(c); esp_http_client_cleanup(c); return nullptr; }
  status = esp_http_client_get_status_code(c);
  return c;
}

static void cloudClose(esp_http_client_handle_t c) {
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
}

// POST a JSON body; the reply (cut at the cap) lands in g_cResp. Returns the
// HTTP status, or < 0 when there was no connection or no reply.
static int cloudPost(const char *path, const char *body) {
  int status;
  g_cResp[0] = 0;
  esp_http_client_handle_t c = cloudOpen(HTTP_METHOD_POST, path, body, status);
  if (!c) return status;
  int n = 0;
  while (n < (int)sizeof(g_cResp) - 1) {
    int r = esp_http_client_read(c, g_cResp + n, sizeof(g_cResp) - 1 - n);
    if (r <= 0) break;
    n += r;
  }
  g_cResp[n] = 0;
  cloudClose(c);                                  // frees the TLS buffers before anything is parsed
  return status;
}

// ---- request body builder ----------------------------------------------------
static size_t g_cReqLen = 0;
static void reqAdd(const char *fmt, ...) {
  if (g_cReqLen >= sizeof(g_cReq) - 1) return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(g_cReq + g_cReqLen, sizeof(g_cReq) - g_cReqLen, fmt, ap);
  va_end(ap);
  if (n > 0) g_cReqLen = min(g_cReqLen + (size_t)n, sizeof(g_cReq) - 1);
}
// A quoted JSON string
static void reqStr(const char *s) {
  reqAdd("\"");
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') reqAdd("\\%c", *s);
    else if ((uint8_t)*s < 0x20) reqAdd(" ");
    else reqAdd("%c", *s);
  }
  reqAdd("\"");
}

// ============================================================================
// Register
// ============================================================================
// { macAddress, hardwareType, firmwareVersion, localIp, capabilities{...},
//   protocol: 2, name, botId (if he has one), resetReason }
static void cloudBuildRegister() {
  char name[DEVICE_NAME_MAX];
  deviceNameCopy(name, sizeof(name));
  g_cReqLen = 0;
  reqAdd("{\"macAddress\":\"%s\",\"hardwareType\":\"" CLOUD_HARDWARE_TYPE "\",\"firmwareVersion\":\"" FIRMWARE_VERSION "\","
         "\"localIp\":\"%s\",", WiFi.macAddress().c_str(), WiFi.localIP().toString().c_str());
  // What the hardware really is: 128x64 mono OLED, no touch panel, no IMU, an LED strip, no speaker
  reqAdd("\"capabilities\":{\"screenWidth\":%d,\"screenHeight\":%d,\"hasTouch\":false,\"hasIMU\":false,"
         "\"hasLED\":true,\"hasAudio\":false,\"hasBotMode\":true},", OLED_WIDTH, OLED_HEIGHT);
  reqAdd("\"protocol\":2,\"name\":");
  reqStr(name);
  if (g_cl.botId[0]) { reqAdd(",\"botId\":"); reqStr(g_cl.botId); }
  reqAdd(",\"resetReason\":\"%s\"}", cloudResetReason());
}

// approval: "approved" | "pending" | "disabled". A value he does not know counts
// as pending (do nothing). A v1 server sends none: what it answers is approved.
static void cloudReadApproval(const char *root, bool v1IsApproved) {
  char a[12];
  if (jsStr(jsGet(root, "approval"), a, sizeof(a))) {
    g_cl.approval = !strcmp(a, "approved") ? CA_APPROVED : !strcmp(a, "disabled") ? CA_DISABLED : CA_PENDING;
  } else if (v1IsApproved) {
    g_cl.approval = CA_APPROVED;
  }
}

// 200: { botId, name, contentVersion, pollInterval, config{...}, content, botToken, approval }
static bool cloudRegister() {
  cloudBuildRegister();
  bool stale = g_cl.reauth;                       // the old token was refused: the secret header goes out
  int code = cloudPost("/api/bots/register", g_cReq);
  g_cl.lastCode = code;
  g_cl.regAt = millis();
  if (code != 200) return false;

  char id[sizeof(g_cl.botId)], tok[sizeof(g_cl.token)];
  if (!jsStr(jsGet(g_cResp, "botId"), id, sizeof(id)) || !cloudPlainId(id)) { g_cl.err = "odd reply"; g_cl.lastCode = -4; return false; }
  strcpy(g_cl.botId, id);
  if (jsStr(jsGet(g_cResp, "botToken"), tok, sizeof(tok)) && cloudPlainId(tok)) {
    strcpy(g_cl.token, tok);                      // shown once, never again: straight to NVS
    g_cl.reauth = false;
  } else if (stale) {
    g_cl.token[0] = 0;                            // re-registered on a server that issues none
    g_cl.reauth = false;
  }
  long v;
  if (jsInt(jsGet(g_cResp, "contentVersion"), v)) g_cl.contentVer = (int32_t)v;
  if (jsInt(jsGet(g_cResp, "pollInterval"), v)) g_cl.pollS = (uint16_t)constrain(v, 5, CLOUD_POLL_MAX_S);
  cloudReadApproval(g_cResp, false);
  cloudSaveIdentity();
  g_cl.regTried = true;
  Serial.printf("[cloud] registered as %s%s\n", g_cl.botId, g_cl.token[0] ? "" : " (no token: v1 server)");
  return true;
}

// ============================================================================
// Sync
// ============================================================================
// { contentVersion, status, commandAcks[], state{...},
//   protocol: 2, name, firmwareVersion, resetReason (until a sync that is not
//   pending succeeds), configRev, otaError (after a failed update) }
// state carries only what this board has: expression, rssi, freeHeap, uptime,
// ntpTime/ntpSynced and the lights. No personality, botState, IMU, mesh or sensors.
static void cloudBuildSync() {
  char name[DEVICE_NAME_MAX];
  deviceNameCopy(name, sizeof(name));
  g_cReqLen = 0;
  reqAdd("{\"contentVersion\":%ld,\"status\":\"active\",", (long)g_cl.contentVer);
  if (g_cAckN) {
    reqAdd("\"commandAcks\":[");
    for (uint8_t i = 0; i < g_cAckN; i++) reqAdd("%s\"%s\"", i ? "," : "", g_cAcks[i]);
    reqAdd("],");
  }
  reqAdd("\"state\":{\"expression\":%u,\"rssi\":%d,\"freeHeap\":%u,\"uptime\":%lu,",
         (unsigned)g_expr, (int)WiFi.RSSI(), (unsigned)ESP.getFreeHeap(), millis() / 1000UL);
  time_t now = time(nullptr);
  struct tm tmv;
  if (now > 1000000000 && gmtime_r(&now, &tmv)) {  // past 2001: the clock has been set by NTP
    char t[24];
    strftime(t, sizeof(t), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    reqAdd("\"ntpTime\":\"%s\",\"ntpSynced\":true,", t);
  } else {
    reqAdd("\"ntpSynced\":false,");
  }
  // The lights as the web page shows them: the mode and the chosen pattern
  // and palette ("auto" while they cycle)
  reqAdd("\"ledMode\":\"%s\",\"ledPattern\":\"%s\",\"ledPalette\":\"%s\"},",
         leds.modeName(), leds.patternSel(), leds.paletteSel());
  reqAdd("\"protocol\":2,\"name\":");
  reqStr(name);
  reqAdd(",\"firmwareVersion\":\"" FIRMWARE_VERSION "\",");
  if (!g_cl.resetSent) reqAdd("\"resetReason\":\"%s\",", cloudResetReason());
  if (g_cl.otaErr[0]) { reqAdd("\"otaError\":"); reqStr(g_cl.otaErr); reqAdd(","); }
  reqAdd("\"configRev\":%ld}", (long)g_cl.cfgRev);
}

// One command from a sync reply. Returns false only when it should come again
// (no room to hold it); everything else, understood or not, is acked.
static bool cloudTakeCommand(const char *type, const char *payload, uint32_t due) {
  long n;
  char txt[49];
  if (!strcmp(type, "expression")) {
    if (jsInt(jsGet(payload, "value"), n) && n >= 0 && n < BOT_NUM_EXPRESSIONS) return cloudQueue(CJ_EXPRESSION, (int)n, nullptr, due);
  } else if (!strcmp(type, "say")) {                       // { text }: 48 characters fit the bubble
    if (jsStr(jsGet(payload, "text"), txt, sizeof(txt)) && txt[0]) return cloudQueue(CJ_SAY, 0, txt, due);
  } else if (!strcmp(type, "brightness")) {                // { value: 0-100 }, like the v1 command
    if (jsInt(jsGet(payload, "value"), n)) return cloudQueue(CJ_BRIGHTNESS, max(1, (int)(constrain(n, 0, 100) * 255 / 100)), nullptr, due);
  } else if (!strcmp(type, "reboot")) {
    return cloudQueue(CJ_REBOOT, 0, nullptr, due);
  } else if (!strcmp(type, "led_mode")) {                  // { value: mood | ambient | off | solid }
    if (jsStr(jsGet(payload, "value"), txt, sizeof(txt)))
      for (uint8_t i = 0; i < LEDS_NUM_MODES; i++) if (!strcmp(txt, ledModeNames[i])) return cloudQueue(CJ_LED_MODE, i, nullptr, due);
  } else if (!strcmp(type, "led_pattern")) {               // { value: <pattern> | auto }
    if (jsStr(jsGet(payload, "value"), txt, sizeof(txt))) {
      bool known = !strcmp(txt, "auto");
      for (uint8_t i = 0; i < LED_PATTERNS; i++) known |= !strcmp(txt, ledPatternNames[i]);
      if (known) return cloudQueue(CJ_LED_PATTERN, 0, txt, due);
    }
  } else if (!strcmp(type, "led_palette")) {               // { value: <palette> | auto }
    if (jsStr(jsGet(payload, "value"), txt, sizeof(txt))) {
      bool known = !strcmp(txt, "auto");
      for (uint8_t i = 0; i < LED_PALETTES; i++) known |= !strcmp(txt, ledPaletteNames[i]);
      if (known) return cloudQueue(CJ_LED_PALETTE, 0, txt, due);
    }
  }
  return true;                                             // unknown type or a payload he cannot use: ack and ignore
}

// config: { rev, deviceName?, brightness? (0-255), pollIntervalSec? (5-300), personality? (none here) }
// brightness is the LED strip brightness (the OLED has contrast, not a backlight).
// pollIntervalSec is kept as sent, but he never syncs faster than CLOUD_POLL_MIN_S.
static void cloudTakeConfig(const char *cfg) {
  long rev, n;
  if (!jsInt(jsGet(cfg, "rev"), rev)) return;
  bool held = true;
  char nm[DEVICE_NAME_MAX + 8];
  if (jsStr(jsGet(cfg, "deviceName"), nm, sizeof(nm))) {
    if (deviceNameValid(nm)) held &= cloudQueue(CJ_NAME, 0, nm, 0);
    else Serial.println("[cloud] config.deviceName breaks the name rules: left as it is");
  }
  if (jsInt(jsGet(cfg, "brightness"), n)) held &= cloudQueue(CJ_BRIGHTNESS, (int)constrain(n, 1, 255), nullptr, 0);   // 0 is clamped to 1, as on vizbot
  if (jsInt(jsGet(cfg, "pollIntervalSec"), n)) g_cl.pollS = (uint16_t)constrain(n, 5, CLOUD_POLL_MAX_S);
  if (!held) return;                                       // no room just now: the same config comes again
  g_cl.cfgRev = (int32_t)rev;
  cloudSaveIdentity();
}

// ---- cloud firmware update -----------------------------------------------------
struct CloudFirmware { char buildId[40]; char version[33]; char url[112]; char sha[65]; long size; };

static void cloudOtaFail(const char *why) {
  strncpy(g_cl.otaErr, why, sizeof(g_cl.otaErr) - 1);
  g_cl.otaErr[sizeof(g_cl.otaErr) - 1] = 0;
  g_cl.otaPct = 255;
  Serial.printf("[cloud] firmware update failed: %s\n", why);
}

// Download fw over the same pinned connection into the spare app slot. The
// boot slot only changes after the size, the SHA-256 and the image itself
// check out; then he restarts into it on probation (cloudBootCheck).
// Runs in the cloud task with the network lock held.
static void cloudOta(const CloudFirmware &fw) {
  g_cl.otaAt = millis() | 1;
  const esp_partition_t *part = esp_ota_get_next_update_partition(nullptr);
  if (!part) return cloudOtaFail("no spare app slot");
  if (fw.size < 0x10000 || (uint32_t)fw.size > part->size) return cloudOtaFail("size does not fit the app slot");
  if (fw.url[0] != '/' || strlen(fw.sha) != 64) return cloudOtaFail("bad firmware entry");

  Serial.printf("[cloud] firmware %s (%ld bytes): downloading\n", fw.version, fw.size);
  g_cl.otaPct = 0;
  cloudQueue(CJ_SAY, 0, "updating...", 0);
  int status;
  esp_http_client_handle_t c = cloudOpen(HTTP_METHOD_GET, fw.url, nullptr, status);
  if (!c) return cloudOtaFail("no connection");
  if (status != 200) {
    cloudClose(c);
    static char why[24];
    snprintf(why, sizeof(why), "download HTTP %d", status);
    return cloudOtaFail(why);
  }
  int64_t clen = esp_http_client_get_content_length(c);
  if (clen > 0 && clen != fw.size) {                       // a Content-Length must match size
    cloudClose(c);
    static char why[64];
    snprintf(why, sizeof(why), "Content-Length %lld, expected %ld", (long long)clen, fw.size);
    return cloudOtaFail(why);
  }
  if (!Update.begin((size_t)fw.size, U_FLASH)) { cloudClose(c); return cloudOtaFail(Update.errorString()); }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  const char *why = nullptr;
  long got = 0;
  uint32_t t0 = millis();
  while (!why && got < fw.size) {
    int n = 0;                                             // fill the buffer: whole sectors, and a full first header
    while (n < (int)sizeof(g_cResp)) {
      int r = esp_http_client_read(c, g_cResp + n, sizeof(g_cResp) - n);
      if (r <= 0) break;
      n += r;
    }
    if (n <= 0) { why = "download stopped early"; break; }
    if (got == 0) {                                        // an ESP32-C3 app image, nothing else
      const esp_image_header_t *h = (const esp_image_header_t *)g_cResp;
      if (n < (int)sizeof(esp_image_header_t) || h->magic != ESP_IMAGE_HEADER_MAGIC) { why = "not a firmware image"; break; }
      if (h->chip_id != ESP_CHIP_ID_ESP32C3) { why = "firmware is for another chip"; break; }
    }
    if (got + n > fw.size) { why = "longer than its size"; break; }
    if (Update.write((uint8_t *)g_cResp, n) != (size_t)n) { why = Update.errorString(); break; }
    mbedtls_sha256_update(&sha, (const unsigned char *)g_cResp, n);
    got += n;
    g_cl.otaPct = (uint8_t)(got * 100 / fw.size);
    if (millis() - t0 > CLOUD_OTA_MAX_MS) why = "download too slow";
    vTaskDelay(1);                                         // the face and the web page get a turn
  }
  cloudClose(c);                                           // TLS buffers back before the image is checked
  unsigned char dig[32];
  mbedtls_sha256_finish(&sha, dig);
  mbedtls_sha256_free(&sha);
  if (!why && got != fw.size) why = "size mismatch";
  if (!why) {
    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", dig[i]);
    if (strcasecmp(hex, fw.sha) != 0) why = "sha256 mismatch";
  }
  if (why) { Update.abort(); return cloudOtaFail(why); }

  // Mark the attempt before the boot slot changes, so the next boot can tell
  // "running the update" from "rolled back"
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putUInt("otaAddr", part->address);
  p.putString("otaBuild", fw.buildId);
  p.putUChar("otaSt", 2);
  p.end();
  if (!Update.end()) {                                     // checks the image, then switches the boot slot
    p.begin(NVS_NAMESPACE, false);
    p.putUChar("otaSt", 0);
    p.end();
    return cloudOtaFail(Update.errorString());
  }
  g_cl.otaPct = 100;
  Serial.println("[cloud] firmware written: restarting into it");
  cloudQueue(CJ_SAY, 0, "restarting", 0);
  g_cloudRebootAt = millis() + 2500;
}

// firmware: { buildId, version, url, sha256, size } while an update is assigned
static void cloudTakeFirmware(const char *fwj) {
  CloudFirmware fw = {};
  long n = 0;
  if (!jsStrOrNum(jsGet(fwj, "buildId"), fw.buildId, sizeof(fw.buildId)) ||
      !jsStr(jsGet(fwj, "version"), fw.version, sizeof(fw.version)) ||
      !jsStr(jsGet(fwj, "url"), fw.url, sizeof(fw.url)) ||
      !jsStr(jsGet(fwj, "sha256"), fw.sha, sizeof(fw.sha)) ||
      !jsInt(jsGet(fwj, "size"), n)) return;
  fw.size = n;
  if (!strcmp(fw.version, FIRMWARE_VERSION)) return;      // already running it
  for (const char *b = fw.buildId; *b; b++) if (!isalnum((unsigned char)*b) && *b != '-' && *b != '_' && *b != '.') return;
  if (!fw.buildId[0]) return;
  if (g_cloudOtaUnconfirmed || g_cloudRebootAt || otaUploadActive()) return;
  if (g_cl.otaAt && millis() - g_cl.otaAt < CLOUD_OTA_COOLDOWN_MS) return;   // once an hour at most
  // TLS (~47 KB) and Update's 4 KB sector buffer stay up for the whole
  // download while the face and the web page run: wait for a roomy moment
  // (not a failure, so nothing is reported; the next sync looks again)
  if (ESP.getFreeHeap() < CLOUD_OTA_MIN_FREE || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < CLOUD_TLS_MIN_BLOCK) {
    Serial.printf("[cloud] firmware update waits: %u bytes free\n", (unsigned)ESP.getFreeHeap());
    return;
  }
  // A build he already installed, offered again, means its version string is
  // not the one the server expects. Installing it again would loop forever.
  Preferences p;
  p.begin(NVS_NAMESPACE, true);
  bool same = p.getString("otaBuild", "") == fw.buildId && p.getString("otaFw", "") == FIRMWARE_VERSION;
  p.end();
  if (same) {
    if (!g_cl.otaSameWarned) cloudOtaFail("build already installed: it reports version " FIRMWARE_VERSION);
    g_cl.otaSameWarned = true;
    return;
  }
  cloudOta(fw);
}

// 200: { contentVersion, commands[], content, config, groups[], fleet{},
//        approval, firmware }
static bool cloudSync() {
  bool sentReset = !g_cl.resetSent, sentOtaErr = g_cl.otaErr[0] != 0;
  cloudBuildSync();
  char path[64];
  snprintf(path, sizeof(path), "/api/bots/%s/sync", g_cl.botId);
  int code = cloudPost(path, g_cReq);
  g_cl.lastCode = code;
  if (code != 200) return false;
  if (*jsWs(g_cResp) != '{') { g_cl.err = "odd reply"; g_cl.lastCode = -4; return false; }

  g_cAckN = 0;                                             // the acks went out
  g_cl.lastOkMs = millis() | 1;
  cloudOtaConfirm();                                       // new firmware has now proved it can sync

  cloudReadApproval(g_cResp, true);
  if (g_cl.approval != CA_APPROVED) return true;           // pending or disabled: nothing to do, quietly
  // The server keeps what this request carried once only (as vizbot: a
  // pending answer stores nothing, so both go again on the next sync)
  if (sentReset) g_cl.resetSent = true;
  if (sentOtaErr) g_cl.otaErr[0] = 0;
  long v;
  if (jsInt(jsGet(g_cResp, "contentVersion"), v) && v != g_cl.contentVer) { g_cl.contentVer = (int32_t)v; cloudSaveIdentity(); }

  for (const char *el = jsFirst(jsGet(g_cResp, "commands")); el; el = jsNext(el)) {
    if (!jsSkip(el)) break;                                // cut off at the cap: it comes again next time
    if (g_cAckN >= CLOUD_ACK_SLOTS) break;                 // no room to ack more: the rest come again
    char id[40] = "", type[20] = "", at[32] = "";
    jsStr(jsGet(el, "id"), id, sizeof(id));
    jsStr(jsGet(el, "type"), type, sizeof(type));
    uint32_t due = 0;
    if (jsStr(jsGet(el, "execute_at"), at, sizeof(at))) {
      uint32_t when = cloudIsoToEpoch(at), nowS = (uint32_t)time(nullptr);
      if (when && nowS > 1000000000UL && when > nowS + 2) due = when;   // no clock yet: run it now, as vizbot does
    }
    bool taken = cloudTakeCommand(type, jsGet(el, "payload"), due);
    if (taken && cloudPlainId(id) && g_cAckN < CLOUD_ACK_SLOTS) strcpy(g_cAcks[g_cAckN++], id);
    if (taken) Serial.printf("[cloud] command %s\n", type);
  }

  const char *cfg = jsGet(g_cResp, "config");
  if (jsIsObject(cfg) && jsSkip(cfg)) cloudTakeConfig(cfg);

  const char *fw = jsGet(g_cResp, "firmware");
  if (jsIsObject(fw) && jsSkip(fw)) cloudTakeFirmware(fw);
  return true;
}

// ============================================================================
// The cloud task
// ============================================================================
static uint32_t cloudPollMs() {
  uint32_t s = max((uint32_t)g_cl.pollS, (uint32_t)CLOUD_POLL_MIN_S);
  if (g_cl.approval == CA_DISABLED) s = max(s, (uint32_t)CLOUD_DISABLED_POLL_S);
  s <<= min((int)g_cl.slow, 4);                            // 429: back off, up to 10 minutes
  return min(s, (uint32_t)600) * 1000UL;
}

static void cloudAfterCall(bool ok, int code) {
  uint32_t now = millis();
  if (ok) {
    g_cl.fails = 0; g_cl.slow = 0; g_cl.err = "";
    return;
  }
  if (code == 401) {                                       // key refused: an hour, then register again
    g_cl.reauth = true;
    g_cl.err = "key refused";
    g_cl.nextAt = now + CLOUD_AUTH_BACKOFF_S * 1000UL;
    return;
  }
  if (code == 429) {                                       // asked to slow down
    if (g_cl.slow < 4) g_cl.slow = g_cl.slow + 1;
    g_cl.err = "asked to slow down";
    g_cl.nextAt = now + cloudPollMs();
    return;
  }
  if (g_cl.fails < 8) g_cl.fails = g_cl.fails + 1;
  if (code != -4) g_cl.err = code < 0 ? "no connection" : "server error";
  uint32_t wait = (code < 0 ? 30UL : 10UL) << min((int)g_cl.fails - 1, 5);   // 30 s (10 s for an HTTP error), doubling
  g_cl.nextAt = now + min(wait, (uint32_t)CLOUD_BACKOFF_MAX_S) * 1000UL;
}

static void cloudTask(void *) {
  vTaskDelay(pdMS_TO_TICKS(CLOUD_BOOT_DELAY_MS));
  g_cl.nextAt = millis();
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    g_cl.stackFree = (uint16_t)uxTaskGetStackHighWaterMark(nullptr);

    if (g_cl.kick) {                                       // the web page changed the server
      g_cl.kick = 0;
      cloudLoadServer();
      if (g_cl.custom[0]) {                                // a test server always starts from nothing
        Preferences p;
        p.begin(NVS_NAMESPACE, false);
        p.remove("tId"); p.remove("tTok"); p.remove("tRev"); p.remove("tCv"); p.remove("tPoll");
        p.end();
      }
      cloudLoadIdentity();
      g_cl.approval = CA_UNKNOWN; g_cl.reauth = false; g_cl.regTried = false;
      g_cl.fails = 0; g_cl.slow = 0; g_cl.err = ""; g_cl.lastOkMs = 0; g_cl.lastCode = 0;
      g_cAckN = 0;
      g_cl.nextAt = millis();
      Serial.printf("[cloud] server is now %s\n", cloudBase());
    }

    g_cl.online = WiFi.status() == WL_CONNECTED && !webIsAP();
    if (!g_cl.online) continue;
    if (g_cloudRebootAfterSync && !g_cl.reauth) g_cl.nextAt = millis();   // get the reboot's ack out now
    else if (g_cloudRebootAt) continue;                                    // restarting: no new call
    if ((int32_t)(millis() - g_cl.nextAt) < 0) continue;
    if (!cloudHeapOk()) { g_cl.err = "low memory"; g_cl.nextAt = millis() + 30000; continue; }
    if (!netTake(15000)) continue;                         // the weather fetch or a firmware upload has the network

    // Register with no id, with a refused token, or (v1 server: an id but no
    // token) once a boot and every 6 hours in case the server now issues one
    bool reg = g_cl.reauth || !g_cl.botId[0] ||
               (!g_cl.token[0] && (!g_cl.regTried || millis() - g_cl.regAt > 6UL * 3600000UL));
    bool ok;
    if (reg) {
      ok = cloudRegister();
      if (ok) g_cl.nextAt = millis() + 2000;               // then sync straight away
    } else {
      ok = cloudSync();
      if (ok) g_cl.nextAt = millis() + cloudPollMs();
    }
    cloudAfterCall(ok, g_cl.lastCode);
    netGive();
    if (g_cloudRebootAfterSync && !reg) { g_cloudRebootAfterSync = false; g_cloudRebootAt = millis() + 500; }
  }
}

// ============================================================================
// Loop-task side
// ============================================================================
// Call once in setup() after the WiFi is up
inline void cloudBegin() {
  netLockInit();
  cloudLoadServer();
  cloudLoadIdentity();
  Serial.printf("[cloud] %s  bot %s  %s\n", cloudBase(), g_cl.botId[0] ? g_cl.botId : "(not registered)",
                g_cl.token[0] ? "has token" : "no token");
  xTaskCreate(cloudTask, "cloud", CLOUD_TASK_STACK, nullptr, 1, nullptr);
}

static void cloudRunJob(const CloudJobSlot &j) {
  switch (j.job) {
    case CJ_EXPRESSION:  ctlSetExpression((uint8_t)j.value); break;
    case CJ_SAY:         ctlSay(j.text); break;
    case CJ_BRIGHTNESS:  ctlSetLeds(-1, j.value); break;            // the Lights brightness slider
    case CJ_LED_MODE:    ctlSetLeds(j.value, -1); break;
    case CJ_LED_PATTERN: leds.setPattern(j.text); break;
    case CJ_LED_PALETTE: leds.setPalette(j.text); break;
    case CJ_NAME:        if (strcmp(j.text, g_devName) != 0 && deviceNameSet(j.text)) webApplyName(); break;
    case CJ_REBOOT:                                                 // after one more sync, or 20 s at the latest
      ctlSay("restarting");
      if (!g_cloudRebootAt) { g_cloudRebootAt = millis() + 20000; g_cloudRebootAfterSync = true; }
      break;
    default: break;
  }
}

// Call every loop pass: runs what the cloud asked for, on the loop task, through
// the same functions the web page uses
inline void cloudLoop() {
  if (g_cloudRebootAt && (int32_t)(millis() - g_cloudRebootAt) >= 0) ESP.restart();
  if (g_cloudOtaUnconfirmed && millis() > CLOUD_OTA_CONFIRM_MS) {
    Serial.println("[cloud] new firmware never synced: rolling back");
    esp_ota_mark_app_invalid_rollback_and_reboot();
    g_cloudOtaUnconfirmed = false;                         // only reached if there is nothing to roll back to
  }
  if (!g_cqUsed) return;
  uint32_t nowS = (uint32_t)time(nullptr);
  for (uint8_t i = 0; i < CLOUD_JOB_SLOTS; i++) {
    CloudJobSlot j;
    portENTER_CRITICAL(&g_cqMux);
    j = g_cq[i];
    bool run = j.job != CJ_NONE && (j.due == 0 || nowS >= j.due);
    if (run) { g_cq[i].job = CJ_NONE; g_cqUsed = g_cqUsed - 1; }
    portEXIT_CRITICAL(&g_cqMux);
    if (run) cloudRunJob(j);
  }
}

// For /state: {"st":..,"server":..,"url":..,"id":..,"lastS":..,"nextS":..,"err":..,...}
// st: offline (no WiFi) | auth (key refused, re-pairing) | error | approved |
//     pending | disabled | new (not registered yet)
inline String cloudStatusJson() {
  const char *st = !g_cl.online ? "offline" : g_cl.reauth ? "auth" : g_cl.fails ? "error" :
                   g_cl.approval == CA_APPROVED ? "approved" : g_cl.approval == CA_PENDING ? "pending" :
                   g_cl.approval == CA_DISABLED ? "disabled" : "new";
  const char *base = cloudBase(), *host = strstr(base, "://");
  uint32_t now = millis(), last = g_cl.lastOkMs;
  int32_t next = (int32_t)(g_cl.nextAt - now);
  char ota[48] = "";
  if (g_cl.otaPct != 255) snprintf(ota, sizeof(ota), "downloading %u%%", (unsigned)g_cl.otaPct);
  else if (g_cloudOtaUnconfirmed) strcpy(ota, "new firmware, waiting for its first sync");
  String s = "{\"st\":\"";
  s += st;
  s += "\",\"server\":\"" + String(host ? host + 3 : base) + "\",\"url\":\"" + String(g_cl.custom) + "\",\"id\":\"" + String(g_cl.botId) + "\"";
  s += ",\"lastS\":" + String(last ? (long)((now - last) / 1000) : -1L);
  s += ",\"nextS\":" + String(next > 0 ? (long)(next / 1000) : 0L);
  s += ",\"err\":\"" + String(g_cl.err) + "\",\"code\":" + String(g_cl.lastCode);
  s += ",\"rev\":" + String(g_cl.cfgRev) + ",\"poll\":" + String(cloudPollMs() / 1000);
  s += ",\"stack\":" + String(g_cl.stackFree) + ",\"ota\":\"" + String(ota) + "\"}";
  return s;
}

#else  // no WiFi or no cloud: the same calls, doing nothing
inline void cloudBootCheck() {}
inline void cloudBegin() {}
inline void cloudLoop() {}
inline void cloudAcceptRunningImage() {}
inline bool cloudOtaRunning() { return false; }
inline const char *cloudSetServer(const String &) { return "This build has no cloud"; }
inline String cloudStatusJson() { return "{\"st\":\"off\"}"; }
#endif

#endif // CLOUD_CLIENT_H
