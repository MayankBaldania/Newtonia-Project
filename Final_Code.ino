#include <WiFi.h>               // Library for WiFi connectivity on ESP32-C3
#include <WebServer.h>          // Library for HTTP web server
#include <ESPmDNS.h>            // Library for mDNS to access device via hostname
#include <WebSocketsServer.h>    // Library for WebSocket communication
#include <Wire.h>               // Library for I2C communication with ADXL345

// WiFi credentials
const char* ssid = "Galaxy Mayank";     
const char* password = "123456789";     

WebServer server(80);                   // HTTP server on port 80 for webpage
WebSocketsServer webSocket = WebSocketsServer(81); // WebSocket server on port 81 for real-time data

int ADXL345 = 0x53;                     // I2C address of ADXL345 accelerometer
float X_out;                            // Raw X-axis acceleration (m/s^2)

float X_filtered = 0
const float EMA_ALPHA = 0.1;            

const int MOVING_AVG_SIZE = 2;          
float movingAvgBuffer[MOVING_AVG_SIZE]; 
int bufferIndex = 0;                    
bool bufferFull = false;                
float movingAvgSum = 0;             

// Function declarations
void handleRoot();
void readADXL();
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

void setup() {
  Serial.begin(115200);                 // Start serial communication at 115200 baud for debugging

  WiFi.begin(ssid, password);           // Connect to WiFi network
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) { // Wait for connection
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString()); 

  if (MDNS.begin("newtonia")) {         // Start mDNS with hostname "newtonia"
    Serial.println("http://newtonia.local"); // Accessible via hostname
  }

  Wire.begin(4, 5);                     // Initialize I2C with SDA=pin 4, SCL=pin 5 (ESP32-C3 defaults)

  // Configure ADXL345 for ±2g range (256 LSB/g)
  Wire.beginTransmission(ADXL345);
  Wire.write(0x31);                   
  Wire.write(0x00);                   
  Wire.endTransmission();

  Wire.beginTransmission(ADXL345);
  Wire.write(0x2C);                   
  Wire.write(0x06);                   
  Wire.endTransmission();

  Wire.beginTransmission(ADXL345);
  Wire.write(0x2D);                   
  Wire.write(0x08);                   
  Wire.endTransmission();

  for (int i = 0; i < MOVING_AVG_SIZE; i++) { 
    movingAvgBuffer[i] = 0;
  }

  server.on("/", handleRoot);           // Define HTTP route for root URL
  server.begin();                       // Start HTTP server

  webSocket.begin();                    // Start WebSocket server
  webSocket.onEvent(onWebSocketEvent);  // Attach WebSocket event handler

  Serial.println("HTTP + WS server started"); // Confirm servers are running
}

void loop() {
  server.handleClient();                // Handle HTTP client requests
  webSocket.loop();                     // Handle WebSocket events

  static unsigned long lastSend = 0;    
  if (millis() - lastSend > 50) {       // Send data every 50ms
    lastSend = millis();                
    readADXL();                         

    // Update moving average
    movingAvgSum -= movingAvgBuffer[bufferIndex]; 
    movingAvgBuffer[bufferIndex] = X_out;        
    movingAvgSum += X_out;                       

    if (bufferFull) {                   
      X_out = movingAvgSum / MOVING_AVG_SIZE;
    } else if (++bufferIndex >= MOVING_AVG_SIZE) { 
      bufferIndex = 0;                  
      bufferFull = true;                
      X_out = movingAvgSum / MOVING_AVG_SIZE; 
    }

    // Apply EMA filter for additional smoothing
    X_filtered = (EMA_ALPHA * X_out) + ((1.0 - EMA_ALPHA) * X_filtered);

    // Send smoothed data as JSON via WebSocket
    String json = "{\"x\":" + String(X_filtered, 2) + "}";
    webSocket.broadcastTXT(json);       // Broadcast to all connected clients
  }
}

// Serve the HTML webpage
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Real-Time Acceleration</title>
  <meta name="viewport" content="width=device-width,initial-scale=1" /> <!-- Responsive viewport -->
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script> <!-- Include Chart.js for graphing -->
  <style>
    body {
      background-color: #1a1a1a;       /* Dark background */
      color: #ffffff;                 /* White text */
      font-family: Arial, sans-serif;
      margin: 0;
      position: relative;
      overflow: auto;                 /* Allow scrolling */
      min-height: 100vh;
    }
    body::before {
      content: '';
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: radial-gradient(circle, rgba(255, 255, 255, 0.1) 1px, transparent 1px); /* Subtle grid pattern */
      background-size: 10px 10px;
      opacity: 0.5;
      z-index: -1;
    }
    h1 {
      text-align: center;
      color: #F6FF00;                /* Yellow title */
      font-family: "Cooper Black";
      font-size: 2.5em;
      letter-spacing: 1px;
      text-shadow: 0 0 8px #FF0000; /* Red glow effect */
    }
    .chart-container {
      display: flex;
      justify-content: center;
      align-items: flex-start;
      max-width: 1000px;
      margin: 20px auto;            /* Center chart */
    }
    .chart-wrapper {
      flex: 3;
      max-width: 800px;             /* Limit chart width */
    }
    canvas {
      background-color: #000000 !important; /* Black canvas background */
      border-radius: 8px;
      padding: 8px;
      width: 100%;
    }
    .button-column {
      flex: 1;
      display: flex;
      flex-direction: column;
      align-items: center;
      margin-left: 20px;            /* Space buttons from chart */
    }
    button {
      background-color: #00f0ff;    /* Cyan buttons */
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
      background-color: #00b8cc;    /* Darker cyan on hover */
    }
    .force-section {
      text-align: center;
      max-width: 800px;
      margin: 20px auto;
      padding: 10px;
      border: 1px solid rgba(255, 255, 255, 0.2);
      border-radius: 8px;           /* Styled force calculation section */
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
      text-align: center;           /* Input for acceleration */
    }
    .force-section p {
      margin: 10px 0;
    }
    .force-section .description {
      text-align: justify;
      font-size: 0.9em;
      color: #ccc;
      max-width: 600px;
      margin: 0 auto;               /* Description of Newton’s second law */
    }
    h3 {
      color: #FF963B;               /* Orange force label */
    }
    h2 {
      color: #82FF3B;               /* Green force title */
    }
  </style>
</head>
<body>
  <h1>Real-Time Acceleration (m/s<sup>2</sup>)</h1> <!-- Page title -->
  <div class="chart-container">
    <div class="chart-wrapper">
      <canvas id="chart"></canvas>  <!-- Canvas for Chart.js graph -->
    </div>
    <div class="button-column">
      <button id="resetButton">Reset Graph</button> <!-- Clear graph data -->
      <button id="toggleButton">Stop Graph</button> <!-- Pause/resume graph -->
      <p><b>Current Accel:</b> <span id="xVal">0</span> m/s<sup>2</sup></p> <!-- Display current acceleration -->
    </div>
  </div>
  <div class="force-section">
    <h2><u><b>Force Force (F) = Mass (m) x Acceleration (a)</u></b></h2>
    <p><label for="accelInput">Acceleration a (m/s<sup>2</sup>) :</label>
    <input type="number" id="accelInput" value="0" step="0.1"></p> <!-- Input for force calculation -->
    <p>Mass m (kg): 0.070 </p>   <!-- Fixed mass of 70g -->
    <h3><b>Force F =</b> <span id="fVal">0.00</span> N</h3> <!-- Display calculated force -->
    <p class="description">Newton's second law of motion states that the net force F acting on an object is equal to the product of its mass m and acceleration a, expressed as F = m x a. This law explains how the velocity of an object changes when subjected to an external force.</p>
  </div>
  <script>
    var ctx = document.getElementById('chart').getContext('2d'); // Get canvas context
    var chart = new Chart(ctx, {   // Initialize Chart.js line graph
      type: 'line',
      data: {
        labels: [],                // Array for x-axis labels (empty)
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
        animation: false,          // Disable animations for performance
        responsive: true,         // Adjust chart to container size
        scales: {
          x: { display: false },   // Hide x-axis
          y: {
            min: -6,               // Set y-axis range for acceleration
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
    var ws = new WebSocket('ws://' + window.location.hostname + ':81/'); // Connect to WebSocket
    ws.onmessage = function(event) { // Handle incoming WebSocket messages
      if (isGraphRunning) {       
        var val = JSON.parse(event.data); 
        var xAccel = parseFloat(val.x); 
        currentAccel = xAccel;    

        document.getElementById("xVal").innerText = xAccel; // Display current acceleration

        if (chart.data.labels.length > 300) { // Limit to 300 points
          chart.data.labels.shift(); 
          chart.data.datasets[0].data.shift(); 
        }
        chart.data.labels.push(''); 
        chart.data.datasets[0].data.push(xAccel); 
        chart.update();            // Update graph

        updateForce();             
      }
    };

    // Calculate force based on input acceleration
    function updateForce() {
      var massKg = 70 / 1000;    // Fixed mass of 0.07 kg
      var accel = parseFloat(document.getElementById("accelInput").value) || 0; // Get input acceleration
      var force = massKg * accel; // Apply F = m * a
      document.getElementById("fVal").innerText = force.toFixed(3); // Display force with 3 decimal places
    }

    // Reset graph data on button click
    document.getElementById('resetButton').addEventListener('click', function() {
      chart.data.labels = [];    
      chart.data.datasets[0].data = []; 
      chart.update();            
    });

    // Toggle graph updates on button click
    document.getElementById('toggleButton').addEventListener('click', function() {
      isGraphRunning = !isGraphRunning; 
      this.textContent = isGraphRunning ? 'Stop Graph' : 'Continue Graph'; // Update button text
    });

    // Update force when acceleration input changes
    document.getElementById('accelInput').addEventListener('input', updateForce);
  </script>
</body>
</html>
  )rawliteral";

  server.send(200, "text/html", html); // Send HTML webpage to client
}

// Handle WebSocket events
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {     
    Serial.printf("Client %u connected\n", num);
  }
}

// Read X-axis acceleration from ADXL345
void readADXL() {
  Wire.beginTransmission(ADXL345);
  Wire.write(0x32);                 // Start at DATAX0 register
  Wire.endTransmission(false);
  Wire.requestFrom(ADXL345, 6, true); // Request 6 bytes (X, Y, Z)

  int16_t x = Wire.read() | (Wire.read() << 8); // Read X-axis (16-bit, two’s complement)

  // Convert to m/s^2 (±2g, 256 LSB/g, 1g ≈ 9.81 m/s^2)
  X_out = (x / 256.0) * 9.81;
  X_out = -X_out;                    
}
