/*
 * POC: WLED Scrolling Text via JSON API
 *
 * Proves we can reach the WLED device on the LAN and use
 * the scrolling text effect (FX 122) via raw HTTP POST.
 *
 * Flow:
 *   1. Connect to home WiFi
 *   2. GET current WLED state (dump full JSON + capture segment fields)
 *   3. Send scrolling text "Hello from vizBot!" targeting segment id:0
 *   4. Wait 5 seconds
 *   5. Restore previous state by segment id:0
 *
 * Hardware: Any ESP32-S3 board (no LEDs/LCD needed)
 * Serial: 115200 baud for debug output
 */

#include <WiFi.h>

// ============================================================================
// Configuration
// ============================================================================

const char* WIFI_SSID = "powerhouse";
const char* WIFI_PASS = "R00s3v3lt";

const char* WLED_IP   = "10.0.0.226";
const int   WLED_PORT = 80;

// WLED scrolling text effect index
#define WLED_FX_SCROLL_TEXT 122

// Scroll speed (0-255, higher = faster)
#define WLED_SCROLL_SPEED 128

// Which segment to target (by WLED segment ID)
#define WLED_SEGMENT_ID 0

// Saved state for restore
int savedFx = -1;
int savedSx = -1;     // speed
int savedIx = -1;     // intensity
int savedPal = -1;    // palette
char savedSegName[32] = "";

// ============================================================================
// HTTP helpers — raw WiFiClient (avoids 30KB HTTPClient library)
// ============================================================================

// POST JSON to /json/state. Returns HTTP status code or -1.
int wledPost(const char* ip, int port, const char* jsonBody) {
  WiFiClient client;
  client.setTimeout(2000);

  Serial.printf("  POST http://%s:%d/json/state\n", ip, port);
  Serial.printf("  Body: %s\n", jsonBody);

  if (!client.connect(ip, port)) {
    Serial.println("  ERROR: connection failed");
    return -1;
  }

  int bodyLen = strlen(jsonBody);
  client.printf("POST /json/state HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s",
                ip, bodyLen, jsonBody);

  // Wait for response
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 3000) {
      Serial.println("  ERROR: response timeout");
      client.stop();
      return -1;
    }
  }

  String statusLine = client.readStringUntil('\n');
  Serial.printf("  Response: %s\n", statusLine.c_str());

  int httpCode = -1;
  int spaceIdx = statusLine.indexOf(' ');
  if (spaceIdx > 0) {
    httpCode = statusLine.substring(spaceIdx + 1).toInt();
  }

  // Drain remaining response
  while (client.available()) {
    client.read();
  }
  client.stop();

  return httpCode;
}

// GET /json/state — returns full response body (after headers)
String wledGet(const char* ip, int port, const char* path) {
  WiFiClient client;
  client.setTimeout(2000);

  Serial.printf("  GET http://%s:%d%s\n", ip, port, path);

  if (!client.connect(ip, port)) {
    Serial.println("  ERROR: connection failed");
    return "";
  }

  client.printf("GET %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Connection: close\r\n"
                "\r\n",
                path, ip);

  // Wait for response
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 3000) {
      Serial.println("  ERROR: response timeout");
      client.stop();
      return "";
    }
  }

  // Read full response into string
  String response = "";
  timeout = millis();
  while (client.connected() || client.available()) {
    if (client.available()) {
      response += (char)client.read();
    }
    if (millis() - timeout > 5000) break;  // safety timeout
  }
  client.stop();

  // Strip HTTP headers — body starts after \r\n\r\n
  int bodyStart = response.indexOf("\r\n\r\n");
  if (bodyStart >= 0) {
    return response.substring(bodyStart + 4);
  }
  return response;
}

// ============================================================================
// Parse segment state from JSON body
// ============================================================================
// Find the first segment in "seg":[...] and extract key fields.

bool parseSegmentState(const String& json) {
  // Find the seg array
  int segIdx = json.indexOf("\"seg\"");
  if (segIdx < 0) {
    Serial.println("  ERROR: no 'seg' found in JSON");
    return false;
  }

  // Find first '{' after seg (start of first segment object)
  int segStart = json.indexOf('{', segIdx);
  if (segStart < 0) return false;

  // Find matching '}' for this segment (simple: find next '}')
  int segEnd = json.indexOf('}', segStart);
  if (segEnd < 0) return false;

  // Extract just the first segment substring for cleaner parsing
  String seg = json.substring(segStart, segEnd + 1);
  Serial.println("  First segment object:");
  Serial.println("  " + seg);

  // Helper: find int value for "key":value in the segment string
  auto findInt = [&](const char* key, int& out) -> bool {
    char pattern[16];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    int idx = seg.indexOf(pattern);
    if (idx >= 0) {
      out = seg.substring(idx + strlen(pattern)).toInt();
      Serial.printf("  -> %s = %d\n", key, out);
      return true;
    }
    return false;
  };

  findInt("fx", savedFx);
  findInt("sx", savedSx);
  findInt("ix", savedIx);
  findInt("pal", savedPal);

  // Find "n":"value" — segment name
  int nIdx = seg.indexOf("\"n\":\"");
  if (nIdx >= 0) {
    nIdx += 5;
    int nEnd = seg.indexOf("\"", nIdx);
    if (nEnd > nIdx && (nEnd - nIdx) < 31) {
      seg.substring(nIdx, nEnd).toCharArray(savedSegName, 32);
      Serial.printf("  -> n = \"%s\"\n", savedSegName);
    }
  }

  return (savedFx >= 0);
}

// ============================================================================
// Setup — runs the full test sequence
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n========================================");
  Serial.println("  POC: WLED Scrolling Text via JSON API");
  Serial.println("========================================\n");

  // ---- Step 1: Connect to WiFi ----
  Serial.println("[1/5] Connecting to WiFi...");
  Serial.printf("  SSID: %s\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(200);

  WiFi.persistent(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setAutoReconnect(false);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.printf("  try %2d/30  status:%d\n", tries + 1, WiFi.status());
    tries++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("  FAILED — status: %d\n", WiFi.status());
    return;
  }

  Serial.printf("  CONNECTED! IP: %s  RSSI: %d dBm\n\n",
    WiFi.localIP().toString().c_str(), WiFi.RSSI());

  // ---- Step 2: Capture current WLED state ----
  Serial.println("[2/5] Reading current WLED state...");
  String stateJson = wledGet(WLED_IP, WLED_PORT, "/json/state");

  if (stateJson.length() == 0) {
    Serial.println("  ERROR: empty response from WLED\n");
    return;
  }

  // Print full JSON so we can see everything
  Serial.println("  === FULL WLED STATE JSON ===");
  // Print in chunks (Serial buffer is limited)
  for (unsigned int i = 0; i < stateJson.length(); i += 200) {
    Serial.print(stateJson.substring(i, min((unsigned int)stateJson.length(), i + 200)));
  }
  Serial.println("\n  === END JSON ===\n");

  bool gotState = parseSegmentState(stateJson);
  if (gotState) {
    Serial.printf("  OK — saved: fx=%d sx=%d ix=%d pal=%d n=\"%s\"\n\n",
      savedFx, savedSx, savedIx, savedPal, savedSegName);
  } else {
    Serial.println("  WARNING — couldn't parse state (will skip restore)\n");
  }

  delay(200);

  // ---- Step 3: Send scrolling text (target segment by ID) ----
  Serial.println("[3/5] Sending scrolling text to WLED segment %d...");

  char body[160];
  snprintf(body, sizeof(body),
    "{\"seg\":{\"id\":%d,\"fx\":%d,\"sx\":%d,\"col\":[[255,255,255]],\"n\":\"Hello from vizBot!\"}}",
    WLED_SEGMENT_ID, WLED_FX_SCROLL_TEXT, WLED_SCROLL_SPEED);

  int rc = wledPost(WLED_IP, WLED_PORT, body);
  if (rc == 200) {
    Serial.println("  OK — scrolling text sent!\n");
  } else {
    Serial.printf("  ERROR — HTTP %d\n\n", rc);
  }

  // ---- Step 4: Wait 5 seconds ----
  Serial.println("[4/5] Waiting 5 seconds (watch the WLED display)...");
  for (int i = 5; i > 0; i--) {
    Serial.printf("  %d...\n", i);
    delay(1000);
  }
  Serial.println();

  // ---- Step 5: Restore previous state (target segment by ID) ----
  Serial.println("[5/5] Restoring previous WLED state...");
  if (savedFx >= 0) {
    char restoreBody[160];
    snprintf(restoreBody, sizeof(restoreBody),
      "{\"seg\":{\"id\":%d,\"fx\":%d,\"sx\":%d,\"ix\":%d,\"pal\":%d,\"n\":\"%s\"}}",
      WLED_SEGMENT_ID, savedFx, savedSx, savedIx, savedPal, savedSegName);
    rc = wledPost(WLED_IP, WLED_PORT, restoreBody);
    if (rc == 200) {
      Serial.println("  OK — previous state restored!\n");
    } else {
      Serial.printf("  ERROR — HTTP %d\n\n", rc);
    }
  } else {
    Serial.println("  SKIPPED — no saved state to restore\n");
  }

  Serial.println("========================================");
  Serial.println("  POC complete! Check results above.");
  Serial.println("========================================");
}

void loop() {
}
