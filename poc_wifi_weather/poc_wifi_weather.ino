#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN 14
#define NUM_LEDS 64

Adafruit_NeoPixel matrix(NUM_LEDS, LED_PIN, NEO_RGB + NEO_KHZ800);

const uint8_t font[10][5] = {
  {0b111, 0b101, 0b101, 0b101, 0b111},
  {0b010, 0b110, 0b010, 0b010, 0b111},
  {0b111, 0b001, 0b111, 0b100, 0b111},
  {0b111, 0b001, 0b111, 0b001, 0b111},
  {0b101, 0b101, 0b111, 0b001, 0b001},
  {0b111, 0b100, 0b111, 0b001, 0b111},
  {0b111, 0b100, 0b111, 0b101, 0b111},
  {0b111, 0b001, 0b001, 0b001, 0b001},
  {0b111, 0b101, 0b111, 0b101, 0b111},
  {0b111, 0b101, 0b111, 0b001, 0b111},
};

int xyToIndex(int x, int y) {
  return y * 8 + x;
}

void drawDigit(int digit, int xOffset, int yOffset, uint32_t color) {
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      if (font[digit][row] & (0b100 >> col)) {
        int x = xOffset + col;
        int y = yOffset + row;
        if (x >= 0 && x < 8 && y >= 0 && y < 8) {
          matrix.setPixelColor(xyToIndex(x, y), color);
        }
      }
    }
  }
}

void displayNumber(int num, uint32_t color) {
  matrix.clear();
  int tens = (num / 10) % 10;
  int ones = num % 10;
  if (num >= 10) {
    drawDigit(tens, 1, 1, color);
  }
  drawDigit(ones, 5, 1, color);
  matrix.show();
}

void flashColor(uint32_t color, int ms) {
  matrix.fill(color);
  matrix.show();
  delay(ms);
  matrix.clear();
  matrix.show();
  delay(200);
}

void solidColor(uint32_t color) {
  matrix.fill(color);
  matrix.show();
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  matrix.begin();
  matrix.setBrightness(5);
  matrix.clear();
  matrix.show();

  // White flash = boot
  flashColor(matrix.Color(255, 255, 255), 500);
  Serial.println("Boot");

  // Yellow = connecting to WiFi
  solidColor(matrix.Color(255, 255, 0));
  Serial.println("Connecting to WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  WiFi.begin("powerhouse", "YOUR_PASSWORD_HERE");

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    tries++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    // Red = WiFi failed
    solidColor(matrix.Color(255, 0, 0));
    Serial.println("WiFi failed");
    return;
  }

  // Green flash = WiFi connected
  flashColor(matrix.Color(0, 255, 0), 500);
  Serial.print("Connected: ");
  Serial.println(WiFi.localIP());

  // Blue = connecting to server
  solidColor(matrix.Color(0, 0, 255));
  Serial.println("Fetching weather...");

  WiFiClient client;
  if (!client.connect("api.open-meteo.com", 80)) {
    // Magenta = server connection failed
    solidColor(matrix.Color(255, 0, 255));
    Serial.println("Server connection failed");
    return;
  }

  // Cyan flash = connected to server
  flashColor(matrix.Color(0, 255, 255), 500);

  client.println("GET /v1/forecast?latitude=37.54&longitude=-77.43&current=temperature_2m&temperature_unit=fahrenheit HTTP/1.1");
  client.println("Host: api.open-meteo.com");
  client.println("Connection: close");
  client.println();

  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 10000) {
      // Orange = timeout
      solidColor(matrix.Color(255, 100, 0));
      Serial.println("Timeout");
      client.stop();
      return;
    }
  }

  String response = "";
  while (client.available()) {
    response += (char)client.read();
  }
  client.stop();

  int idx = response.lastIndexOf("\"temperature_2m\":");
  if (idx == -1) {
    // Dark red = parse error
    solidColor(matrix.Color(128, 0, 0));
    Serial.println("Parse error");
    return;
  }

  idx += 17;
  int endIdx = response.indexOf("}", idx);
  String tempStr = response.substring(idx, endIdx);

  Serial.print("Temp: ");
  Serial.println(tempStr);

  int temp = (int)(tempStr.toFloat() + 0.5);

  uint32_t color;
  if (temp < 40) {
    color = matrix.Color(0, 100, 255);  // Blue = cold
  } else if (temp < 75) {
    color = matrix.Color(0, 255, 0);    // Green = nice
  } else {
    color = matrix.Color(255, 50, 0);   // Orange = hot
  }

  displayNumber(temp, color);
  Serial.println("Done");
}

void loop() {
}
