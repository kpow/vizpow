/*
 * POC: WLED Scrolling Text via JSON API
 *
 * Proves we can reach the WLED device on the LAN and use
 * the scrolling text effect (FX 122) via raw HTTP POST.
 *
 * Flow:
 *   1. Connect to home WiFi
 *   2. Save current WLED state to temp preset 255
 *   3. Send scrolling text "Hello from vizBot!"
 *   4. Wait 5 seconds
 *   5. Restore WLED to saved preset 255
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

// ============================================================================
// Raw HTTP POST helper — avoids 30KB HTTPClient library
// ============================================================================
// Returns HTTP status code (e.g. 200) or -1 on connection failure.

int wledPost(const char* ip, int port, const char* jsonBody) {
  WiFiClient client;
  client.setTimeout(2000);  // 2s timeout for POC (generous)

  Serial.printf("  POST http://%s:%d/json/state\n", ip, port);
  Serial.printf("  Body: %s\n", jsonBody);

  if (!client.connect(ip, port)) {
    Serial.println("  ERROR: connection failed");
    return -1;
  }

  // Send HTTP request
  int bodyLen = strlen(jsonBody);
  client.printf("POST /json/state HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s",
                ip, bodyLen, jsonBody);

  // Read response status line (e.g. "HTTP/1.1 200 OK")
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

  // Parse status code from "HTTP/1.1 200 OK"
  int httpCode = -1;
  int spaceIdx = statusLine.indexOf(' ');
  if (spaceIdx > 0) {
    httpCode = statusLine.substring(spaceIdx + 1).toInt();
  }

  // Drain remaining response (don't need the body)
  while (client.available()) {
    client.read();
  }
  client.stop();

  return httpCode;
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

  // ---- Step 2: Save current WLED state to preset 255 ----
  Serial.println("[2/5] Saving current WLED state to preset 255...");
  int rc = wledPost(WLED_IP, WLED_PORT, "{\"psave\":255,\"ib\":true,\"sb\":true}");
  if (rc == 200) {
    Serial.println("  OK — state saved to preset 255\n");
  } else {
    Serial.printf("  WARNING — HTTP %d (continuing anyway)\n\n", rc);
  }

  delay(200);  // Small gap between requests

  // ---- Step 3: Send scrolling text ----
  Serial.println("[3/5] Sending scrolling text to WLED...");

  char body[128];
  snprintf(body, sizeof(body),
    "{\"seg\":[{\"fx\":%d,\"sx\":%d,\"col\":[[255,255,255]],\"n\":\"Hello from vizBot!\"}]}",
    WLED_FX_SCROLL_TEXT, WLED_SCROLL_SPEED);

  rc = wledPost(WLED_IP, WLED_PORT, body);
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
  Serial.println("[5/5] Restoring WLED to preset 255...");
  rc = wledPost(WLED_IP, WLED_PORT, "{\"ps\":255}");
  if (rc == 200) {
    Serial.println("  OK — previous state restored!\n");
  } else {
    Serial.printf("  ERROR — HTTP %d\n\n", rc);
  }

  // ---- Done ----
  Serial.println("========================================");
  Serial.println("  POC complete! Check results above.");
  Serial.println("========================================");
}

void loop() {
  // Nothing — POC runs once in setup()
}
