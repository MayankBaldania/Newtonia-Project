#include <Wire.h>
#include <math.h>
#include <WiFi.h>          // For ESP32 (use <ESP8266WiFi.h> if ESP8266)
#include <WebServer.h>
#include <WebSocketsServer.h>

// ---------- WiFi Setup ----------
const char* ssid     = "Galaxy Mayank";
const char* password = "123456789";

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// ---------- ADXL345 Setup ----------
const int ADXL345_ADDR = 0x53;

// Raw / filtered accelerometer values (g)
float rawX = 0, rawY = 0, rawZ = 0;
float X_out = 0, Y_out = 0, Z_out = 0;
float A_out = 0;

// Thresholds & tuning (in g)
#define MOTION_THRESHOLD 0.05f      // ~0.49 m/s² deadband at rest
const int CAL_SAMPLES = 200;
const unsigned long CAL_DELAY_MS = 5;

// Kalman filter
struct Kalman { float estimate, P, Q, R; };
#define KF_Q 0.005f
#define KF_R 0.08f
Kalman kfX, kfY, kfZ;

// Gravity calibration / normalization
float gravity_cal = 1.0f;
float gX = 0, gY = 0, gZ = 1.0;
float scale_correction = 1.0f;  // multiply readings by this so |g| → 1.000

// ---------- Low-pass state for gravity (initialized after calibrate) ----------
float gravX = 0, gravY = 0, gravZ = 0;
// LPF time constant (seconds) and timing
const float tau = 1.5f;           // how slow gravity should move
uint32_t lastLPFms = 0;

// ---------- I2C helpers ----------
bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ADXL345_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// ---------- ADXL init ----------
bool initADXL345() {
  // Data rate 100 Hz, full-resolution ±2g
  // Order: put into standby, set format/rate, then measure
  if (!writeReg(0x2D, 0x00)) return false;   // POWER_CTL: standby
  if (!writeReg(0x31, 0x08)) return false;   // DATA_FORMAT: FULL_RES=1, ±2g (range=0)
  if (!writeReg(0x2C, 0x0A)) return false;   // BW_RATE: 0x0A => 100 Hz
  if (!writeReg(0x2D, 0x08)) return false;   // POWER_CTL: Measure=1
  return true;
}

// ---------- Read raw ADXL (g units after scale) ----------
bool readRawADXL345(float &ax, float &ay, float &az) {
  Wire.beginTransmission(ADXL345_ADDR);
  Wire.write(0x32); // DATAX0
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom(ADXL345_ADDR, 6, true);
  if (Wire.available() < 6) return false;

  int16_t rx = (int16_t)(Wire.read() | (Wire.read() << 8));
  int16_t ry = (int16_t)(Wire.read() | (Wire.read() << 8));
  int16_t rz = (int16_t)(Wire.read() | (Wire.read() << 8));

  // ADXL345 ~256 LSB/g in full-res; convert to g then normalize by scale_correction
  ax = (rx / 256.0f) * scale_correction;
  ay = (ry / 256.0f) * scale_correction;
  az = (rz / 256.0f) * scale_correction;
  return true;
}

// ---------- Kalman update ----------
float kalmanUpdate(Kalman &kf, float measurement) {
  kf.P += kf.Q;
  float K = kf.P / (kf.P + kf.R);
  kf.estimate += K * (measurement - kf.estimate);
  kf.P *= (1.0f - K);
  return kf.estimate;
}

// ---------- Calibration ----------
void calibrateGravity() {
  double sumX = 0, sumY = 0, sumZ = 0;
  int got = 0;
  for (int i = 0; i < CAL_SAMPLES; i++) {
    // During calibration we want raw counts -> g without normalization first
    Wire.beginTransmission(ADXL345_ADDR);
    Wire.write(0x32);
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(ADXL345_ADDR, 6, true) && Wire.available() >= 6) {
      int16_t rx = (int16_t)(Wire.read() | (Wire.read() << 8));
      int16_t ry = (int16_t)(Wire.read() | (Wire.read() << 8));
      int16_t rz = (int16_t)(Wire.read() | (Wire.read() << 8));
      sumX += rx / 256.0; sumY += ry / 256.0; sumZ += rz / 256.0;
      got++;
    }
    delay(CAL_DELAY_MS);
  }
  if (got == 0) {
    gX = 0; gY = 0; gZ = 1.0; gravity_cal = 1.0;
  } else {
    gX = sumX / got; gY = sumY / got; gZ = sumZ / got;
    gravity_cal = sqrt(gX*gX + gY*gY + gZ*gZ);
  }

  // Normalize so |g| -> 1.000
  scale_correction = (gravity_cal > 0.0001f) ? (1.0f / gravity_cal) : 1.0f;

  // Initialize Kalman with the calibrated baseline
  kfX.estimate = gX * scale_correction;
  kfY.estimate = gY * scale_correction;
  kfZ.estimate = gZ * scale_correction;
  kfX.P = kfY.P = kfZ.P = 1.0f;

  // Initialize LPF with the same baseline (important!)
  gravX = kfX.estimate;
  gravY = kfY.estimate;
  gravZ = kfZ.estimate;

  lastLPFms = millis();
}

// ---------- HTML Page ----------
const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Real-Time Accel Data</title>
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head>
<body>
  <h2>Real-Time Acceleration Magnitude (m/s²)</h2>
  <div style="width: 100%; max-width: 1000px; margin: auto;">
    <canvas id="chart"></canvas>
  </div>
  <script>
    var ctx = document.getElementById('chart').getContext('2d');
    var chart = new Chart(ctx, {
      type: 'line',
      data: { labels: [], datasets: [{label: 'Accel', borderColor: 'blue', data: []}] },
      options: {
        animation: false,
        responsive: true,
        scales: { x: { display:false }, y: { beginAtZero:true } }
      }
    });

    var ws = new WebSocket('ws://' + location.hostname + ':81/');
    ws.onmessage = function(event) {
      var accel = parseFloat(event.data);
      var time = new Date().toLocaleTimeString();
      if (chart.data.labels.length > 300) {
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
      }
      chart.data.labels.push(time);
      chart.data.datasets[0].data.push(accel);
      chart.update();
    };
  </script>
</body>
</html>
)rawliteral";

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  // If your board uses SDA=21, SCL=22, change to Wire.begin(21, 22);
  Wire.begin(4, 5);
  Wire.setClock(400000); // fast I2C for cleaner reads

  if (!initADXL345()) {
    Serial.println("ADXL345 init failed!");
    while (1) delay(500);
  }

  kfX.Q = kfY.Q = kfZ.Q = KF_Q;
  kfX.R = kfY.R = kfZ.R = KF_R;
  kfX.P = kfY.P = kfZ.P = 1.0f;

  calibrateGravity();

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  server.on("/", []() {
    server.send_P(200, "text/html", webpage);
  });
  server.begin();
  webSocket.begin();
}

// ---------- Loop ----------
void loop() {
  float ax, ay, az;
  if (readRawADXL345(ax, ay, az)) {
    // Kalman (denoise)
    X_out = kalmanUpdate(kfX, ax);
    Y_out = kalmanUpdate(kfY, ay);
    Z_out = kalmanUpdate(kfZ, az);

    // --- Low-pass filter to estimate gravity (time-constant form) ---
    uint32_t now = millis();
    float dt = (lastLPFms == 0) ? 0.1f : (now - lastLPFms) / 1000.0f;
    lastLPFms = now;
    float alpha = tau / (tau + dt);     // 0..1 , closer to 1 = slower gravity
    gravX = alpha * gravX + (1.0f - alpha) * X_out;
    gravY = alpha * gravY + (1.0f - alpha) * Y_out;
    gravZ = alpha * gravZ + (1.0f - alpha) * Z_out;

    // --- Subtract gravity to get linear acceleration ---
    float motionX_g = X_out - gravX; 
    float motionY_g = Y_out - gravY; 
    float motionZ_g = Z_out - gravZ; 
    float total_g = sqrtf(X_out*X_out + Y_out*Y_out + Z_out*Z_out); 
    float motion_mag_g = fabs(total_g - 1.0f); 
    if (motion_mag_g < MOTION_THRESHOLD) motion_mag_g = 0.0f;
     A_out = motion_mag_g * 9.81f;


    String msg = String(A_out, 2);
    webSocket.broadcastTXT(msg);
  }

  webSocket.loop();
  server.handleClient();
  delay(50);  // slightly faster loop helps the LPF settle
}
