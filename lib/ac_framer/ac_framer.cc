#include "ac_framer.h"

#include <cstring>

#define FRAME_LENGTH_BYTE 2
#define FRAME_KEY_BYTE 4
#define FRAME_VALUE_BYTE 5

const uint8_t kPreamble[2] = {0x5a, 0x5a};
const uint8_t kPostamble[2] = {0x0d, 0x0a};

bool ACFramer::FrameData(const uint8_t data) {
  // Check if we have space in the buffer
  if (buffer_pos_ >= sizeof(buffer_)) {
    return false;  // Buffer overflow
  }

  // Check if we have a full frame already
  if (HasFullFrame()) {
    return false;
  }

  // Check for preamble
  if (buffer_pos_ < sizeof(kPreamble) && data != kPreamble[buffer_pos_]) {
    return false;  // Invalid start of frame
  }

  // Add the data to the buffer
  buffer_[buffer_pos_++] = data;

  // Validate the frame if we have enough data
  if (HasFullFrame()) {
    return ValidateFrame();
  }

  return true;
}

void ACFramer::Reset() { buffer_pos_ = 0; }

ACFramer::Key ACFramer::GetKey() const {
  return static_cast<Key>(buffer_[FRAME_KEY_BYTE]);
}

uint16_t ACFramer::GetValue() const {
  switch (GetValueLength()) {
    case 1:
      return buffer_[FRAME_VALUE_BYTE];
    case 2:
      return (buffer_[FRAME_VALUE_BYTE] << 8) | buffer_[FRAME_VALUE_BYTE + 1];
  }
  return 0;
}

void ACFramer::NewFrame(Key key, uint16_t value) {
  Reset();

  // Preamble.
  assert(buffer_pos_ == 0);
  memcpy(buffer_, kPreamble, sizeof(kPreamble));
  buffer_pos_ += sizeof(kPreamble);

  // Length
  assert(buffer_pos_ == FRAME_LENGTH_BYTE);
  bool long_value = value > UINT8_MAX;
  buffer_[buffer_pos_++] = sizeof(kPostamble) + 3 /* unknown, key, checksum */ +
                           (long_value ? 2 : 1);

  // Unknown.
  buffer_[buffer_pos_++] = 1;

  // Key
  assert(buffer_pos_ == FRAME_KEY_BYTE);
  buffer_[buffer_pos_++] = static_cast<uint8_t>(key);

  // Value
  assert(buffer_pos_ == FRAME_VALUE_BYTE);
  if (long_value) {
    buffer_[buffer_pos_++] = static_cast<uint8_t>(value >> 8);
    buffer_[buffer_pos_++] = static_cast<uint8_t>(value);
  } else {
    buffer_[buffer_pos_++] = static_cast<uint8_t>(value);
  }

  // Checksum
  uint8_t checksum = 0;
  for (size_t i = 0; i < buffer_pos_; ++i) {
    checksum += buffer_[i];
  }
  buffer_[buffer_pos_++] = checksum;

  // Postamble
  memcpy(buffer_ + buffer_pos_, kPostamble, sizeof(kPostamble));
  buffer_pos_ += sizeof(kPostamble);

  // assert(ValidateFrame());
}

uint8_t ACFramer::GetLength() const {
  if (buffer_pos_ <= FRAME_LENGTH_BYTE) {
    return 0;
  }
  return buffer_[FRAME_LENGTH_BYTE];
}

uint8_t ACFramer::GetValueLength() const {
  if (!HasFullFrame()) {
    return 0;
  }
  return GetLength() - 3 /* unknown, key, checksum */ - sizeof(kPostamble);
}

bool ACFramer::HasFullFrame() const {
  static_assert(FRAME_LENGTH_BYTE < sizeof(kPreamble) + 1);
  return (buffer_pos_ == GetLength() + sizeof(kPreamble) + 1);
}

bool ACFramer::ValidateFrame() const {
  // Check if we have a full frame
  if (!HasFullFrame()) {
    return false;  // No full frame to validate
  }

  // No need to validate preamble as it is already checked in FrameData

  // Validate key.
  if (!ValidateKey()) {
    return false;  // Invalid key
  }

  // Validate checksum.
  uint8_t checksum = 0;
  for (size_t i = 0; i < buffer_pos_ - sizeof(kPostamble) - 1; ++i) {
    checksum += buffer_[i];
  }
  if (checksum != buffer_[buffer_pos_ - sizeof(kPostamble) - 1]) {
    return false;  // Invalid checksum
  }

  // Check if the frame ends with the postamble
  if (buffer_pos_ < sizeof(kPostamble) ||
      memcmp(buffer_ + buffer_pos_ - sizeof(kPostamble), kPostamble,
             sizeof(kPostamble)) != 0) {
    return false;  // Invalid postamble
  }

  return true;  // Frame is valid
}

bool ACFramer::ValidateKey() const {
  Key k = static_cast<Key>(buffer_[FRAME_KEY_BYTE]);
  switch (k) {
    case Key::Power:
    case Key::Mode:
    case Key::SetTemperature:
    case Key::FanSpeed:
    case Key::UndervoltProtect:
    case Key::OvervoltProtect:
    case Key::IntakeAirTemp:
    case Key::OutletAirTemp:
    case Key::LCD:
    case Key::Swing:
    case Key::Voltage:
    case Key::Amperage:
    case Key::Light:
    case Key::Active:
      return true;
  }
  return false;
}
ACFramer::ACFramer() { Reset(); }