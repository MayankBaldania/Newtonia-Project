#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>

// ------------------- Wi-Fi Credentials -------------------
const char* ssid = "Galaxy Mayank";  
const char* password = "123456789";

// ------------------- Servers -------------------
WebServer server(80);           // HTTP server on port 80
WebSocketsServer webSocket(81); // WebSocket server on port 81

// ------------------- ADXL345 Constants -------------------
#define ADXL345_ADDR 0x53
#define ADXL345_REG_DEVID 0x00
#define ADXL345_REG_POWER_CTL 0x2D
#define ADXL345_REG_DATA_FORMAT 0x31
#define ADXL345_REG_DATAX0 0x32

const float SENSITIVITY_2G = 256.0; // LSB/g
float currentSensitivity = SENSITIVITY_2G;

// ------------------- Sensor Variables -------------------
int16_t rawX, rawY, rawZ;
float accelX, accelY, accelZ;
float linAccelX, linAccelY, linAccelZ;
float gravityX = 0, gravityY = 0, gravityZ = 0;
const float ALPHA = 0.90; // High-pass filter coefficient

// ------------------- HTML Page -------------------
const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 ADXL345 Test</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; text-align: center; }
        .data { margin: 10px; padding: 10px; background: #f0f0f0; border-radius: 5px; }
    </style>
</head>
<body>
    <h1>ESP32 ADXL345 WebSocket Test</h1>
    <p>Check the serial console for WebSocket data.</p>
    <div class="data">Linear X: <span id="x">0</span> m/s²</div>
    <div class="data">Linear Y: <span id="y">0</span> m/s²</div>
    <div class="data">Linear Z: <span id="z">0</span> m/s²</div>

    <script>
        var ws = new WebSocket('ws://' + window.location.hostname + ':81/');
        ws.onmessage = function(event) {
            var data = JSON.parse(event.data);
            document.getElementById('x').textContent = data.x;
            document.getElementById('y').textContent = data.y;
            document.getElementById('z').textContent = data.z;
        };
    </script>
</body>
</html>
)rawliteral";

// ------------------- Functions -------------------
bool initADXL345() {
  Wire.beginTransmission(ADXL345_ADDR);
  Wire.write(ADXL345_REG_DEVID);
  Wire.endTransmission(false);
  Wire.requestFrom(ADXL345_ADDR, 1);
  if (Wire.available() && Wire.read() == 0xE5) {
    Wire.beginTransmission(ADXL345_ADDR);
    Wire.write(ADXL345_REG_POWER_CTL);
    Wire.write(0x08); // Measure bit
    Wire.endTransmission();

    Wire.beginTransmission(ADXL345_ADDR);
    Wire.write(ADXL345_REG_DATA_FORMAT);
    Wire.write(0x00); // ±2g
    Wire.endTransmission();
    return true;
  }
  return false;
}

void readADXL345Data() {
  Wire.beginTransmission(ADXL345_ADDR);
  Wire.write(ADXL345_REG_DATAX0);
  Wire.endTransmission(false);
  Wire.requestFrom(ADXL345_ADDR, 6);

  if (Wire.available() >= 6) {
    rawX = (Wire.read() | (Wire.read() << 8));
    rawY = (Wire.read() | (Wire.read() << 8));
    rawZ = (Wire.read() | (Wire.read() << 8));

    accelX = (rawX / currentSensitivity) * 9.81;
    accelY = (rawY / currentSensitivity) * 9.81;
    accelZ = (rawZ / currentSensitivity) * 9.81;
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
      webSocket.sendTXT(num, "Connected to ESP32 ADXL345 WebSocket Server");
      break;
    }
    case WStype_TEXT:
      break;
  }
}

// ------------------- Setup -------------------
void setup() {
  Serial.begin(115200);
  Wire.begin(4,5);

  // Initialize ADXL345
  if (!initADXL345()) {
    Serial.println("ADXL345 Initialization Failed! Halting.");
    while(1);
  }
  Serial.println("ADXL345 Initialized.");

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);   // clear old config
  delay(1000);

  Serial.printf("Connecting to WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 60) { // wait up to ~30s
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Connected to WiFi!");
    Serial.print("📶 IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ Failed to connect to WiFi. Check hotspot settings.");
  }


  // Start WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started on ws://<IP>:81");

  // HTTP server
  server.on("/", []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });
  server.begin();
  Serial.println("HTTP server started. Go to http://<IP> in your browser.");
}

// ------------------- Loop -------------------
void loop() {
  webSocket.loop();
  server.handleClient();

  readADXL345Data();

  // High-pass filter
  gravityX = ALPHA * gravityX + (1 - ALPHA) * accelX;
  gravityY = ALPHA * gravityY + (1 - ALPHA) * accelY;
  gravityZ = ALPHA * gravityZ + (1 - ALPHA) * accelZ;

  linAccelX = accelX - gravityX;
  linAccelY = accelY - gravityY;
  linAccelZ = accelZ - gravityZ;

  // Create JSON
  String jsonData = "{\"x\":";
  jsonData += String(linAccelX, 2);
  jsonData += ",\"y\":";
  jsonData += String(linAccelY, 2);
  jsonData += ",\"z\":";
  jsonData += String(linAccelZ, 2);
  jsonData += "}";

  webSocket.broadcastTXT(jsonData);
  Serial.println(jsonData);

  delay(50); // ~20 Hz
}
