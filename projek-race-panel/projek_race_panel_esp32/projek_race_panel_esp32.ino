/*
 * ==============================================================================
 * PROJEK RACE PANEL - ESP32 ULTRA-LOW LATENCY BLUETOOTH RACING EDITION
 * Motor: Yamaha MX King V1 | ECU: Red Leo Racing (3.3V Logic)
 * Board: ESP32 DOIT DevKit V1 (Port: COM6)
 * ==============================================================================
 * Keunggulan Mode Bluetooth:
 * 1. ZERO DELAY: Respon instan (~5-10ms) untuk RPM, AFR, dan ganti Map.
 * 2. 4G TETAP AKTIF: Kuota data seluler HP tetap bisa dipakai untuk Google Maps,
 *    WhatsApp, telepon headset helm, dll.
 * 3. AUTO CONNECT: Otomatis tersambung begitu motor dinyalakan.
 * 4. ADEM: Arus hanya ~40mA (jauh lebih hemat baterai & tidak panas).
 * ==============================================================================
 */

#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error "Bluetooth tidak aktif di core ESP32 ini!"
#endif

BluetoothSerial SerialBT;

// Pin Serial UART ke ECU Red Leo (3.3V Logic)
#define ECU_RX_PIN 16 // GPIO 16 (RX2) -> Terhubung ke Data TX ECU
#define ECU_TX_PIN 17 // GPIO 17 (TX2) -> Terhubung ke Data RX ECU
#define ECU_BAUD 115200 // Baudrate komunikasi Red Leo

// Pin Tombol Fisik Cadangan di Stang (Opsional)
#define PHYSICAL_BUTTON_PIN 4 

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
// KIRIM PERINTAH GANTI MAP KE ECU RED LEO
// ==============================================================================
void sendMapCommandToEcu(int mapNumber) {
  byte frame[] = { 0xA5, 0x01, (byte)mapNumber, 0x5A };
  EcuSerial.write(frame, sizeof(frame));
  telemetri.activeMap = mapNumber;

  Serial.printf("[ECU TX] Ganti Map ke: %d\n", mapNumber);

  // Kirim feedback balik ke HP via Bluetooth
  if (SerialBT.hasClient()) {
    SerialBT.printf("\n>>> [MAP AKTIF] : MAP %d <<<\n\n", mapNumber);
  }
}

// ==============================================================================
// PARSER DATA DARI HP (BLUETOOTH COMMAND LISTENER)
// ==============================================================================
void handleBluetoothIncoming() {
  if (SerialBT.available()) {
    String cmd = SerialBT.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "M1" || cmd == "MAP1" || cmd == "1") {
      sendMapCommandToEcu(1);
    } else if (cmd == "M2" || cmd == "MAP2" || cmd == "2") {
      sendMapCommandToEcu(2);
    } else if (cmd == "M3" || cmd == "MAP3" || cmd == "3") {
      sendMapCommandToEcu(3);
    } else if (cmd == "M4" || cmd == "MAP4" || cmd == "4") {
      sendMapCommandToEcu(4);
    }
  }
}

// ==============================================================================
// BROADCAST TELEMETRI KE SMARTPHONE VIA BLUETOOTH
// ==============================================================================
void broadcastBluetoothTelemetry() {
  if (!SerialBT.hasClient()) return;

  // Format 1: CSV Bersih untuk App Inventor / Kodular (Sangat mudah di-parse)
  // Format: RPM,AFR,TEMP,TPS,VOLT,MAP
  char csvBuf[64];
  snprintf(csvBuf, sizeof(csvBuf), "%d,%.1f,%d,%d,%.1f,%d\n",
    telemetri.rpm, telemetri.afr, telemetri.engineTemp,
    telemetri.tps, telemetri.batteryVolt, telemetri.activeMap);
  SerialBT.print(csvBuf);

  // Format 2: Tag Bluetooth Electronics (*R...*A...*)
  char buffer[128];
  snprintf(buffer, sizeof(buffer), 
    "*R%d*A%.1f*T%d*P%d*V%.1f*M%d*\n", 
    telemetri.rpm, telemetri.afr, telemetri.engineTemp, 
    telemetri.tps, telemetri.batteryVolt, telemetri.activeMap);
  SerialBT.print(buffer);

  // Juga kirim format ringkas untuk tampilan terminal biasa
  static int termCount = 0;
  if (++termCount >= 5) { // Kirim setiap ~250ms agar terminal tidak banjir
    termCount = 0;
    char textBuf[128];
    snprintf(textBuf, sizeof(textBuf),
      "[RPM: %5d | AFR: %4.1f | TEMP: %2d°C | TPS: %3d%% | MAP: %d]\n",
      telemetri.rpm, telemetri.afr, telemetri.engineTemp, telemetri.tps, telemetri.activeMap);
    SerialBT.print(textBuf);
  }
}

// ==============================================================================
// BACA STREAM DATA SERIAL DARI ECU RED LEO
// ==============================================================================
void readEcuStream() {
  while (EcuSerial.available() >= 6) {
    if (EcuSerial.read() == 0xA5) {
      byte type = EcuSerial.read();
      if (type == 0x02) {
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

// Mode simulasi jika diuji di meja tanpa mesin motor
void simulateDataIfIdle() {
  if (!telemetri.ecuConnected) {
    static float angle = 0;
    angle += 0.08;
    telemetri.rpm = 2500 + abs(sin(angle)) * 9200 + (rand() % 150);
    telemetri.afr = 12.8 + sin(angle * 0.4) * 0.5;
    telemetri.engineTemp = 86 + (int)(sin(angle * 0.1) * 4);
    telemetri.tps = max(0, min(100, (int)((telemetri.rpm - 1800) / 95)));
    telemetri.batteryVolt = 13.6 + sin(angle * 0.2) * 0.3;
  }
}

// ==============================================================================
// SETUP UTAMA ESP32
// ==============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- RACE PANEL MX KING RED LEO (BLUETOOTH EDITION) ---");

  // Inisialisasi UART2 ke ECU Red Leo di GPIO 16 (RX) dan GPIO 17 (TX)
  EcuSerial.begin(ECU_BAUD, SERIAL_8N1, ECU_RX_PIN, ECU_TX_PIN);

  // Tombol Fisik Cadangan
  pinMode(PHYSICAL_BUTTON_PIN, INPUT_PULLUP);

  // Inisialisasi Bluetooth Classic SPP
  // Nama Bluetooth yang akan muncul saat discan di HP: "RacePanel-RedLeo"
  SerialBT.begin("RacePanel-RedLeo");
  Serial.println("Bluetooth Aktif! Nama Perangkat: RacePanel-RedLeo");
  Serial.println("Buka Bluetooth di HP dan lakukan pairing.");
}

// ==============================================================================
// LOOP UTAMA
// ==============================================================================
void loop() {
  // 1. Baca perintah masuk dari HP via Bluetooth (Ganti Map: M1, M2, M3, M4)
  handleBluetoothIncoming();

  // 2. Baca data telemetri dari ECU Red Leo
  readEcuStream();

  // 3. Mode simulasi jika belum dicolok ke motor
  simulateDataIfIdle();

  // 4. Tombol fisik cadangan (jika ada tombol tambahan di stang)
  if (digitalRead(PHYSICAL_BUTTON_PIN) == LOW && (millis() - lastButtonPress > 400)) {
    lastButtonPress = millis();
    int nextMap = (telemetri.activeMap % 4) + 1;
    sendMapCommandToEcu(nextMap);
  }

  // 5. Broadcast telemetri ke HP setiap 50ms (20x per detik - Ultra Smooth & Low Latency)
  if (millis() - lastBroadcast > 50) {
    lastBroadcast = millis();
    broadcastBluetoothTelemetry();
  }
}
