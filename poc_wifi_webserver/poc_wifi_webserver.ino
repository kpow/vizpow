// POC: Prove WiFi AP + STA + WebServer work on ESP32-S3-Touch-LCD
// Based on working weather POC WiFi pattern
//
// What this does:
//   1. Connects to home WiFi (STA) - same as weather POC
//   2. Starts AP "VizPow-Test" so you can connect from phone
//   3. Runs a web server on both interfaces
//   4. Serves a simple page with a button that toggles an LED color
//   5. Serial prints every step so you can see what's happening

#include <WiFi.h>
#include <WebServer.h>

// Same credentials as weather POC / main sketch
const char* STA_SSID = "powerhouse";
const char* STA_PASS = "R00s3velt";
const char* AP_SSID  = "VizPow-Test";
const char* AP_PASS  = "12345678";

WebServer server(80);
bool ledOn = false;

const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>VizPow WiFi Test</title>
  <style>
    body { font-family: sans-serif; background: #1a1a2e; color: #fff;
           text-align: center; padding: 40px 20px; }
    h1 { margin-bottom: 10px; }
    .status { color: #888; margin-bottom: 30px; }
    button { background: #6366f1; border: none; color: #fff;
             padding: 20px 40px; border-radius: 12px; font-size: 18px;
             cursor: pointer; margin: 10px; }
    button:active { background: #4f46e5; }
    .info { background: rgba(255,255,255,0.1); border-radius: 12px;
            padding: 15px; margin: 20px auto; max-width: 300px;
            text-align: left; font-size: 14px; line-height: 1.8; }
  </style>
</head>
<body>
  <h1>VizPow WiFi Test</h1>
  <p class="status" id="status">Connected!</p>
  <button onclick="toggle()">Toggle</button>
  <button onclick="ping()">Ping</button>
  <div class="info" id="info">Loading...</div>
  <script>
    async function api(url) {
      try {
        const r = await fetch(url);
        return await r.text();
      } catch(e) { return 'Error: ' + e; }
    }
    async function toggle() {
      const r = await api('/toggle');
      document.getElementById('status').textContent = r;
    }
    async function ping() {
      const t = Date.now();
      const r = await api('/ping');
      const ms = Date.now() - t;
      document.getElementById('status').textContent = r + ' (' + ms + 'ms)';
    }
    async function loadInfo() {
      const r = await api('/info');
      document.getElementById('info').innerHTML = r;
    }
    loadInfo();
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", webpage);
}

void handleToggle() {
  ledOn = !ledOn;
  Serial.print("Toggle -> ");
  Serial.println(ledOn ? "ON" : "OFF");
  server.send(200, "text/plain", ledOn ? "LED: ON" : "LED: OFF");
}

void handlePing() {
  Serial.println("Ping!");
  server.send(200, "text/plain", "Pong!");
}

void handleInfo() {
  String info = "AP SSID: " + String(AP_SSID) + "<br>";
  info += "AP IP: " + WiFi.softAPIP().toString() + "<br>";
  info += "STA SSID: " + String(STA_SSID) + "<br>";
  info += "STA Status: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Not connected") + "<br>";
  if (WiFi.status() == WL_CONNECTED) {
    info += "STA IP: " + WiFi.localIP().toString() + "<br>";
    info += "RSSI: " + String(WiFi.RSSI()) + " dBm<br>";
  }
  info += "Free Heap: " + String(ESP.getFreeHeap()) + "<br>";
  info += "Uptime: " + String(millis() / 1000) + "s";
  server.send(200, "text/html", info);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n\n=== VizPow WiFi Test ===");

  // Step 1: Clean slate (same as weather POC)
  WiFi.disconnect(true);
  delay(100);

  // Step 2: AP+STA mode
  Serial.println("Setting WIFI_AP_STA mode...");
  WiFi.mode(WIFI_AP_STA);
  delay(100);

  // Step 3: Start AP
  bool apOk = WiFi.softAP(AP_SSID, AP_PASS, 1, false, 4);
  Serial.print("AP started: ");
  Serial.println(apOk ? "YES" : "NO");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // Step 4: Connect to home WiFi (same pattern as weather POC)
  Serial.print("Connecting to ");
  Serial.print(STA_SSID);
  WiFi.begin(STA_SSID, STA_PASS);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("STA connected! IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());
  } else {
    Serial.println("STA connection failed (AP still works)");
  }

  // Step 5: Start web server
  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.on("/ping", handlePing);
  server.on("/info", handleInfo);
  server.begin();
  Serial.println("Web server started!");
  Serial.println("\nConnect to WiFi \"" + String(AP_SSID) + "\" and open http://" + WiFi.softAPIP().toString());
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Or use http://" + WiFi.localIP().toString() + " from your home network");
  }
  Serial.println("=== Ready ===\n");
}

void loop() {
  server.handleClient();
}
