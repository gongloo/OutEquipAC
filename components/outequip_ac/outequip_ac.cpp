#include "outequip_ac.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include <cmath>

namespace esphome {
namespace outequip_ac {

void OutEquipACSwitch::write_state(bool state) {
  if (parent_ != nullptr) {
    switch (type_) {
    case LCD:
      parent_->set_lcd_state(state);
      break;
    case SWING:
      parent_->set_swing_state(state);
      break;
    case LIGHT:
      parent_->set_light_state(state);
      break;
    }
  }
}

void OutEquipACSwitch::setup() {
  auto restored = this->get_initial_state_with_restore_mode();
  if (restored.has_value()) {
    if (restored.value()) {
      this->turn_on();
    } else {
      this->turn_off();
    }
  }
}

void OutEquipAC::set_lcd_state(bool state) {
  ESP_LOGD("outequip_ac", "Setting LCD state to %s", state ? "ON" : "OFF");
  EnqueueFrame(ACFramer::Key::LCD,
               state ? static_cast<uint16_t>(ACFramer::OnOffValue::On)
                     : static_cast<uint16_t>(ACFramer::OnOffValue::Off),
               true /* query_after */);
}

void OutEquipAC::set_swing_state(bool state) {
  ESP_LOGD("outequip_ac", "Setting Swing state to %s", state ? "ON" : "OFF");
  EnqueueFrame(ACFramer::Key::Swing,
               state ? static_cast<uint16_t>(ACFramer::OnOffValue::On)
                     : static_cast<uint16_t>(ACFramer::OnOffValue::Off),
               true /* query_after */);
}

void OutEquipAC::set_light_state(bool state) {
  ESP_LOGD("outequip_ac", "Setting Light state to %s", state ? "ON" : "OFF");
  // Summit2 control board status reporting for Light is buggy, so do not query_after.
  // We maintain light state optimistically.
  EnqueueFrame(ACFramer::Key::Light,
               state ? static_cast<uint16_t>(ACFramer::LightValue::On)
                     : static_cast<uint16_t>(ACFramer::LightValue::Off),
               false /* query_after */);
  if (light_switch_ != nullptr) {
    light_switch_->publish_state(state);
  }
}

void OutEquipAC::setup() {
  EnqueueFrame(ACFramer::Key::Active, 0);
  last_frame_sent = millis();
}

void OutEquipAC::loop() {
  uint32_t now = millis();
  if (awaiting_response_) {
    // Timeout waiting for response: retry / proceed after 1000ms
    if (now - last_frame_sent >= 1000) {
      ESP_LOGW("outequip_ac", "Timeout waiting for UART response");
      awaiting_response_ = false;
      expecting_key.reset();
      if (in_flight_is_query_) {
        if (++cur_query_key_idx >= sizeof(kQueryKeys) / sizeof(*kQueryKeys)) {
          cur_query_key_idx = 0;
          last_full_status = now;
        }
      }
      MaybeSendCurFrame();
    }
  } else if (!txQueue.empty() || now - last_full_status >= 1000) {
    MaybeSendCurFrame();
  }

  auto publish_sensor = [](sensor::Sensor *sensor, float value) {
    if (sensor != nullptr && (!sensor->has_state() || sensor->state != value)) {
      ESP_LOGD("outequip_ac", "Publishing sensor state for '%s': %.1f",
               sensor->get_name().c_str(), value);
      sensor->publish_state(value);
    }
  };

  auto publish_switch = [](switch_::Switch *sw, bool state) {
    if (sw != nullptr && (!sw->has_state() || sw->state != state)) {
      ESP_LOGD("outequip_ac", "Publishing switch state for '%s': %s",
               sw->get_name().c_str(), state ? "ON" : "OFF");
      sw->publish_state(state);
    }
  };

  while (this->available()) {
    uint8_t c = this->read();
    if (!rxFramer.FrameData(c)) {
      if (rxFramer.buffer_pos() > 0) {
        num_frames_failed_++;
      } else {
        num_spurious_bytes_rx_++;
      }
      // Do not call rxFramer.Reset() here: ACFramer::Resync() manages and preserves
      // any valid preamble prefix across errors to avoid dropping subsequent frames.
    } else if (rxFramer.HasFullFrame()) {
      num_frames_rx_++;
      const auto key = rxFramer.GetKey();
      const auto value = rxFramer.GetValue();

      bool climate_changed = false;

      switch (key) {
      case ACFramer::Key::Power: {
        const auto old_power_state = cur_power_state_;
        cur_power_state_ = static_cast<ACFramer::OnOffValue>(value);
        if (lcd_switch_ != nullptr) {
          if (cur_power_state_ == ACFramer::OnOffValue::Off) {
            publish_switch(lcd_switch_, false);
          } else if (old_power_state == ACFramer::OnOffValue::Off &&
                     cur_power_state_ == ACFramer::OnOffValue::On) {
            publish_switch(lcd_switch_, true);
          }
        }
        break;
      }
      case ACFramer::Key::Mode:
        cur_mode_ = static_cast<ACFramer::ModeValue>(value);
        break;
      case ACFramer::Key::SetTemperature: {
        float new_target;
        if (value >= 16 && value <= 30) {
          // Board is in Celsius mode (16-30 C)
          board_uses_fahrenheit_ = false;
          new_target = static_cast<float>(value);
        } else if (value >= 60 && value <= 86) {
          // Board is in Fahrenheit mode (60-86 F)
          board_uses_fahrenheit_ = true;
          new_target = (value - 32.0f) * 5.0f / 9.0f;
        } else {
          // Ignore invalid or query-reflection 0 values
          break;
        }
        if (this->target_temperature != new_target) {
          ESP_LOGD("outequip_ac", "Climate target temp changed to %.1f C (raw=%d, fahrenheit=%d)",
                   new_target, value, board_uses_fahrenheit_);
          this->target_temperature = new_target;
          climate_changed = true;
        }
        break;
      }
      case ACFramer::Key::FanSpeed:
        cur_fan_speed_ = value;
        climate::ClimateFanMode new_fan_mode;
        if (value <= 1)
          new_fan_mode = climate::CLIMATE_FAN_LOW;
        else if (value <= 3)
          new_fan_mode = climate::CLIMATE_FAN_MEDIUM;
        else
          new_fan_mode = climate::CLIMATE_FAN_HIGH;
        if (!this->fan_mode.has_value() ||
            this->fan_mode.value() != new_fan_mode) {
          ESP_LOGD("outequip_ac", "Climate fan speed changed to %d",
                   static_cast<int>(new_fan_mode));
          this->fan_mode = new_fan_mode;
          climate_changed = true;
        }
        break;
      case ACFramer::Key::UndervoltProtect:
        publish_sensor(undervolt_sensor_, value / 10.0f);
        break;
      case ACFramer::Key::OvervoltProtect:
        publish_sensor(overvolt_sensor_, value);
        break;
      case ACFramer::Key::IntakeAirTemp: {
        int8_t intake_temp = static_cast<int8_t>(value & 0xFF);
        publish_sensor(intake_temp_sensor_, intake_temp);
        if (this->current_temperature != intake_temp) {
          ESP_LOGD("outequip_ac", "Climate current temp changed to %d C",
                   intake_temp);
          this->current_temperature = intake_temp;
          climate_changed = true;
        }
        break;
      }
      case ACFramer::Key::OutletAirTemp:
        publish_sensor(outlet_temp_sensor_, static_cast<int8_t>(value & 0xFF));
        break;
      case ACFramer::Key::Voltage:
        publish_sensor(voltage_sensor_, value / 10.0f);
        break;
      case ACFramer::Key::LCD:
        if (lcd_switch_ != nullptr &&
            cur_power_state_ == ACFramer::OnOffValue::On) {
          // A serial-interface reported value of 1 is off, 0 or 2 is on
          bool is_on = (value == 0 || value == 2);
          publish_switch(lcd_switch_, is_on);
        }
        break;
      case ACFramer::Key::Swing:
        if (swing_switch_ != nullptr) {
          bool is_on =
              (value == static_cast<uint16_t>(ACFramer::OnOffValue::On));
          publish_switch(swing_switch_, is_on);
        }
        break;
      case ACFramer::Key::Amperage:
        publish_sensor(amperage_sensor_, value);
        break;
      case ACFramer::Key::Light:
        // Ignore reading Light value over serial since the Summit2 firmware is
        // buggy.
        break;
      case ACFramer::Key::Active:
        if (value == 2)
          EnqueueFrame(ACFramer::Key::Active, 1);
        break;
      }

      // Handle Power/Mode combination for Climate
      if (key == ACFramer::Key::Power || key == ACFramer::Key::Mode) {
        climate::ClimateMode new_mode = climate::CLIMATE_MODE_OFF;
        climate::ClimatePreset new_preset = climate::CLIMATE_PRESET_NONE;

        if (cur_power_state_ == ACFramer::OnOffValue::On) {
          // If Mode hasn't been polled yet on boot, don't publish/overwrite mode until Mode frame arrives
          if (cur_mode_ == ACFramer::ModeValue::Query) {
            // Keep existing mode / do not publish transient OFF
            goto skip_mode_calc;
          }

          switch (cur_mode_) {
          case ACFramer::ModeValue::Cool:
            new_mode = climate::CLIMATE_MODE_COOL;
            new_preset = climate::CLIMATE_PRESET_NONE;
            break;
          case ACFramer::ModeValue::Eco:
            new_mode = climate::CLIMATE_MODE_COOL;
            new_preset = climate::CLIMATE_PRESET_ECO;
            break;
          case ACFramer::ModeValue::Sleep:
            new_mode = climate::CLIMATE_MODE_COOL;
            new_preset = climate::CLIMATE_PRESET_SLEEP;
            break;
          case ACFramer::ModeValue::Turbo:
            new_mode = climate::CLIMATE_MODE_COOL;
            new_preset = climate::CLIMATE_PRESET_BOOST;
            break;
          case ACFramer::ModeValue::Heat:
            new_mode = climate::CLIMATE_MODE_HEAT;
            new_preset = climate::CLIMATE_PRESET_NONE;
            break;
          case ACFramer::ModeValue::Fan:
            new_mode = climate::CLIMATE_MODE_FAN_ONLY;
            new_preset = climate::CLIMATE_PRESET_NONE;
            break;
          case ACFramer::ModeValue::Wet:
            new_mode = climate::CLIMATE_MODE_DRY;
            new_preset = climate::CLIMATE_PRESET_NONE;
            break;
          default:
            break;
          }
        }

        if (this->mode != new_mode) {
          ESP_LOGD("outequip_ac", "Climate mode changed to %d",
                   static_cast<int>(new_mode));
          this->mode = new_mode;
          climate_changed = true;
        }

        if (this->preset != new_preset) {
          ESP_LOGD("outequip_ac", "Climate preset changed to %d",
                   static_cast<int>(new_preset));
          this->preset = new_preset;
          climate_changed = true;
        }
      }

    skip_mode_calc:

      if (climate_changed) {
        this->publish_state();
      }

      // Check response expecting
      if (expecting_key.has_value() && *expecting_key == key) {
        expecting_key.reset();
        awaiting_response_ = false;
        if (in_flight_is_query_ && key == kQueryKeys[cur_query_key_idx]) {
          if (++cur_query_key_idx >= sizeof(kQueryKeys) / sizeof(*kQueryKeys)) {
            cur_query_key_idx = 0;
            last_full_status = millis();
          }
        }
      } else {
        // Any valid packet received clears the awaiting_response_ and expecting_key state
        awaiting_response_ = false;
        expecting_key.reset();
      }

      rxFramer.Reset();
      MaybeSendCurFrame();
    }
  }
}

climate::ClimateTraits OutEquipAC::traits() {
  auto traits = climate::ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_supported_modes(
      {climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_COOL,
       climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_FAN_ONLY,
       climate::CLIMATE_MODE_DRY});
  traits.set_supported_presets(
      {climate::CLIMATE_PRESET_NONE, climate::CLIMATE_PRESET_ECO,
       climate::CLIMATE_PRESET_SLEEP, climate::CLIMATE_PRESET_BOOST});
  traits.set_supported_fan_modes({climate::CLIMATE_FAN_LOW,
                                  climate::CLIMATE_FAN_MEDIUM,
                                  climate::CLIMATE_FAN_HIGH});
  return traits;
}

void OutEquipAC::control(const climate::ClimateCall &call) {
  // 1. Handle Climate Preset changes
  if (call.get_preset().has_value()) {
    auto preset = *call.get_preset();
    if (preset == climate::CLIMATE_PRESET_ECO) {
      if (cur_power_state_ != ACFramer::OnOffValue::On) {
        cur_mode_ = ACFramer::ModeValue::Query;
        EnqueueFrame(ACFramer::Key::Power, static_cast<uint16_t>(ACFramer::OnOffValue::On), true /* query_after */);
      }
      EnqueueFrame(ACFramer::Key::Mode, static_cast<uint16_t>(ACFramer::ModeValue::Eco), true /* query_after */);
    } else if (preset == climate::CLIMATE_PRESET_SLEEP) {
      if (cur_power_state_ != ACFramer::OnOffValue::On) {
        cur_mode_ = ACFramer::ModeValue::Query;
        EnqueueFrame(ACFramer::Key::Power, static_cast<uint16_t>(ACFramer::OnOffValue::On), true /* query_after */);
      }
      EnqueueFrame(ACFramer::Key::Mode, static_cast<uint16_t>(ACFramer::ModeValue::Sleep), true /* query_after */);
    } else if (preset == climate::CLIMATE_PRESET_BOOST) {
      if (cur_power_state_ != ACFramer::OnOffValue::On) {
        cur_mode_ = ACFramer::ModeValue::Query;
        EnqueueFrame(ACFramer::Key::Power, static_cast<uint16_t>(ACFramer::OnOffValue::On), true /* query_after */);
      }
      EnqueueFrame(ACFramer::Key::Mode, static_cast<uint16_t>(ACFramer::ModeValue::Turbo), true /* query_after */);
    } else if (preset == climate::CLIMATE_PRESET_NONE) {
      // If leaving a special preset while in cooling and no explicit mode is in this call, revert to standard Cool mode
      if (!call.get_mode().has_value() &&
          (cur_mode_ == ACFramer::ModeValue::Eco ||
           cur_mode_ == ACFramer::ModeValue::Sleep ||
           cur_mode_ == ACFramer::ModeValue::Turbo)) {
        EnqueueFrame(ACFramer::Key::Mode, static_cast<uint16_t>(ACFramer::ModeValue::Cool), true /* query_after */);
      }
    }
  }

  // 2. Handle Climate Mode changes
  if (call.get_mode().has_value()) {
    auto m = *call.get_mode();
    if (m == climate::CLIMATE_MODE_OFF) {
      // Control board firmware quirk workaround (protocol.md):
      // The Summit2 firmware has a bug where heating engages for several seconds
      // every few minutes when Mode is set to Heat (2), even if Power is Off (1).
      // To work around this, switch Mode to Cool (1) before setting Power to Off
      // if currently in Heat mode (or if mode is unknown/querying).
      if (cur_mode_ == ACFramer::ModeValue::Heat || cur_mode_ == ACFramer::ModeValue::Query) {
        EnqueueFrame(ACFramer::Key::Mode, static_cast<uint16_t>(ACFramer::ModeValue::Cool), true /* query_after */);
      }
      EnqueueFrame(ACFramer::Key::Power, static_cast<uint16_t>(ACFramer::OnOffValue::Off), true /* query_after */);
    } else {
      // Control board firmware quirk workaround (protocol.md):
      // The Summit2 firmware automatically resets Mode to Cool (1) when Power
      // transitions from Off to On. When powering on, Power must be sent before
      // Mode so the requested mode isn't immediately overwritten by the board.
      // Additionally, invalidate cur_mode_ so the climate entity waits for the
      // Mode confirmation frame before publishing its state.
      if (cur_power_state_ != ACFramer::OnOffValue::On) {
        cur_mode_ = ACFramer::ModeValue::Query;
        EnqueueFrame(ACFramer::Key::Power, static_cast<uint16_t>(ACFramer::OnOffValue::On), true /* query_after */);
      }

      ACFramer::ModeValue ac_mode = ACFramer::ModeValue::Query;
      if (m == climate::CLIMATE_MODE_COOL) {
        // If a specific preset (e.g. ECO, SLEEP, BOOST) was also specified in this call,
        // it was already enqueued above. Otherwise, default to standard Cool mode.
        if (!call.get_preset().has_value() || *call.get_preset() == climate::CLIMATE_PRESET_NONE) {
          ac_mode = ACFramer::ModeValue::Cool;
        }
      } else if (m == climate::CLIMATE_MODE_HEAT) {
        ac_mode = ACFramer::ModeValue::Heat;
      } else if (m == climate::CLIMATE_MODE_FAN_ONLY) {
        ac_mode = ACFramer::ModeValue::Fan;
      } else if (m == climate::CLIMATE_MODE_DRY) {
        ac_mode = ACFramer::ModeValue::Wet;
      }

      if (ac_mode != ACFramer::ModeValue::Query) {
        EnqueueFrame(ACFramer::Key::Mode, static_cast<uint16_t>(ac_mode), true /* query_after */);
      }
    }
  }

  if (call.get_target_temperature().has_value()) {
    float target_c = *call.get_target_temperature();
    uint16_t target_wire_val;
    if (board_uses_fahrenheit_) {
      // Board in Fahrenheit: convert Celsius to Fahrenheit
      float fahrenheit = (target_c * 9.0f / 5.0f) + 32.0f;
      target_wire_val = static_cast<uint16_t>(std::round(fahrenheit));
    } else {
      // Board in Celsius: round to nearest degree C
      target_wire_val = static_cast<uint16_t>(std::round(target_c));
    }
    EnqueueFrame(ACFramer::Key::SetTemperature, target_wire_val, true /* query_after */);
  }

  if (call.get_fan_mode().has_value()) {
    auto fm = *call.get_fan_mode();
    uint16_t speed = 3;
    if (fm == climate::CLIMATE_FAN_LOW)
      speed = 1;
    else if (fm == climate::CLIMATE_FAN_MEDIUM)
      speed = 3;
    else if (fm == climate::CLIMATE_FAN_HIGH)
      speed = 5;
    EnqueueFrame(ACFramer::Key::FanSpeed, speed, true /* query_after */);
  }
}

void OutEquipAC::WriteFrame(ACFramer &framer, bool is_query) {
  expecting_key = framer.GetKey();
  in_flight_is_query_ = is_query;
  awaiting_response_ = true;
  this->write_array(framer.buffer(), framer.buffer_pos());
  last_frame_sent = millis();
  num_frames_tx_++;
}

void OutEquipAC::MaybeSendCurFrame() {
  if (awaiting_response_) {
    return;
  }
  if (!txQueue.empty()) {
    ACFramer next_frame = txQueue.front();
    txQueue.pop();
    // A frame from txQueue with value == 0 is a priority re-query
    bool is_query = (next_frame.GetValue() == ACFramer::kQueryVal);
    WriteFrame(next_frame, is_query);
    return;
  }
  if (millis() - last_full_status < 1000) {
    return;
  }
  ACFramer txFramer;
  txFramer.NewFrame(kQueryKeys[cur_query_key_idx], ACFramer::kQueryVal);
  WriteFrame(txFramer, true /* is_query */);
}

bool OutEquipAC::EnqueueFrame(ACFramer::Key key, uint16_t value, bool query_after) {
  ACFramer txFramer;
  if (!txFramer.NewFrame(key, value))
    return false;
  txQueue.push(txFramer);

  if (query_after) {
    ACFramer queryFramer;
    if (queryFramer.NewFrame(key, ACFramer::kQueryVal)) {
      txQueue.push(queryFramer);
    }
  }

  // If bus is currently idle, transmit immediately without waiting for loop interval
  if (!awaiting_response_) {
    MaybeSendCurFrame();
  }
  return true;
}

constexpr ACFramer::Key OutEquipAC::kQueryKeys[];

} // namespace outequip_ac
} // namespace esphome
