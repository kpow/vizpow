/*
 * POC: WLED Scrolling Text via JSON API
 *
 * Proves we can reach the WLED device on the LAN and use
 * the scrolling text effect (FX 122) via raw HTTP POST.
 *
 * Flow:
 *   1. Connect to home WiFi
 *   2. GET current WLED state (capture effect index + segment name)
 *   3. Send scrolling text "Hello from vizBot!"
 *   4. Wait 5 seconds
 *   5. Restore previous effect + segment name
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

// Saved state for restore
int savedFx = -1;
char savedSegName[32] = "";

// ============================================================================
// Raw HTTP POST helper — avoids 30KB HTTPClient library
// ============================================================================
// Returns HTTP status code (e.g. 200) or -1 on connection failure.

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

  // Read response status line
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

// ============================================================================
// GET current state — capture effect index and segment name
// ============================================================================
// Reads /json/state and parses "fx" and "n" from the first segment.
// Uses simple string search (no JSON library needed).

bool wledGetState(const char* ip, int port) {
  WiFiClient client;
  client.setTimeout(2000);

  Serial.printf("  GET http://%s:%d/json/state\n", ip, port);

  if (!client.connect(ip, port)) {
    Serial.println("  ERROR: connection failed");
    return false;
  }

  client.printf("GET /json/state HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Connection: close\r\n"
                "\r\n",
                ip);

  // Wait for response
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 3000) {
      Serial.println("  ERROR: response timeout");
      client.stop();
      return false;
    }
  }

  // Read full response
  String response = "";
  while (client.available()) {
    response += (char)client.read();
  }
  client.stop();

  // Print first part of response for debugging
  Serial.println("  --- Response body (first 500 chars) ---");
  Serial.println(response.substring(response.indexOf("\r\n\r\n") + 4, response.indexOf("\r\n\r\n") + 504));
  Serial.println("  --- end ---");

  // Find "seg" array, then "fx" within it
  int segIdx = response.indexOf("\"seg\"");
  if (segIdx < 0) {
    Serial.println("  ERROR: no 'seg' found in response");
    return false;
  }

  // Find "fx": within seg
  int fxIdx = response.indexOf("\"fx\":", segIdx);
  if (fxIdx >= 0) {
    savedFx = response.substring(fxIdx + 5).toInt();
    Serial.printf("  Captured fx: %d\n", savedFx);
  } else {
    Serial.println("  WARNING: no 'fx' found");
  }

  // Find "n": (segment name) within seg
  int nIdx = response.indexOf("\"n\":\"", segIdx);
  if (nIdx >= 0) {
    nIdx += 5;  // skip past "n":"
    int nEnd = response.indexOf("\"", nIdx);
    if (nEnd > nIdx && (nEnd - nIdx) < 31) {
      response.substring(nIdx, nEnd).toCharArray(savedSegName, 32);
      Serial.printf("  Captured segment name: \"%s\"\n", savedSegName);
    }
  } else {
    Serial.println("  WARNING: no segment name found");
  }

  return true;
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

  // WLED-proven settings — critical for reliable ESP32-S3 connections
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
    Serial.println("\n*** WiFi connection failed. Check credentials. ***");
    return;
  }

  Serial.printf("  CONNECTED! IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("  RSSI: %d dBm\n\n", WiFi.RSSI());

  // ---- Step 2: Capture current WLED state ----
  Serial.println("[2/5] Reading current WLED state...");
  bool gotState = wledGetState(WLED_IP, WLED_PORT);
  if (gotState) {
    Serial.printf("  OK — saved fx=%d, name=\"%s\"\n\n", savedFx, savedSegName);
  } else {
    Serial.println("  WARNING — couldn't read state (will skip restore)\n");
  }

  delay(200);

  // ---- Step 3: Send scrolling text ----
  Serial.println("[3/5] Sending scrolling text to WLED...");

  char body[128];
  snprintf(body, sizeof(body),
    "{\"seg\":[{\"fx\":%d,\"sx\":%d,\"col\":[[255,255,255]],\"n\":\"Hello from vizBot!\"}]}",
    WLED_FX_SCROLL_TEXT, WLED_SCROLL_SPEED);

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

  // ---- Step 5: Restore previous state ----
  Serial.println("[5/5] Restoring previous WLED state...");
  if (savedFx >= 0) {
    char restoreBody[96];
    snprintf(restoreBody, sizeof(restoreBody),
      "{\"seg\":[{\"fx\":%d,\"n\":\"%s\"}]}",
      savedFx, savedSegName);
    rc = wledPost(WLED_IP, WLED_PORT, restoreBody);
    if (rc == 200) {
      Serial.println("  OK — previous state restored!\n");
    } else {
      Serial.printf("  ERROR — HTTP %d\n\n", rc);
    }
  } else {
    Serial.println("  SKIPPED — no saved state to restore\n");
  }

  // ---- Done ----
  Serial.println("========================================");
  Serial.println("  POC complete! Check results above.");
  Serial.println("========================================");
}

void loop() {
  // Nothing — POC runs once in setup()
}
