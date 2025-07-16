#include <Arduino.h>

#include "ac_framer.h"

#define AC_TX_PIN 16
#define AC_RX_PIN 18
#define AC_BAUD_RATE 115200

HardwareSerial acSerial(2);
HardwareSerial debugSerial(1);

ACFramer rxFramer;
ACFramer txFramer;

void DumpFailedFrame(const ACFramer& framer) {
  Serial.print("(Failed frame: 0x");
  for (int i = 0; i < framer.buffer_pos(); i++) {
    Serial.printf(" %02x", framer.buffer()[i]);
  }
  Serial.printf(", %d)\n", framer.buffer_pos());
}

void DumpHexAndAscii(const uint8_t c) {
  Serial.printf("0x%02x", c);
  switch (c) {
    case '\r':
      Serial.print(" (\\r)");
      break;
    case '\n':
      Serial.print(" (\\n)");
      break;
    default:
      Serial.printf(" (%c)", c);;
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.printf("Starting AC serial communication at %d baud...\n",
                AC_BAUD_RATE);
  acSerial.setRxBufferSize(1024);
  acSerial.begin(AC_BAUD_RATE, SERIAL_8N1, AC_RX_PIN, -1);
  debugSerial.setRxBufferSize(1024);
  debugSerial.begin(AC_BAUD_RATE, SERIAL_8N1, AC_TX_PIN, -1);
}

void loop() {
  while (acSerial.available()) {
    uint8_t c = acSerial.read();

    if (!rxFramer.FrameData(c)) {
      Serial.print("Rx: ");
      if (rxFramer.buffer_pos() > 0) {
        DumpFailedFrame(rxFramer);
      } else {
        DumpHexAndAscii(c);
      }
      Serial.println();
      rxFramer.Reset();
    } else if (rxFramer.HasFullFrame()) {
      auto key = rxFramer.GetKey();
      auto value = rxFramer.GetValue();
      rxFramer.Reset();
      switch (key) {
        case ACFramer::Key::Amperage:
        case ACFramer::Key::Voltage:
        case ACFramer::Key::IntakeAirTemp:
        case ACFramer::Key::OutletAirTemp:
          // Skip.
          break;
        default:
          Serial.printf("Rx: %s: %d\n", ACFramer::KeyToString(key), value);
      }
    }
  }
  while (debugSerial.available()) {
    uint8_t c = debugSerial.read();

    if (!txFramer.FrameData(c)) {
      Serial.print("\t\t\t\t\tTx: ");
      if (txFramer.buffer_pos() > 0) {
        DumpFailedFrame(txFramer);
      } else {
        DumpHexAndAscii(c);
      }
      Serial.println();
      txFramer.Reset();
    } else if (txFramer.HasFullFrame()) {
      auto key = txFramer.GetKey();
      auto value = txFramer.GetValue();
      txFramer.Reset();
      switch (key) {
        case ACFramer::Key::Amperage:
        case ACFramer::Key::Voltage:
        case ACFramer::Key::IntakeAirTemp:
        case ACFramer::Key::OutletAirTemp:
          // Skip.
          break;
        default:
          Serial.printf("\t\t\t\t\tTx: %s: %d\n", ACFramer::KeyToString(key),
                        value);
      }
    }
  }
  delay(10);
}