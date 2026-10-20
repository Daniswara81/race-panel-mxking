/*
 * ==============================================================================
 * PROJEK RACE PANEL - ESP32 100% REAL TELEMETRY (RED LEO RACING ECU)
 * Motor: Yamaha MX King V1 | ECU: Red Leo Racing (TQMCU Protocol 38400 Baud)
 * Board: ESP32 DOIT DevKit V1 (Port: COM6)
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

#define PHYSICAL_BUTTON_PIN 4 

HardwareSerial EcuSerial(2);

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

uint8_t rxBuffer[128];
int rxIndex = 0;

void sendMapCommandToEcu(int mapNumber) {
  if (mapNumber < 1 || mapNumber > 4) return;
  
  byte mapFrame[] = { 0x77, (byte)mapNumber, (byte)(255 - mapNumber), (byte)mapNumber, 0x05 };
  EcuSerial.write(mapFrame, sizeof(mapFrame));
  
  byte stdFrame[] = { 0xA5, 0x01, (byte)mapNumber, 0x5A };
  EcuSerial.write(stdFrame, sizeof(stdFrame));
  
  telemetri.activeMap = mapNumber;
  if (SerialBT.hasClient()) {
    SerialBT.printf("\n>>> [MAP AKTIF] : MAP %d <<<\n\n", mapNumber);
  }
}

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

void broadcastBluetoothTelemetry() {
  if (!SerialBT.hasClient()) return;

  char csvBuf[64];
  snprintf(csvBuf, sizeof(csvBuf), "%d,%.1f,%d,%d,%.1f,%d\n",
    telemetri.rpm, telemetri.afr, telemetri.engineTemp,
    telemetri.tps, telemetri.batteryVolt, telemetri.activeMap);
  SerialBT.print(csvBuf);

  char tagBuf[128];
  snprintf(tagBuf, sizeof(tagBuf), 
    "*R%d*A%.1f*T%d*P%d*V%.1f*M%d*\n", 
    telemetri.rpm, telemetri.afr, telemetri.engineTemp, 
    telemetri.tps, telemetri.batteryVolt, telemetri.activeMap);
  SerialBT.print(tagBuf);
}

void readEcuStream() {
  while (EcuSerial.available()) {
    uint8_t b = EcuSerial.read();

    if (rxIndex == 0) {
      if (b == 0xA1 || b == 0xA2 || b == 0xA5) {
        rxBuffer[rxIndex++] = b;
      }
    } else {
      rxBuffer[rxIndex++] = b;

      if (rxIndex >= 50) {
        if ((rxBuffer[0] == 0xA1 || rxBuffer[0] == 0xA2) && ((rxBuffer[0] + rxBuffer[rxIndex - 1]) == 255)) {
          int realRpm = (rxBuffer[6] << 8) | rxBuffer[7];
          if (realRpm >= 0 && realRpm <= 16000) {
            telemetri.rpm = realRpm;
          }

          telemetri.tps = rxBuffer[1];
          telemetri.engineTemp = rxBuffer[2];

          int rawAfr = (rxBuffer[12] << 8) | rxBuffer[13];
          if (rawAfr > 0) {
            telemetri.afr = (float)rawAfr / 100.0;
          }

          if (rxBuffer[41] >= 1 && rxBuffer[41] <= 4) {
            telemetri.activeMap = rxBuffer[41];
          }

          int rawVolt = (rxBuffer[42] << 8) | rxBuffer[43];
          if (rawVolt > 0) {
            telemetri.batteryVolt = (float)rawVolt / 100.0;
          }

          telemetri.ecuConnected = true;
          lastPacketReceived = millis();
        } 
        else if (rxBuffer[0] == 0xA5 && rxIndex >= 6 && rxBuffer[1] == 0x02) {
          telemetri.rpm = (rxBuffer[2] << 8) | rxBuffer[3];
          telemetri.afr = (float)rxBuffer[4] / 10.0;
          telemetri.engineTemp = rxBuffer[5];
          telemetri.ecuConnected = true;
          lastPacketReceived = millis();
        }

        rxIndex = 0;
      }

      if (rxIndex >= sizeof(rxBuffer)) {
        rxIndex = 0;
      }
    }
  }

  if (millis() - lastPacketReceived > 2000) {
    telemetri.ecuConnected = false;
    telemetri.rpm = 0;
    telemetri.tps = 0;
  }
}

void pollEcuTelemetry() {
  if (millis() - lastPollTime > 100) {
    lastPollTime = millis();
    byte pollFrame[] = { 0x69, 0x00, 0x96, 0x69, 0x05 };
    EcuSerial.write(pollFrame, sizeof(pollFrame));
  }
}

void setup() {
  Serial.begin(115200);
  EcuSerial.begin(ECU_BAUD, SERIAL_8N1, ECU_RX_PIN, ECU_TX_PIN);
  pinMode(PHYSICAL_BUTTON_PIN, INPUT_PULLUP);
  SerialBT.begin("RacePanel-RedLeo");
  Serial.println("[OK] Bluetooth: RacePanel-RedLeo | UART: 38400 Baud | Zero Dummy Data");
}

void loop() {
  handleBluetoothIncoming();
  pollEcuTelemetry();
  readEcuStream();

  if (digitalRead(PHYSICAL_BUTTON_PIN) == LOW && (millis() - lastButtonPress > 400)) {
    lastButtonPress = millis();
    int nextMap = (telemetri.activeMap % 4) + 1;
    sendMapCommandToEcu(nextMap);
  }

  if (millis() - lastBroadcast > 50) {
    lastBroadcast = millis();
    broadcastBluetoothTelemetry();
  }
}
