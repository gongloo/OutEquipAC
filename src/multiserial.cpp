#include "multiserial.h"

size_t MultiSerial::write(const uint8_t* buffer, size_t size) {
  if (web_serial_enabled_) {
    WebSerial.write(buffer, size);
  }
  return Serial.write(buffer, size);
}

size_t MultiSerial::write(uint8_t data) {
  if (web_serial_enabled_) {
    WebSerial.write(data);
  }
  return Serial.write(data);
}

int MultiSerial::availableForWrite() {
  if (web_serial_enabled_) {
    return WebSerial.availableForWrite();
  }
  return Serial.availableForWrite();
}

void MultiSerial::flush() {
  if (web_serial_enabled_) {
    WebSerial.flush();
  }
  Serial.flush();
}