#ifndef __MULTISERIAL_H__
#define __MULTISERIAL_H__

#include <Arduino.h>
#include <WebSerial.h>

class MultiSerial : public Print {
 public:
  MultiSerial() : Print() {}

  size_t write(uint8_t data) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  int availableForWrite() override;
  void flush() override;

  void enable_web_serial(bool enable = true) { web_serial_enabled_ = enable; }

 private:
  bool web_serial_enabled_ = false;
};

#endif // __MULTISERIAL_H__