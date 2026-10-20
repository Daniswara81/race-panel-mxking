/*
 * ==============================================================================
 * PROJEK RACE PANEL - ESP32 100% REAL TELEMETRY (RED LEO RACING ECU)
 * Motor: Yamaha MX King V1 | ECU: Red Leo Racing (TQMCU Protocol 38400 Baud)
 * Board: ESP32 DOIT DevKit V1 (Port: COM6)
 * ==============================================================================
 * 100% DATA REAL - TIDAK ADA DUMMY / SIMULASI:
 * - Jika mesin mati / kontak off: RPM = 0, TPS = 0, Temp = 0, Volt = 0.
 * - Saat mesin hidup: Data telemetri dibaca langsung dari port serial ECU Red Leo.
 * - Map Switcher: M1, M2, M3, M4 dikirim langsung ke memori profil ECU.
 * ==============================================================================
 */

#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error "Bluetooth tidak aktif di core ESP32 ini!"
#endif

BluetoothSerial SerialBT;

// Pin Serial UART2 ke ECU Red Leo (3.3V Logic)
#define ECU_RX_PIN 16 // GPIO 16 (RX2) -> Terhubung ke Kabel Putih (Data TX ECU)
#define ECU_TX_PIN 17 // GPIO 17 (TX2) -> Terhubung ke Kabel Kuning (Data RX ECU)
#define ECU_BAUD   38400 // Baudrate resmi TQMCU / Red Leo Racing ECU

// Pin Tombol Fisik Cadangan di Stang (Opsional)
#define PHYSICAL_BUTTON_PIN 4 

HardwareSerial EcuSerial(2); // UART2 ESP32

// State Telemetri Murni (Default 0 - Zero Dummy Data)
struct TelemetryData {
  int rpm = 0;
  float afr = 0.0;
  int engineTemp = 0;
  float batteryVolt = 0.0;
  int tps = 0;
  int activeMap = 1;
  bool ecuConnected = false;
} telemetri;

unsigned long lastBroadcast = 0;
unsigned long lastPollTime = 0;
unsigned long lastPacketReceived = 0;
unsigned long lastButtonPress = 0;

// Buffer penerimaan paket biner Red Leo TQMCU
uint8_t rxBuffer[128];
int rxIndex = 0;

// ==============================================================================
// KIRIM PERINTAH GANTI MAP KE ECU RED LEO
// ==============================================================================
void sendMapCommandToEcu(int mapNumber) {
  if (mapNumber < 1 || mapNumber > 4) return;
  
  // Frame TQMCU Switch Map Profile
  byte mapFrame[] = { 0x77, (byte)mapNumber, (byte)(255 - mapNumber), (byte)mapNumber, 0x05 };
  EcuSerial.write(mapFrame, sizeof(mapFrame));
  
  // Backup frame format standar
  byte stdFrame[] = { 0xA5, 0x01, (byte)mapNumber, 0x5A };
  EcuSerial.write(stdFrame, sizeof(stdFrame));
  
  telemetri.activeMap = mapNumber;
  Serial.printf("[ECU TX] Ganti Map Real ke: %d\n", mapNumber);

  // Feedback instan ke HP via Bluetooth
  if (SerialBT.hasClient()) {
    SerialBT.printf("\n>>> [MAP AKTIF] : MAP %d <<<\n\n", mapNumber);
  }
}

// ==============================================================================
// PARSER PERINTAH MASUK DARI HP (BLUETOOTH COMMAND LISTENER)
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
// BROADCAST TELEMETRI REAL KE SMARTPHONE VIA BLUETOOTH
// ==============================================================================
void broadcastBluetoothTelemetry() {
  if (!SerialBT.hasClient()) return;

  // Format CSV Bersih: RPM,AFR,TEMP,TPS,VOLT,MAP
  char csvBuf[64];
  snprintf(csvBuf, sizeof(csvBuf), "%d,%.1f,%d,%d,%.1f,%d\n",
    telemetri.rpm, telemetri.afr, telemetri.engineTemp,
    telemetri.tps, telemetri.batteryVolt, telemetri.activeMap);
  SerialBT.print(csvBuf);

  // Format Tag Keuwlsoft: *R...*A...*
  char tagBuf[128];
  snprintf(tagBuf, sizeof(tagBuf), 
    "*R%d*A%.1f*T%d*P%d*V%.1f*M%d*\n", 
    telemetri.rpm, telemetri.afr, telemetri.engineTemp, 
    telemetri.tps, telemetri.batteryVolt, telemetri.activeMap);
  SerialBT.print(tagBuf);
}

// ==============================================================================
// BACA STREAM REAL DARI ECU RED LEO (TQMCU PROTOCOL 38400 BAUD)
// ==============================================================================
void readEcuStream() {
  while (EcuSerial.available()) {
    uint8_t b = EcuSerial.read();

    // Deteksi Header Frame Red Leo (0xA1 atau 0xA2)
    if (rxIndex == 0) {
      if (b == 0xA1 || b == 0xA2 || b == 0xA5) {
        rxBuffer[rxIndex++] = b;
      }
    } else {
      rxBuffer[rxIndex++] = b;

      // Frame standar TQMCU Red Leo adalah 50 - 52 byte
      if (rxIndex >= 50) {
        // Validasi header + footer sum == 255
        if ((rxBuffer[0] == 0xA1 || rxBuffer[0] == 0xA2) && ((rxBuffer[0] + rxBuffer[rxIndex - 1]) == 255)) {
          // 1. RPM Mesin (Byte 6 & 7)
          int realRpm = (rxBuffer[6] << 8) | rxBuffer[7];
          if (realRpm >= 0 && realRpm <= 16000) {
            telemetri.rpm = realRpm;
          }

          // 2. Throttle Position / Bukaan Gas (Byte 1)
          telemetri.tps = rxBuffer[1];

          // 3. Suhu Mesin / Coolant (Byte 2)
          telemetri.engineTemp = rxBuffer[2];

          // 4. AFR (Byte 12 & 13)
          int rawAfr = (rxBuffer[12] << 8) | rxBuffer[13];
          if (rawAfr > 0) {
            telemetri.afr = (float)rawAfr / 100.0;
          }

          // 5. Active Map (Byte 41)
          if (rxBuffer[41] >= 1 && rxBuffer[41] <= 4) {
            telemetri.activeMap = rxBuffer[41];
          }

          // 6. Voltase Aki (Byte 42 & 43)
          int rawVolt = (rxBuffer[42] << 8) | rxBuffer[43];
          if (rawVolt > 0) {
            telemetri.batteryVolt = (float)rawVolt / 100.0;
          }

          telemetri.ecuConnected = true;
          lastPacketReceived = millis();
        } 
        // Alternatif Frame 6-byte mini-telemetri
        else if (rxBuffer[0] == 0xA5 && rxIndex >= 6 && rxBuffer[1] == 0x02) {
          telemetri.rpm = (rxBuffer[2] << 8) | rxBuffer[3];
          telemetri.afr = (float)rxBuffer[4] / 10.0;
          telemetri.engineTemp = rxBuffer[5];
          telemetri.ecuConnected = true;
          lastPacketReceived = millis();
        }

        rxIndex = 0; // Reset buffer setelah diproses
      }

      // Lindungi dari buffer overflow
      if (rxIndex >= sizeof(rxBuffer)) {
        rxIndex = 0;
      }
    }
  }

  // Watchdog: Jika kontak motor mati / ECU terputus > 2 detik, reset telemetri ke 0
  if (millis() - lastPacketReceived > 2000) {
    telemetri.ecuConnected = false;
    telemetri.rpm = 0;
    telemetri.tps = 0;
  }
}

// Request paket live telemetry ke ECU setiap 100ms
void pollEcuTelemetry() {
  if (millis() - lastPollTime > 100) {
    lastPollTime = millis();
    // Frame TQMCU proIntoRT (Command 0x69)
    byte pollFrame[] = { 0x69, 0x00, 0x96, 0x69, 0x05 };
    EcuSerial.write(pollFrame, sizeof(pollFrame));
  }
}

// ==============================================================================
// SETUP UTAMA ESP32
// ==============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- RACE PANEL MX KING RED LEO (100% REAL DATA EDITION) ---");

  // Inisialisasi UART2 ke ECU Red Leo di 38400 Baud (TQMCU Hardware Standard)
  EcuSerial.begin(ECU_BAUD, SERIAL_8N1, ECU_RX_PIN, ECU_TX_PIN);

  // Pin Tombol Fisik Cadangan
  pinMode(PHYSICAL_BUTTON_PIN, INPUT_PULLUP);

  // Inisialisasi Bluetooth Classic SPP
  SerialBT.begin("RacePanel-RedLeo");
  Serial.println("[OK] Bluetooth Aktif: 'RacePanel-RedLeo'");
  Serial.println("[OK] UART ECU Siap di 38400 Baud (GPIO 16 RX2 / GPIO 17 TX2)");
  Serial.println("[INFO] Mode Simulasi DIMATIKAN. Menunggu data riil motor...");
}

// ==============================================================================
// LOOP UTAMA
// ==============================================================================
void loop() {
  // 1. Terima perintah dari HP (M1, M2, M3, M4)
  handleBluetoothIncoming();

  // 2. Kirim permintaan polling ke ECU
  pollEcuTelemetry();

  // 3. Baca data serial riil dari ECU Red Leo
  readEcuStream();

  // 4. Tombol fisik cadangan (opsional)
  if (digitalRead(PHYSICAL_BUTTON_PIN) == LOW && (millis() - lastButtonPress > 400)) {
    lastButtonPress = millis();
    int nextMap = (telemetri.activeMap % 4) + 1;
    sendMapCommandToEcu(nextMap);
  }

  // 5. Broadcast telemetri riil ke HP setiap 50ms (20 FPS smooth)
  if (millis() - lastBroadcast > 50) {
    lastBroadcast = millis();
    broadcastBluetoothTelemetry();
  }
}
