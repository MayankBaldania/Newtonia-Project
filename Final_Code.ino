#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WebSocketsServer.h>
#include <Wire.h>

const char* ssid = "Galaxy Mayank";
const char* password = "123456789";

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

int ADXL345 = 0x53;
float X_out;   // raw converted to m/s^2

// EMA filter variables
float X_filtered = 0;  // Filtered X value
const float EMA_ALPHA = 0.1;  // Smoothing factor (0 < alpha < 1, lower = smoother)

// Moving average filter variables
const int MOVING_AVG_SIZE = 2;
float movingAvgBuffer[MOVING_AVG_SIZE];
int bufferIndex = 0;
bool bufferFull = false;
float movingAvgSum = 0;

void handleRoot();
void readADXL();
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

void setup() {
  Serial.begin(115200);

  // WiFi connect
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

  // Configure ADXL345
  // Set ±2g range (256 LSB/g)
  Wire.beginTransmission(ADXL345);
  Wire.write(0x31); // DATA_FORMAT register
  Wire.write(0x00); // ±2g, 10-bit mode
  Wire.endTransmission();

  // Set LPF: ODR = 6.25Hz (bandwidth ~3.13Hz) to reduce vibration noise
  Wire.beginTransmission(ADXL345);
  Wire.write(0x2C); // BW_RATE register
  Wire.write(0x06); // 6.25Hz ODR (0b0110)
  Wire.endTransmission();

  // Wake up ADXL345 (measure mode)
  Wire.beginTransmission(ADXL345);
  Wire.write(0x2D); // POWER_CTL register
  Wire.write(0x08); // Measure mode
  Wire.endTransmission();

  // Initialize moving average buffer
  for (int i = 0; i < MOVING_AVG_SIZE; i++) {
    movingAvgBuffer[i] = 0;
  }

  server.on("/", handleRoot);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  Serial.println("HTTP + WS server started");
}

void loop() {
  server.handleClient();
  webSocket.loop();

  // Send accelerometer data every ~50ms (20Hz)
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 50) {
    lastSend = millis();
    readADXL();

    // Apply moving average filter
    movingAvgSum = movingAvgSum - movingAvgBuffer[bufferIndex];
    movingAvgBuffer[bufferIndex] = X_out;
    movingAvgSum = movingAvgSum + X_out;
    if (bufferFull) {
      X_out = movingAvgSum / MOVING_AVG_SIZE;
    } else if (++bufferIndex >= MOVING_AVG_SIZE) {
      bufferIndex = 0;
      bufferFull = true;
      X_out = movingAvgSum / MOVING_AVG_SIZE;
    }

    // Apply EMA filter to the moving average result
    X_filtered = (EMA_ALPHA * X_out) + ((1.0 - EMA_ALPHA) * X_filtered);

    // Prepare JSON with filtered value (2 decimal places)
    String json = "{\"x\":" + String(X_filtered, 2) + "}";
    webSocket.broadcastTXT(json);
  }
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Real-Time Acceleration</title>
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    body {
      background-color: #1a1a1a;
      color: #ffffff;
      font-family: Arial, sans-serif;
      margin: 0;
      position: relative;
      overflow: auto; /* Changed to allow scrolling */
      min-height: 100vh;
    }
    body::before {
      content: '';
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: radial-gradient(circle, rgba(255, 255, 255, 0.1) 1px, transparent 1px);
      background-size: 10px 10px;
      opacity: 0.5;
      z-index: -1;
    }
    h1{
      text-align: center;
      color: #F6FF00;
      font-family: "Cooper Black";
      font-size: 2.5em;
      letter-spacing: 1px;
      text-shadow: 0 0 8px #FF0000;
    }
    .chart-container {
      display: flex;
      justify-content: center;
      align-items: flex-start;
      max-width: 1000px;
      margin: 20px auto;
    }
    .chart-wrapper {
      flex: 3;
      max-width: 800px;
    }
    canvas {
      background-color: #000000 !important;
      border-radius: 8px;
      padding: 8px;
      width: 100%;
    }
    .button-column {
      flex: 1;
      display: flex;
      flex-direction: column;
      align-items: center;
      margin-left: 20px;
    }
    button {
      background-color: #00f0ff;
      color: #000000;
      border: none;
      padding: 10px 20px;
      margin: 10px 0;
      border-radius: 5px;
      cursor: pointer;
      font-size: 1em;
      text-transform: uppercase;
      transition: background-color 0.3s ease;
      width: 150px;
    }
    button:hover {
      background-color: #00b8cc;
    }
    .force-section {
      text-align: center;
      max-width: 800px;
      margin: 20px auto;
      padding: 10px;
      border: 1px solid rgba(255, 255, 255, 0.2);
      border-radius: 8px;
    }
    .force-section label {
      margin-right: 10px;
    }
    .force-section input {
      background-color: #333;
      color: #fff;
      border: 1px solid #555;
      padding: 5px;
      width: 80px;
      text-align: center;
    }
    .force-section p {
      margin: 10px 0;
    }
    .force-section .description {
      text-align: justify;
      font-size: 0.9em;
      color: #ccc;
      max-width: 600px;
      margin: 0 auto;
    }
    h3{
      color: #FF963B;
    }
    h2{
      color:#82FF3B;
    }
  </style>
</head>
<body>
  <h1>Real-Time Acceleration (m/s<sup>2</sup>)</h1>
  <div class="chart-container">
    <div class="chart-wrapper">
      <canvas id="chart"></canvas>
    </div>
    <div class="button-column">
      <button id="resetButton">Reset Graph</button>
      <button id="toggleButton">Stop Graph</button>
      <p><b>Current Accel:</b> <span id="xVal">0</span> m/s<sup>2</sup></p>
    </div>
  </div>
  <div class="force-section">
    <h2><u><b>Force Force (F) = Mass (m) x Acceleration (a)</u></h2>
    <p><label for="accelInput">Acceleration a (m/s<sup>2</sup>) :</label>
    <input type="number" id="accelInput" value="0" step="0.1"></p>
    <p>Mass m (kg): 0.070 </p>
    
    <h3><b>Force F =</b> <span id="fVal">0.00</span> N</h3>
    <p class="description">Newton's second law of motion states that the net force F acting on an object is equal to the product of its mass m and acceleration a, expressed as F = m x a. This law explains how the velocity of an object changes when subjected to an external force.</p>
  </div>
  <script>
    var ctx = document.getElementById('chart').getContext('2d');
    var chart = new Chart(ctx, {
      type: 'line',
      data: {
        labels: [],
        datasets: [{
          label: 'Accel (m/s^2)',
          borderColor: '#00f0ff',
          backgroundColor: 'rgba(0, 240, 255, 0.1)',
          data: [],
          fill: false,
          borderWidth: 2
        }]
      },
      options: {
        animation: false,
        responsive: true,
        scales: {
          x: { display: false },
          y: {
            min: -6,
            max: 6,
            ticks: { color: '#ffffff' },
            grid: { color: 'rgba(255, 255, 255, 0.1)' }
          }
        },
        plugins: {
          legend: { labels: { color: '#ffffff' } }
        }
      }
    });

    var isGraphRunning = true;
    var currentAccel = 0;
    var ws = new WebSocket('ws://' + window.location.hostname + ':81/');
    ws.onmessage = function(event) {
      if (isGraphRunning) {
        var val = JSON.parse(event.data);
        var xAccel = parseFloat(val.x);
        currentAccel = xAccel;

        document.getElementById("xVal").innerText = xAccel;

        if (chart.data.labels.length > 300) {
          chart.data.labels.shift();
          chart.data.datasets[0].data.shift();
        }
        chart.data.labels.push('');
        chart.data.datasets[0].data.push(xAccel);
        chart.update();

        updateForce();
      }
    };

    function updateForce() {
      var massKg = 70 / 1000; // Fixed mass of 70g = 0.07kg
      var accel = parseFloat(document.getElementById("accelInput").value) || 0;
      var force = massKg * accel;
      document.getElementById("fVal").innerText = force.toFixed(3);
    }

    document.getElementById('resetButton').addEventListener('click', function() {
      chart.data.labels = [];
      chart.data.datasets[0].data = [];
      chart.update();
    });

    document.getElementById('toggleButton').addEventListener('click', function() {
      isGraphRunning = !isGraphRunning;
      this.textContent = isGraphRunning ? 'Stop Graph' : 'Continue Graph';
    });

    document.getElementById('accelInput').addEventListener('input', updateForce);
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
  Wire.write(0x32); // Start reading from DATAX0
  Wire.endTransmission(false);
  Wire.requestFrom(ADXL345, 6, true);

  int16_t x = Wire.read() | (Wire.read() << 8); // X-axis

  // Convert to m/s^2 (±2g, 256 LSB/g, 1g ≈ 9.81 m/s^2)
  X_out = (x / 256.0) * 9.81;
  X_out = -X_out;
}