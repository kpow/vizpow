/*
 * Minimal WiFi AP Test for ESP32-S3-Touch-LCD
 * Just starts WiFi - no LCD, no effects, nothing else
 */

#include <WiFi.h>

const char* ssid = "LCD-Test";
const char* password = "12345678";

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=== WiFi AP Test ===");
  Serial.println("Starting WiFi AP...");

  // Explicitly set mode
  WiFi.mode(WIFI_AP);
  delay(100);

  // Start AP on channel 1
  bool success = WiFi.softAP(ssid, password, 1, false, 4);

  Serial.print("softAP returned: ");
  Serial.println(success ? "SUCCESS" : "FAILED");

  Serial.print("SSID: ");
  Serial.println(ssid);

  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  Serial.print("MAC: ");
  Serial.println(WiFi.softAPmacAddress());

  Serial.println("\nLook for 'LCD-Test' network now...");
  Serial.println("If not visible, this board may have WiFi hardware issues.");
}

void loop() {
  // Print connected clients every 5 seconds
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 5000) {
    lastPrint = millis();
    Serial.print("Connected clients: ");
    Serial.println(WiFi.softAPgetStationNum());
  }
}
