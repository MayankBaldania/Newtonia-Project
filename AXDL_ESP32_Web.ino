#include <Wire.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>

int ADXL345 = 0x53; // Default I2C address

float X_out, Y_out, Z_out;

// WiFi AP credentials (ESP32 creates its own network)
// const char* apSSID = "ESP32-Accel";
// const char* apPassword = "password123";  // Minimum 8 characters

// Uncomment and fill for station mode (connect to existing WiFi)
const char* ssid = "Galaxy Mayank";
const char* password = "123456789";

AsyncWebServer server(80);
AsyncEventSource events("/events");

unsigned long lastTime = 0;
unsigned long timerDelay = 500;  // Update every 500ms

// HTML web page with text and chart (embedded)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP32 ADXL345 Real-Time Data</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    html { font-family: Arial; text-align: center; }
    h2 { font-size: 2.0rem; }
    p { font-size: 1.5rem; margin: 10px; }
    .value { font-weight: bold; color: #034078; }
    #chartContainer { max-width: 800px; margin: 20px auto; }
  </style>
</head>
<body>
  <h2>ESP32-C3 ADXL345 Accelerometer</h2>
  <p>X: <span id="x" class="value">--</span> g</p>
  <p>Y: <span id="y" class="value">--</span> g</p>
  <p>Z: <span id="z" class="value">--</span> g</p>
  <div id="chartContainer"><canvas id="accelChart"></canvas></div>
<script>
  var ctx = document.getElementById('accelChart').getContext('2d');
  var chart = new Chart(ctx, {
    type: 'line',
    data: {
      labels: [],
      datasets: [{
        label: 'X (g)',
        data: [],
        borderColor: 'red',
        fill: false
      }, {
        label: 'Y (g)',
        data: [],
        borderColor: 'green',
        fill: false
      }, {
        label: 'Z (g)',
        data: [],
        borderColor: 'blue',
        fill: false
      }]
    },
    options: {
      responsive: true,
      scales: {
        x: { title: { display: true, text: 'Update Count' } },
        y: { title: { display: true, text: 'Acceleration (g)' } }
      }
    }
  });

  var count = 0;
  if (!!window.EventSource) {
    var source = new EventSource('/events');
    source.addEventListener('open', function(e) { console.log("Connected"); }, false);
    source.addEventListener('error', function(e) { console.log("Error"); }, false);
    source.addEventListener('readings', function(e) {
      var obj = JSON.parse(e.data);
      document.getElementById("x").innerHTML = obj.x.toFixed(2);
      document.getElementById("y").innerHTML = obj.y.toFixed(2);
      document.getElementById("z").innerHTML = obj.z.toFixed(2);

      chart.data.labels.push(count++);
      chart.data.datasets[0].data.push(obj.x);
      chart.data.datasets[1].data.push(obj.y);
      chart.data.datasets[2].data.push(obj.z);

      if (chart.data.labels.length > 50) {
        chart.data.labels.shift();
        chart.data.datasets.forEach(function(dataset) {
          dataset.data.shift();
        });
      }
      chart.update();
    }, false);
  }
</script>
</body>
</html>)rawliteral";

// Get sensor readings as JSON
String getSensorReadings() {
  Wire.beginTransmission(ADXL345);
  Wire.write(0x32); // Start at ACCEL_XOUT_H
  Wire.endTransmission(false);
  Wire.requestFrom(ADXL345, 6, true);
  int16_t rawX = (Wire.read() | (Wire.read() << 8));
  int16_t rawY = (Wire.read() | (Wire.read() << 8));
  int16_t rawZ = (Wire.read() | (Wire.read() << 8));
  X_out = rawX / 256.0;
  Y_out = rawY / 256.0;
  Z_out = rawZ / 256.0;

  JsonDocument doc;
  doc["x"] = X_out;
  doc["y"] = Y_out;
  doc["z"] = Z_out;
  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

void setup() {
  Serial.begin(9600);
  Wire.begin(4, 5); // SDA = GPIO 4, SCL = GPIO 5
  Wire.beginTransmission(ADXL345);
  Wire.write(0x2D); // POWER_CTL
  Wire.write(8);    // Measurement mode
  Wire.endTransmission();
  delay(10);

  // Initialize WiFi (AP mode)
  // WiFi.softAP(apSSID, apPassword);
  // Serial.print("AP IP Address: ");
  // Serial.println(WiFi.softAPIP());

  // For station mode instead (uncomment and comment AP lines)
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Web server root
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  // Handle SSE connections
  events.onConnect([](AsyncEventSourceClient *client) {
    client->send("hello!", NULL, millis(), 1000);
  });
  server.addHandler(&events);

  server.begin();
}

void loop() {
  if ((millis() - lastTime) > timerDelay) {
    String readings = getSensorReadings();
    events.send(readings.c_str(), "readings", millis());
    lastTime = millis();

    // Optional: Keep serial output for debugging
    Serial.print("Xa= "); Serial.print(X_out);
    Serial.print(" Ya= "); Serial.print(Y_out);
    Serial.print(" Za= "); Serial.println(Z_out);
  }
}