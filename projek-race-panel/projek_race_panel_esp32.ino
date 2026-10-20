/*
 * ==============================================================================
 * PROJEK RACE PANEL - ESP32 WIRELESS TELEMETRY & MAP SWITCHER
 * Motor: Yamaha MX King V1 | ECU: Red Leo Racing (3.3V Logic)
 * Board: ESP32 DOIT DevKit V1
 * ==============================================================================
 * Fitur:
 * 1. Membaca data serial telemetri dari ECU Red Leo (GPIO 16 & GPIO 17).
 * 2. Memancarkan Wi-Fi Hotspot mandiri ("RacePanel-MXKing").
 * 3. Menyajikan Web Dashboard responsif langsung ke browser HP (192.168.4.1).
 * 4. WebSocket stream live data (RPM, AFR, Suhu, TPS, Voltase, Map).
 * 5. Mengirim perintah ganti Map (Map 1, 2, 3, 4) dari tombol di HP ke ECU.
 * 6. Dilengkapi mode simulasi otomatis saat ECU belum tercolok.
 * ==============================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

// Konfigurasi Wi-Fi Hotspot ESP32
const char *ssid = "RacePanel-MXKing";
const char *password = "racing12345"; // Minimal 8 karakter, atau ubah jadi NULL jika ingin tanpa password

// Pin Serial UART ke ECU Red Leo
#define ECU_RX_PIN 16 // GPIO 16 (RX2) -> Terhubung ke Data TX ECU
#define ECU_TX_PIN 17 // GPIO 17 (TX2) -> Terhubung ke Data RX ECU
#define ECU_BAUD 115200 // Baudrate komunikasi Red Leo (standar 115200 atau 9600)

// Pin Tombol Fisik Cadangan (Opsional jika ingin tombol di stang)
#define PHYSICAL_BUTTON_PIN 4 

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
HardwareSerial EcuSerial(2); // UART2

// State Telemetri Mesin
struct TelemetryData {
  int rpm = 0;
  float afr = 13.0;
  int engineTemp = 85;
  float batteryVolt = 12.6;
  int tps = 0;
  int activeMap = 1;
  bool ecuConnected = false;
} telemetri;

unsigned long lastBroadcast = 0;
unsigned long lastButtonPress = 0;

// ==============================================================================
// KODE HTML, CSS & JAVASCRIPT DASHBOARD SMARTPHONE (EMBEDDED PROGMEM)
// ==============================================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>RACE PANEL | MX KING RED LEO</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Chakra+Petch:wght@600;700&family=Orbitron:wght@700;900&family=Rajdhani:wght@600;700&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg: #07090c;
      --card-bg: rgba(18, 22, 29, 0.85);
      --border: rgba(255, 255, 255, 0.1);
      --cyan: #00f0ff;
      --green: #00ff88;
      --amber: #ffb700;
      --red: #ff2a4b;
      --glow-cyan: 0 0 16px rgba(0, 240, 255, 0.6);
      --glow-red: 0 0 16px rgba(255, 42, 75, 0.6);
      --font-racing: 'Orbitron', sans-serif;
      --font-gauge: 'Rajdhani', sans-serif;
      --font-tech: 'Chakra Petch', sans-serif;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; user-select: none; }
    body {
      background: var(--bg);
      color: #fff;
      font-family: var(--font-gauge);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      padding: 0.75rem;
      background-image: radial-gradient(circle at 50% 10%, rgba(0, 240, 255, 0.08), transparent 60%),
                        linear-gradient(to bottom, #0d1117, #06080b);
    }
    .header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding-bottom: 0.5rem;
      border-bottom: 1px solid var(--border);
      margin-bottom: 0.75rem;
    }
    .brand-title {
      font-family: var(--font-racing);
      font-size: 1.1rem;
      letter-spacing: 1.5px;
      color: #fff;
    }
    .brand-title span { color: var(--red); }
    .status-badge {
      font-family: var(--font-tech);
      font-size: 0.75rem;
      font-weight: 700;
      padding: 3px 8px;
      border-radius: 4px;
      background: rgba(0, 255, 136, 0.15);
      border: 1px solid rgba(0, 255, 136, 0.4);
      color: var(--green);
    }
    .status-badge.disconnected {
      background: rgba(255, 42, 75, 0.15);
      border-color: rgba(255, 42, 75, 0.4);
      color: var(--red);
    }

    /* RPM Tachometer Bar */
    .rpm-container {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 10px;
      padding: 0.75rem;
      margin-bottom: 0.75rem;
    }
    .rpm-header {
      display: flex;
      justify-content: space-between;
      align-items: baseline;
      margin-bottom: 4px;
    }
    .rpm-num {
      font-family: var(--font-racing);
      font-size: 2.5rem;
      font-weight: 900;
      color: var(--cyan);
      line-height: 1;
      text-shadow: var(--glow-cyan);
    }
    .rpm-num.redline {
      color: var(--red);
      text-shadow: var(--glow-red);
      animation: flash 0.15s infinite alternate;
    }
    .rpm-track {
      width: 100%;
      height: 20px;
      background: #151a22;
      border-radius: 4px;
      overflow: hidden;
      display: flex;
      border: 1px solid #283344;
    }
    .rpm-fill {
      height: 100%;
      width: 0%;
      background: linear-gradient(to right, var(--green) 0%, var(--amber) 65%, var(--red) 85%);
      transition: width 0.08s linear;
    }
    .rpm-ticks {
      display: flex;
      justify-content: space-between;
      font-family: var(--font-tech);
      font-size: 0.65rem;
      color: #64748b;
      margin-top: 3px;
    }

    /* Grid Parameter (AFR, Suhu, TPS, Volt) */
    .metrics-grid {
      display: grid;
      grid-template-columns: repeat(2, 1fr);
      gap: 0.65rem;
      margin-bottom: 0.75rem;
    }
    .metric-card {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 0.65rem;
      display: flex;
      flex-direction: column;
      justify-content: space-between;
    }
    .card-label {
      font-family: var(--font-tech);
      font-size: 0.75rem;
      color: #94a3b8;
      letter-spacing: 1px;
    }
    .card-val {
      font-family: var(--font-racing);
      font-size: 1.8rem;
      font-weight: 800;
      color: #fff;
      line-height: 1.1;
      margin: 4px 0;
    }
    .afr-val { color: var(--green); }
    .afr-val.lean { color: var(--red); }
    .afr-val.rich { color: var(--cyan); }
    .card-sub {
      font-size: 0.7rem;
      color: #64748b;
    }

    /* Map Switcher Touch Grid */
    .map-section {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 10px;
      padding: 0.75rem;
      flex: 1;
      display: flex;
      flex-direction: column;
      justify-content: center;
    }
    .section-title {
      font-family: var(--font-tech);
      font-size: 0.8rem;
      color: var(--amber);
      letter-spacing: 1px;
      margin-bottom: 0.6rem;
      text-transform: uppercase;
      font-weight: 700;
    }
    .maps-grid {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 0.5rem;
    }
    .btn-map {
      background: rgba(255, 255, 255, 0.05);
      border: 2px solid #2d3748;
      border-radius: 8px;
      padding: 0.75rem 0.25rem;
      color: #94a3b8;
      font-family: var(--font-racing);
      font-size: 1rem;
      font-weight: 900;
      cursor: pointer;
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 4px;
      transition: all 0.15s ease;
    }
    .btn-map .map-sub {
      font-family: var(--font-tech);
      font-size: 0.6rem;
      font-weight: 600;
      color: #64748b;
    }
    .btn-map:active { transform: scale(0.95); }
    .btn-map.active {
      background: rgba(0, 240, 255, 0.15);
      border-color: var(--cyan);
      color: var(--cyan);
      box-shadow: var(--glow-cyan);
    }
    .btn-map.active .map-sub { color: #fff; }

    @keyframes flash {
      from { opacity: 0.4; } to { opacity: 1; }
    }
  </style>
</head>
<body>
  <header class="header">
    <div class="brand-title">RACE PANEL <span>MX-KING</span></div>
    <div class="status-badge" id="status-badge">ONLINE</div>
  </header>

  <!-- Tachometer RPM -->
  <div class="rpm-container">
    <div class="rpm-header">
      <span class="card-label">ENGINE SPEED (RPM)</span>
      <span class="rpm-num" id="rpm-val">0</span>
    </div>
    <div class="rpm-track">
      <div class="rpm-fill" id="rpm-bar"></div>
    </div>
    <div class="rpm-ticks">
      <span>0</span><span>3K</span><span>6K</span><span>9K</span><span>12K</span><span>14K</span>
    </div>
  </div>

  <!-- Telemetry 4-Card Grid -->
  <div class="metrics-grid">
    <div class="metric-card">
      <span class="card-label">AIR-FUEL RATIO</span>
      <span class="card-val afr-val" id="afr-val">13.0</span>
      <span class="card-sub" id="afr-sub">TARGET: 12.8 - 13.2</span>
    </div>

    <div class="metric-card">
      <span class="card-label">ENGINE TEMP</span>
      <span class="card-val" id="temp-val">85°C</span>
      <span class="card-sub">NORMAL: 80 - 95°C</span>
    </div>

    <div class="metric-card">
      <span class="card-label">THROTTLE (TPS)</span>
      <span class="card-val" id="tps-val">0%</span>
      <span class="card-sub">SENSOR VALVE</span>
    </div>

    <div class="metric-card">
      <span class="card-label">BATTERY VOLT</span>
      <span class="card-val" id="volt-val">12.6V</span>
      <span class="card-sub">CHARGING SYSTEM</span>
    </div>
  </div>

  <!-- Map Switcher Touch Controls -->
  <div class="map-section">
    <div class="section-title">SELECT ECU MAPPING PROFILE</div>
    <div class="maps-grid">
      <button class="btn-map active" data-map="1" id="map-1" onclick="switchMap(1)">
        <span>M1</span>
        <span class="map-sub">HARIAN</span>
      </button>
      <button class="btn-map" data-map="2" id="map-2" onclick="switchMap(2)">
        <span>M2</span>
        <span class="map-sub">TOURING</span>
      </button>
      <button class="btn-map" data-map="3" id="map-3" onclick="switchMap(3)">
        <span>M3</span>
        <span class="map-sub">RACE</span>
      </button>
      <button class="btn-map" data-map="4" id="map-4" onclick="switchMap(4)">
        <span>M4</span>
        <span class="map-sub">BACKFIRE</span>
      </button>
    </div>
  </div>

  <script>
    let socket;
    function initWebSocket() {
      const wsUrl = `ws://${window.location.hostname}:81/`;
      socket = new WebSocket(wsUrl);

      socket.onopen = () => {
        document.getElementById('status-badge').textContent = 'CONNECTED';
        document.getElementById('status-badge').classList.remove('disconnected');
      };

      socket.onclose = () => {
        document.getElementById('status-badge').textContent = 'RECONNECTING...';
        document.getElementById('status-badge').classList.add('disconnected');
        setTimeout(initWebSocket, 2000);
      };

      socket.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          updateUI(data);
        } catch(e) {}
      };
    }

    function updateUI(data) {
      // Update RPM
      const rpmEl = document.getElementById('rpm-val');
      rpmEl.textContent = data.rpm;
      rpmEl.classList.toggle('redline', data.rpm > 12000);

      const rpmPercent = Math.min(100, (data.rpm / 14000) * 100);
      document.getElementById('rpm-bar').style.width = rpmPercent + '%';

      // Update AFR
      const afrEl = document.getElementById('afr-val');
      afrEl.textContent = data.afr.toFixed(1);
      afrEl.className = 'card-val afr-val ' + (data.afr < 12.2 ? 'rich' : (data.afr > 13.5 ? 'lean' : ''));

      // Update Temp, TPS, Volt
      document.getElementById('temp-val').textContent = data.temp + '°C';
      document.getElementById('tps-val').textContent = data.tps + '%';
      document.getElementById('volt-val').textContent = data.volt.toFixed(1) + 'V';

      // Update Map Button Active
      for (let i = 1; i <= 4; i++) {
        const btn = document.getElementById(`map-${i}`);
        if (btn) btn.classList.toggle('active', i === data.map);
      }
    }

    function switchMap(mapNum) {
      if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify({ action: "SET_MAP", map: mapNum }));
      }
      for (let i = 1; i <= 4; i++) {
        const btn = document.getElementById(`map-${i}`);
        if (btn) btn.classList.toggle('active', i === mapNum);
      }
    }

    window.addEventListener('load', initWebSocket);
  </script>
</body>
</html>
)rawliteral";

// ==============================================================================
// LOGIKA PERINTAH SWITCH MAP KE ECU RED LEO
// ==============================================================================
void sendMapCommandToEcu(int mapNumber) {
  // Protokol ganti map Red Leo via Serial UART
  // Format standar frame: [Header] [Command] [Map] [Checksum/End]
  // Contoh umum paket kirim: 0xA5, 0x01, (uint8_t)mapNumber, 0x5A
  byte frame[] = { 0xA5, 0x01, (byte)mapNumber, 0x5A };
  EcuSerial.write(frame, sizeof(frame));
  telemetri.activeMap = mapNumber;

  Serial.printf("[ECU TX] Mengirim perintah Ganti Map ke: %d\n", mapNumber);
}

// ==============================================================================
// HANDLER WEBSOCKET
// ==============================================================================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_TEXT) {
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      const char* action = doc["action"];
      if (strcmp(action, "SET_MAP") == 0) {
        int requestedMap = doc["map"];
        if (requestedMap >= 1 && requestedMap <= 4) {
          sendMapCommandToEcu(requestedMap);
        }
      }
    }
  }
}

// Broadcast data telemetri ke semua HP yang terhubung via WebSocket
void broadcastTelemetry() {
  StaticJsonDocument<256> doc;
  doc["rpm"] = telemetri.rpm;
  doc["afr"] = telemetri.afr;
  doc["temp"] = telemetri.engineTemp;
  doc["tps"] = telemetri.tps;
  doc["volt"] = telemetri.batteryVolt;
  doc["map"] = telemetri.activeMap;

  String output;
  serializeJson(doc, output);
  webSocket.broadcastTXT(output);
}

// ==============================================================================
// PARSER STREAM DATA SERIAL DARI ECU RED LEO
// ==============================================================================
void readEcuStream() {
  while (EcuSerial.available() >= 6) {
    // Cari byte sync awal (header frame Red Leo biasanya 0xA5 atau 0x55)
    if (EcuSerial.read() == 0xA5) {
      byte type = EcuSerial.read();
      if (type == 0x02) { // Frame telemetri live
        byte highRpm = EcuSerial.read();
        byte lowRpm = EcuSerial.read();
        byte afrRaw = EcuSerial.read();
        byte tempRaw = EcuSerial.read();

        telemetri.rpm = (highRpm << 8) | lowRpm;
        telemetri.afr = (float)afrRaw / 10.0;
        telemetri.engineTemp = tempRaw;
        telemetri.ecuConnected = true;
      }
    }
  }
}

// Simulasi jika ECU belum terpasang di atas meja
void simulateDataIfIdle() {
  if (!telemetri.ecuConnected) {
    static float angle = 0;
    angle += 0.05;
    telemetri.rpm = 2500 + sin(angle) * 1800 + (rand() % 200);
    telemetri.afr = 12.8 + sin(angle * 0.5) * 0.4;
    telemetri.engineTemp = 86 + (int)(sin(angle * 0.1) * 4);
    telemetri.tps = max(0, min(100, (int)((telemetri.rpm - 1500) / 45)));
    telemetri.batteryVolt = 12.8 + sin(angle * 0.2) * 0.3;
  }
}

// ==============================================================================
// SETUP UTAMA ESP32
// ==============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- INISIALISASI RACE PANEL MX KING RED LEO ---");

  // Inisialisasi UART2 ke ECU Red Leo di GPIO 16 (RX) dan GPIO 17 (TX)
  EcuSerial.begin(ECU_BAUD, SERIAL_8N1, ECU_RX_PIN, ECU_TX_PIN);

  // Pin Tombol Fisik Cadangan
  pinMode(PHYSICAL_BUTTON_PIN, INPUT_PULLUP);

  // Buat Hotspot Wi-Fi Mandiri
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.printf("Hotspot Aktif: %s\n", ssid);
  Serial.printf("Buka Dashboard di HP: http://%s\n", IP.toString().c_str());

  // Sajikan Halaman Web Dashboard
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });
  server.begin();

  // Inisialisasi WebSocket di Port 81
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

// ==============================================================================
// LOOP UTAMA
// ==============================================================================
void loop() {
  server.handleClient();
  webSocket.loop();

  // Baca data serial dari ECU Red Leo
  readEcuStream();

  // Mode simulasi jika dites di atas meja tanpa ECU
  simulateDataIfIdle();

  // Deteksi Tombol Fisik Cadangan (Debounce)
  if (digitalRead(PHYSICAL_BUTTON_PIN) == LOW && (millis() - lastButtonPress > 400)) {
    lastButtonPress = millis();
    int nextMap = (telemetri.activeMap % 4) + 1; // 1 -> 2 -> 3 -> 4 -> 1
    sendMapCommandToEcu(nextMap);
  }

  // Broadcast data telemetri ke HP setiap 100ms (10 FPS smooth)
  if (millis() - lastBroadcast > 100) {
    lastBroadcast = millis();
    broadcastTelemetry();
  }
}
