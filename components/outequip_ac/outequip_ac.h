#pragma once

#include "ac_framer.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include <optional>
#include <queue>

namespace esphome {
namespace outequip_ac {

class OutEquipAC;

enum OutEquipACSwitchType { LCD, SWING, LIGHT };

class OutEquipACSwitch : public switch_::Switch, public Component {
public:
  void set_parent(OutEquipAC *parent) { parent_ = parent; }
  void set_type(OutEquipACSwitchType type) { type_ = type; }
  void write_state(bool state) override;
  void setup() override;

protected:
  OutEquipAC *parent_{nullptr};
  OutEquipACSwitchType type_;
};

class OutEquipAC : public Component,
                   public climate::Climate,
                   public uart::UARTDevice {
public:
  OutEquipAC() = default;

  void set_intake_temp_sensor(sensor::Sensor *sensor) {
    intake_temp_sensor_ = sensor;
  }
  void set_outlet_temp_sensor(sensor::Sensor *sensor) {
    outlet_temp_sensor_ = sensor;
  }
  void set_voltage_sensor(sensor::Sensor *sensor) { voltage_sensor_ = sensor; }
  void set_undervolt_sensor(sensor::Sensor *sensor) {
    undervolt_sensor_ = sensor;
  }
  void set_overvolt_sensor(sensor::Sensor *sensor) {
    overvolt_sensor_ = sensor;
  }
  void set_amperage_sensor(sensor::Sensor *sensor) {
    amperage_sensor_ = sensor;
  }
  void set_lcd_switch(switch_::Switch *lcd_switch) { lcd_switch_ = lcd_switch; }
  void set_swing_switch(switch_::Switch *swing_switch) {
    swing_switch_ = swing_switch;
  }
  void set_light_switch(switch_::Switch *light_switch) {
    light_switch_ = light_switch;
  }

  void set_lcd_state(bool state);
  void set_swing_state(bool state);
  void set_light_state(bool state);

  void setup() override;
  void loop() override;
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  ACFramer::OnOffValue power_state() const { return cur_power_state_; }
  ACFramer::ModeValue cur_mode() const { return cur_mode_; }
  uint16_t fan_speed() const { return cur_fan_speed_; }
  uint32_t num_frames_tx() const { return num_frames_tx_; }
  uint32_t num_frames_rx() const { return num_frames_rx_; }
  uint32_t num_frames_failed() const { return num_frames_failed_; }
  uint32_t num_spurious_bytes_rx() const { return num_spurious_bytes_rx_; }

protected:
  sensor::Sensor *intake_temp_sensor_{nullptr};
  sensor::Sensor *outlet_temp_sensor_{nullptr};
  sensor::Sensor *voltage_sensor_{nullptr};
  sensor::Sensor *undervolt_sensor_{nullptr};
  sensor::Sensor *overvolt_sensor_{nullptr};
  sensor::Sensor *amperage_sensor_{nullptr};
  switch_::Switch *lcd_switch_{nullptr};
  switch_::Switch *swing_switch_{nullptr};
  switch_::Switch *light_switch_{nullptr};

private:
  void WriteFrame(ACFramer &framer);
  void MaybeSendCurFrame();
  bool EnqueueFrame(ACFramer::Key key, uint16_t value);

  constexpr static ACFramer::Key kQueryKeys[] = {
      ACFramer::Key::Power,
      ACFramer::Key::Mode,
      ACFramer::Key::SetTemperature,
      ACFramer::Key::FanSpeed,
      ACFramer::Key::UndervoltProtect,
      ACFramer::Key::OvervoltProtect,
      ACFramer::Key::IntakeAirTemp,
      ACFramer::Key::OutletAirTemp,
      // Summit2 firmware has light/lcd status reporting is buggy. Ignore.
      ACFramer::Key::LCD,
      // ACFramer::Key::Light,
      ACFramer::Key::Swing,
      ACFramer::Key::Voltage,
      ACFramer::Key::Amperage,
  };

  size_t cur_query_key_idx = 0;
  uint32_t last_frame_sent = 0;
  uint32_t last_full_status = 0;
  std::queue<ACFramer> txQueue;
  std::optional<ACFramer::Key> expecting_key;
  ACFramer rxFramer;

  ACFramer::OnOffValue cur_power_state_ = ACFramer::OnOffValue::Query;
  ACFramer::ModeValue cur_mode_ = ACFramer::ModeValue::Query;
  uint16_t cur_fan_speed_{0};
  uint32_t num_frames_tx_{0};
  uint32_t num_frames_rx_{0};
  uint32_t num_frames_failed_{0};
  uint32_t num_spurious_bytes_rx_{0};
};

} // namespace outequip_ac
} // namespace esphome
