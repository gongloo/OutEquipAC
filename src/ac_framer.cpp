#include "ac_framer.h"

#include <cassert>
#include <cstring>

namespace {

enum FrameBytePos { Length = 2, DeviceType = 3, Key = 4, Value = 5 };

constexpr uint8_t kPreamble[] = {0x5a, 0x5a};
constexpr uint8_t kPostamble[] = {0x0d, 0x0a};

}  // namespace

// cppcheck-suppress unusedFunction
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
    case Key::LCD:
    case Key::Swing:
      return OnOffValueToString(static_cast<OnOffValue>(GetValue()));
    case Key::Mode:
      return ModeValueToString(static_cast<ModeValue>(GetValue()));
    case Key::Light:
      return LightValueToString(static_cast<LightValue>(GetValue()));
    case Key::FanSpeed:
    case Key::SetTemperature:
    case Key::OvervoltProtect:
    case Key::IntakeAirTemp:
    case Key::OutletAirTemp:
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
  bool long_value = value > UINT8_MAX;
  buffer_[buffer_pos_++] = sizeof(kPostamble) +
                           3 /* device type, key, checksum */ +
                           (long_value ? 2 : 1);

  // Device Type
  buffer_[buffer_pos_++] = 1;

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
  return (buffer_pos_ == GetLength() + sizeof(kPreamble) + 1);
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

  // Validate key.
  if (!ValidateKey(buffer_[FrameBytePos::Key])) {
    return false;  // Invalid key
  }

  // Validate value.
  switch (GetKey()) {
    case Key::Power:
    case Key::LCD:
    case Key::Swing:
    case Key::Light:
      switch (static_cast<OnOffValue>(GetValue())) {
        case OnOffValue::Query:
        case OnOffValue::Off:
        case OnOffValue::On:
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
    case Key::SetTemperature:
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