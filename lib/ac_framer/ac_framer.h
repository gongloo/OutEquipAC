#ifndef __AC_FRAMER_H__
#define __AC_FRAMER_H__

#include <cstdint>

class ACFramer {
  public:
    static const uint8_t kMaxFrameSize = 10;  // Maximum value length of 2.

    enum class Key {
      Power = 0x01,
      Mode = 0x02,
      SetTemperature = 0x03,
      FanSpeed = 0x04,
      UndervoltProtect = 0x05,
      OvervoltProtect = 0x06,
      IntakeAirTemp = 0x07,
      OutletAirTemp = 0x08,
      LCD = 0x0a,
      Swing = 0x10,
      Voltage = 0x12,
      Amperage = 0x13,
      Light = 0x1c,
      Active = 0x42
    };

    enum class OnOffValue {
      Query = 0x00,
      On = 0x01,
      Off = 0x02
    };

    enum class ModeValue {
      Query = 0x00,
      Cool = 0x01,
      Heat = 0x02,
      Fan = 0x03,
      Eco = 0x04,
      Sleep = 0x05,
      Turbo = 0x06,
      Wet = 0x07
    };

    static constexpr const char* KeyToString(Key k) {
      switch (k) {
        case Key::Power: return "Power";
        case Key::Mode: return "Mode";
        case Key::SetTemperature: return "Set Temperature";
        case Key::FanSpeed: return "Fan Speed";
        case Key::UndervoltProtect: return "Undervolt Protection";
        case Key::OvervoltProtect: return "Overvolt Protection";
        case Key::IntakeAirTemp: return "Intake Air Temperature";
        case Key::OutletAirTemp: return "Outlet Air Temperature";
        case Key::LCD: return "LCD";
        case Key::Swing: return "Swing";
        case Key::Voltage: return "Voltage";
        case Key::Amperage: return "Amperage";
        case Key::Light: return "Light";
        case Key::Active: return "Active";
      }
      return "Invalid Key";
    }

    static constexpr const char* OnOffValueToString(OnOffValue v) {
      switch (v) {
        case OnOffValue::Query: return "Query";
        case OnOffValue::On: return "On";
        case OnOffValue::Off: return "Off";
      }
    }

    static constexpr const char* ModeValueToString(ModeValue v) {
      switch (v) {
        case ModeValue::Query: return "Query";
        case ModeValue::Cool: return "Cool";
        case ModeValue::Heat: return "Heat";
        case ModeValue::Fan: return "Fan";
        case ModeValue::Eco: return "Eco";
        case ModeValue::Sleep: return "Sleep";
        case ModeValue::Turbo: return "Turbo";
        case ModeValue::Wet: return "Wet";
      }
    }

    ACFramer();
    ~ACFramer() {}

    /**
     * @brief Frames the incoming data.
     *
     * @param data Byte of data to be framed.
     * @return true if the data was successfully framed, false otherwise.
     */
    bool FrameData(const uint8_t data);
    void Reset();

    bool HasFullFrame() const;
    Key GetKey() const;
    uint16_t GetValue() const;

    const uint8_t* buffer() const { return buffer_; }
    uint8_t buffer_pos() const { return buffer_pos_; }

  private:
    uint8_t GetLength() const;
    uint8_t GetValueLength() const;
    bool ValidateFrame() const;
    bool ValidateKey() const;

    uint8_t buffer_[kMaxFrameSize];
    uint8_t buffer_pos_;
    bool has_full_frame_;
};

#endif // __AC_FRAMER_H__