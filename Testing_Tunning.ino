#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <math.h>

const char* ssid = "Galaxy Mayank";
const char* password = "123456789";

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

int ADXL345 = 0x53;
float X_out, Y_out, Z_out;   // raw converted to m/s^2

// ---- Kalman filter state for each axis ----
float k_x = 0, p_x = 1.0, x_est_x = 0;
float k_y = 0, p_y = 1.0, x_est_y = 0;
float k_z = 0, p_z = 1.0, x_est_z = 0;

const float Q = 0.01;  // process noise variance
const float R = 0.5;   // measurement noise variance
bool kalmanInitialized = false;

// 🔹 Function prototypes
void handleRoot();
void readADXL();
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  if (MDNS.begin("newtonia")) {
    Serial.println("http://newtonia.local");
  }

  Wire.begin(4, 5); // ESP32-C3 default SDA=4, SCL=5
  Wire.beginTransmission(ADXL345);
  Wire.write(0x2D);
  Wire.write(8); // measurement mode
  Wire.endTransmission();

  server.on("/", handleRoot);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  Serial.println("HTTP + WS server started");
}

void loop() {
  server.handleClient();
  webSocket.loop();

  static unsigned long lastSend = 0;
  if (millis() - lastSend > 50) {
    lastSend = millis();
    readADXL();

    if (!kalmanInitialized) {
      x_est_x = X_out;
      x_est_y = Y_out;
      x_est_z = Z_out;
      p_x = p_y = p_z = 1.0;
      kalmanInitialized = true;
    }

    // ---- Kalman filter update per axis ----
    p_x += Q; p_y += Q; p_z += Q;
    k_x = p_x / (p_x + R);
    k_y = p_y / (p_y + R);
    k_z = p_z / (p_z + R);

    x_est_x += k_x * (X_out - x_est_x);
    x_est_y += k_y * (Y_out - x_est_y);
    x_est_z += k_z * (Z_out - x_est_z);

    p_x *= (1 - k_x);
    p_y *= (1 - k_y);
    p_z *= (1 - k_z);

    // Compute magnitude
    float mag = sqrt(x_est_x * x_est_x +
                     x_est_y * x_est_y +
                     x_est_z * x_est_z);

    String json = "{\"magnitude\":" + String(mag, 2) + "}";
    webSocket.broadcastTXT(json);
  }
}

void handleRoot() {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <title>ADXL345 Magnitude (Kalman)</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <style>
      body { font-family: Arial, sans-serif; text-align:center; padding:10px; }
      .vals { margin-bottom: 8px; }
      .container { width: 90%; max-width: 900px; height: 320px; margin: auto; }
    </style>
  </head>
  <body>
    <h2>ADXL345 - Acceleration Magnitude (ESP32C3 + Kalman)</h2>
    <p class="vals"><b>Magnitude:</b> <span id="magVal">0</span> m/s²</p>

    <div class="container"><canvas id="myChart"></canvas></div>

    <script>
      var ctx = document.getElementById('myChart').getContext('2d');
      var chart = new Chart(ctx, {
        type: 'line',
        data: {
          labels: [],
          datasets: [
            { label: 'Acceleration Magnitude (m/s²)', borderColor: 'purple', data: [], fill: false }
          ]
        },
        options: {
          responsive: true,
          maintainAspectRatio: false,
          animation: false,
          scales: {
            x: { display: false },
            y: { min: 6, max: 14 }   // 🔹 FIXED Y-AXIS SCALE
          }
        }
      });

      var socket = new WebSocket('ws://' + window.location.hostname + ':81/');
      socket.onmessage = function(event) {
        var val = JSON.parse(event.data);

        document.getElementById("magVal").innerText = val.magnitude;

        chart.data.labels.push('');
        chart.data.datasets[0].data.push(val.magnitude);

        if (chart.data.labels.length > 100) {
          chart.data.labels.shift();
          chart.data.datasets[0].data.shift();
        }
        chart.update('none');
      };
    </script>
  </body>
  </html>
  )rawliteral";

  server.send(200, "text/html", html);
}


void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.printf("Client %u connected\n", num);
  }
}

void readADXL() {
  Wire.beginTransmission(ADXL345);
  Wire.write(0x32);
  Wire.endTransmission(false);
  Wire.requestFrom(ADXL345, 6, true);

  int16_t x = Wire.read() | (Wire.read() << 8);
  int16_t y = Wire.read() | (Wire.read() << 8);
  int16_t z = Wire.read() | (Wire.read() << 8);

  // Convert to m/s^2 (±2g, 256 LSB/g)
  X_out = (x / 256.0) * 9.81;
  Y_out = (y / 256.0) * 9.81;
  Z_out = (z / 256.0) * 9.81;
}
