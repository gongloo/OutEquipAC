#include "ac_framer.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <stdint.h>

namespace {

enum FrameBytePos { Length = 2, DeviceType = 3, Key = 4, Value = 5 };

constexpr uint8_t kPreamble[] = {0x5a, 0x5a};
constexpr uint8_t kPostamble[] = {0x0d, 0x0a};
constexpr uint8_t kDeviceTypeAc = 0x01;

}  // namespace

void ACFramer::Resync() {
  // Look for any suffix starting at offset >= 1 that begins with preamble bytes.
  // We want to preserve the longest matching preamble prefix [0x5a, 0x5a] or [0x5a].
  for (size_t start = 1; start < buffer_pos_; ++start) {
    size_t len = buffer_pos_ - start;
    if (len >= 2 && buffer_[start] == kPreamble[0] && buffer_[start + 1] == kPreamble[1]) {
      // Shift remaining bytes starting with the full 2-byte preamble to the front
      memmove(buffer_, buffer_ + start, len);
      buffer_pos_ = len;
      return;
    }
    if (len == 1 && buffer_[start] == kPreamble[0]) {
      // Shift single trailing 0x5a to front
      buffer_[0] = kPreamble[0];
      buffer_pos_ = 1;
      return;
    }
  }
  buffer_pos_ = 0;
}

// cppcheck-suppress unusedFunction
bool ACFramer::FrameData(const uint8_t data) {
  // If we already have a completed full frame, reset before starting the next frame
  if (HasFullFrame()) {
    Reset();
  }

  // Check if we have space in the buffer
  if (buffer_pos_ >= sizeof(buffer_)) {
    Resync();
    if (buffer_pos_ >= sizeof(buffer_)) {
      Reset();
      return false;
    }
  }

  // Check for preamble
  if (buffer_pos_ < sizeof(kPreamble)) {
    if (data != kPreamble[buffer_pos_]) {
      // If we received 0x5a when expecting the second 0x5a (i.e. buffer_pos_ == 1),
      // we can stay at buffer_pos_ = 1 (treating this byte as the new first 0x5a).
      if (data == kPreamble[0]) {
        buffer_pos_ = 1;
        buffer_[0] = data;
      } else {
        buffer_pos_ = 0;
      }
      return false;
    }
    buffer_[buffer_pos_++] = data;
    return true;
  }

  // Early header validation for byte positions: Length (2), DeviceType (3), Key (4)
  if (buffer_pos_ == FrameBytePos::Length) {
    // Valid lengths in this protocol: 6 (1-byte value) or 7 (2-byte value)
    if (data != 6 && data != 7) {
      buffer_[buffer_pos_++] = data;
      Resync();
      return false;
    }
  } else if (buffer_pos_ == FrameBytePos::DeviceType) {
    if (data != kDeviceTypeAc) {
      buffer_[buffer_pos_++] = data;
      Resync();
      return false;
    }
  } else if (buffer_pos_ == FrameBytePos::Key) {
    if (!ValidateKey(data)) {
      buffer_[buffer_pos_++] = data;
      Resync();
      return false;
    }
  }

  // Add the data to the buffer
  buffer_[buffer_pos_++] = data;

  // Validate the frame if we have enough data
  if (HasFullFrame()) {
    if (!ValidateFrame()) {
      Resync();
      return false;
    }
  }

  return true;
}

void ACFramer::Reset() {
  memset(buffer_, 0, sizeof(buffer_));
  memset(val_str_, 0, sizeof(val_str_));
  buffer_pos_ = 0;
}

ACFramer::Key ACFramer::GetKey() const {
  return static_cast<Key>(buffer_[FrameBytePos::Key]);
}

const char* ACFramer::GetKeyAsString() const { return KeyToString(GetKey()); }

uint16_t ACFramer::GetValue() const {
  switch (GetValueLength()) {
    case 1:
      return buffer_[FrameBytePos::Value];
    case 2:
      return (buffer_[FrameBytePos::Value] << 8) |
             buffer_[FrameBytePos::Value + 1];
  }
  return 0;
}

const char* ACFramer::GetValueAsString() {
  switch (GetKey()) {
    case Key::Power:
    case Key::Swing:
      return OnOffValueToString(static_cast<OnOffValue>(GetValue()));
    case Key::LCD:
      // In the serial protocol for LCD, 0 is On and 1 is Off
      if (GetValue() == 0 || GetValue() == 2) {
        return "on";
      } else if (GetValue() == 1) {
        return "off";
      }
      return "query";
    case Key::Mode:
      return ModeValueToString(static_cast<ModeValue>(GetValue()));
    case Key::Light:
      return LightValueToString(static_cast<LightValue>(GetValue()));
    case Key::IntakeAirTemp:
    case Key::OutletAirTemp:
      snprintf(val_str_, sizeof(val_str_), "%d",
               static_cast<int8_t>(GetValue() & 0xFF));
      return val_str_;
    case Key::FanSpeed:
    case Key::SetTemperature:
    case Key::OvervoltProtect:
    case Key::Active:
      snprintf(val_str_, sizeof(val_str_), "%d", GetValue());
      return val_str_;
    case Key::UndervoltProtect:
    case Key::Voltage:
    case Key::Amperage:
      snprintf(val_str_, sizeof(val_str_), "%.1f", GetValue() / 10.0);
      return val_str_;
  }
  return "invalid";
}

// cppcheck-suppress unusedFunction
bool ACFramer::NewFrame(Key key, uint16_t value, bool allow_invalid) {
  Reset();

  // Preamble.
  assert(buffer_pos_ == 0);
  memcpy(buffer_, kPreamble, sizeof(kPreamble));
  buffer_pos_ += sizeof(kPreamble);

  // Length
  assert(buffer_pos_ == FrameBytePos::Length);
  bool long_value = (key == Key::Voltage || key == Key::Amperage) && value > 0;
  buffer_[buffer_pos_++] = sizeof(kPostamble) +
                           3 /* device type, key, checksum */ +
                           (long_value ? 2 : 1);

  // Device Type
  buffer_[buffer_pos_++] = kDeviceTypeAc;

  // Key
  assert(buffer_pos_ == FrameBytePos::Key);
  buffer_[buffer_pos_++] = static_cast<uint8_t>(key);

  // Value
  assert(buffer_pos_ == FrameBytePos::Value);
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

  return allow_invalid ? true : ValidateFrame();
}

uint8_t ACFramer::GetLength() const {
  if (buffer_pos_ <= FrameBytePos::Length) {
    return 0;
  }
  return buffer_[FrameBytePos::Length];
}

uint8_t ACFramer::GetValueLength() const {
  if (!HasFullFrame()) {
    return 0;
  }
  return GetLength() - 3 /* device type, key, checksum */ - sizeof(kPostamble);
}

bool ACFramer::HasFullFrame() const {
  static_assert(FrameBytePos::Length < sizeof(kPreamble) + 1);
  const uint8_t len = GetLength();
  return (len >= 6 && buffer_pos_ == len + sizeof(kPreamble) + 1);
}

bool ACFramer::ValidateFrame() const {
  // Check if we have a full frame
  if (!HasFullFrame()) {
    return false;  // No full frame to validate
  }

  // Validate preamble.
  if (memcmp(buffer_, kPreamble, sizeof(kPreamble)) != 0) {
    return false;  // Invalid preamble
  }

  // Validate length byte: must be 6 or 7.
  const uint8_t len = GetLength();
  if (len != 6 && len != 7) {
    return false;
  }

  // Validate device type.
  if (buffer_[FrameBytePos::DeviceType] != kDeviceTypeAc) {
    return false;
  }

  // Validate key.
  if (!ValidateKey(buffer_[FrameBytePos::Key])) {
    return false;  // Invalid key
  }

  // Validate value length matches key expectations.
  const uint8_t val_len = GetValueLength();
  switch (GetKey()) {
    case Key::Voltage:
    case Key::Amperage:
      // Voltage and Amperage state responses are 16-bit (2 bytes), but query packets can be 1 byte.
      if (val_len != 1 && val_len != 2) {
        return false;
      }
      break;
    default:
      if (val_len != 1) {
        return false;
      }
      break;
  }

  // Validate value.
  switch (GetKey()) {
    case Key::Power:
    case Key::Swing:
      switch (static_cast<OnOffValue>(GetValue())) {
        case OnOffValue::Query:
        case OnOffValue::Off:
        case OnOffValue::On:
          break;
        default:
          return false;
      }
      break;
    case Key::LCD:
      if (GetValue() > 2) {
        return false;
      }
      break;
    case Key::Light:
      switch (static_cast<LightValue>(GetValue())) {
        case LightValue::Query:
        case LightValue::On:
        case LightValue::Off:
          break;
        default:
          return false;
      }
      break;
    case Key::Mode:
      switch (static_cast<ModeValue>(GetValue())) {
        case ModeValue::Query:
        case ModeValue::Cool:
        case ModeValue::Heat:
        case ModeValue::Fan:
        case ModeValue::Eco:
        case ModeValue::Sleep:
        case ModeValue::Turbo:
        case ModeValue::Wet:
          break;
        default:
          return false;
      }
      break;
    case Key::FanSpeed:
      if (GetValue() > 5) {
        return false;
      }
      break;
    case Key::SetTemperature: {
      const uint16_t v = GetValue();
      const bool valid_c = (v >= 16 && v <= 30);
      const bool valid_f = (v >= 60 && v <= 86);
      if (v != kQueryVal && !valid_c && !valid_f) {
        return false;
      }
      break;
    }
    case Key::UndervoltProtect:
    case Key::OvervoltProtect:
    case Key::IntakeAirTemp:
    case Key::OutletAirTemp:
    case Key::Voltage:
    case Key::Amperage:
    case Key::Active:
      // Allow any value.
      break;
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

bool ACFramer::ValidateKey(uint8_t data) {
  Key k = static_cast<Key>(data);
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