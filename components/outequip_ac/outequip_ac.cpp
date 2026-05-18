#include "outequip_ac.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include <cmath>

#ifdef USE_WEBSERVER
#include "esphome/components/web_server_base/web_server_base.h"
#include "outequip_ac_thermostat_html.h"
#endif

namespace esphome {
namespace outequip_ac {

#ifdef USE_WEBSERVER
class OutEquipCustomPageHandler : public AsyncWebHandler {
protected:
  OutEquipAC *parent_;

public:
  OutEquipCustomPageHandler(OutEquipAC *parent) : parent_(parent) {}

  bool canHandle(AsyncWebServerRequest *request) const override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    if (request->method() != HTTP_GET)
      return false;
    std::string url = request->url_to(url_buf);
    return url == "/thermostat" || url == "/outequip_ac";
  }

  void handleRequest(AsyncWebServerRequest *request) override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    std::string url = request->url_to(url_buf);
    if (url == "/thermostat") {
      std::string html(reinterpret_cast<const char *>(OUTEQUIP_AC_HTML_GZ),
                       OUTEQUIP_AC_HTML_GZ_SIZE);
      AsyncWebServerResponse *response =
          request->beginResponse(200, "text/html", html);
      response->addHeader("Content-Encoding", "gzip");
      request->send(response);
      return;
    }

    if (url == "/outequip_ac") {
      float min_temp = parent_->get_traits().get_visual_min_temperature();
      float max_temp = parent_->get_traits().get_visual_max_temperature();
      char json_buf[128];
      snprintf(json_buf, sizeof(json_buf), "{\"min_temp\":%.1f,\"max_temp\":%.1f}", min_temp, max_temp);
      AsyncWebServerResponse *response = request->beginResponse(
          200, "application/json", json_buf);
      request->send(response);
      return;
    }
  }
};
#endif

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
                     : static_cast<uint16_t>(ACFramer::OnOffValue::Off));
  if (lcd_switch_ != nullptr) {
    lcd_switch_->publish_state(state);
  }
}

void OutEquipAC::set_swing_state(bool state) {
  ESP_LOGD("outequip_ac", "Setting Swing state to %s", state ? "ON" : "OFF");
  EnqueueFrame(ACFramer::Key::Swing,
               state ? static_cast<uint16_t>(ACFramer::OnOffValue::On)
                     : static_cast<uint16_t>(ACFramer::OnOffValue::Off));
}

void OutEquipAC::set_light_state(bool state) {
  ESP_LOGD("outequip_ac", "Setting Light state to %s", state ? "ON" : "OFF");
  EnqueueFrame(ACFramer::Key::Light,
               state ? static_cast<uint16_t>(ACFramer::LightValue::On)
                     : static_cast<uint16_t>(ACFramer::LightValue::Off));
  if (light_switch_ != nullptr) {
    light_switch_->publish_state(state);
    light_switch_->set_has_state(true);
  }
}

void OutEquipAC::setup() {
  EnqueueFrame(ACFramer::Key::Active, 0);
  last_frame_sent = millis();

#ifdef USE_WEBSERVER
  if (web_server_base::global_web_server_base != nullptr) {
    web_server_base::global_web_server_base->add_handler(
        new OutEquipCustomPageHandler(this));
  }
#endif
}

void OutEquipAC::loop() {
  if (millis() - last_frame_sent >= 1000) {
    MaybeSendCurFrame();
  }

  auto publish_sensor = [](sensor::Sensor *sensor, float value) {
    if (sensor != nullptr && (!sensor->has_state() || sensor->state != value)) {
      ESP_LOGD("outequip_ac", "Publishing sensor state for '%s': %.1f",
               sensor->get_name().c_str(), value);
      sensor->publish_state(value);
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
      rxFramer.Reset();
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
            lcd_switch_->publish_state(false);
            lcd_switch_->set_has_state(true);
          } else if (old_power_state == ACFramer::OnOffValue::Off &&
                     cur_power_state_ == ACFramer::OnOffValue::On) {
            lcd_switch_->publish_state(true);
            lcd_switch_->set_has_state(true);
          }
        }
        break;
      }
      case ACFramer::Key::Mode:
        cur_mode_ = static_cast<ACFramer::ModeValue>(value);
        break;
      case ACFramer::Key::SetTemperature: {
        float new_target = (value - 32.0f) * 5.0f / 9.0f;
        if (this->target_temperature != new_target) {
          ESP_LOGD("outequip_ac", "Climate target temp changed to %.1f C",
                   new_target);
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
          // A serial-interface reported value of 1 is off and 0 is on
          bool is_on = (value == 0);
          if (!lcd_switch_->has_state() || lcd_switch_->state != is_on) {
            ESP_LOGD("outequip_ac", "LCD switch state changed to %s",
                     is_on ? "ON" : "OFF");
            lcd_switch_->publish_state(is_on);
            lcd_switch_->set_has_state(true);
          }
        }
        break;
      case ACFramer::Key::Swing:
        if (swing_switch_ != nullptr) {
          bool is_on =
              (value == static_cast<uint16_t>(ACFramer::OnOffValue::On));
          if (!swing_switch_->has_state() || swing_switch_->state != is_on) {
            ESP_LOGD("outequip_ac", "Swing switch state changed to %s",
                     is_on ? "ON" : "OFF");
            swing_switch_->publish_state(is_on);
            swing_switch_->set_has_state(true);
          }
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
        if (cur_power_state_ == ACFramer::OnOffValue::On) {
          switch (cur_mode_) {
          case ACFramer::ModeValue::Cool:
          case ACFramer::ModeValue::Eco:
          case ACFramer::ModeValue::Sleep:
          case ACFramer::ModeValue::Turbo:
            new_mode = climate::CLIMATE_MODE_COOL;
            break;
          case ACFramer::ModeValue::Heat:
            new_mode = climate::CLIMATE_MODE_HEAT;
            break;
          case ACFramer::ModeValue::Fan:
            new_mode = climate::CLIMATE_MODE_FAN_ONLY;
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
      }

      if (climate_changed) {
        this->publish_state();
      }

      // Check response expecting
      if (expecting_key.has_value() && *expecting_key == key) {
        expecting_key.reset();
        if (key == kQueryKeys[cur_query_key_idx]) {
          if (++cur_query_key_idx >= sizeof(kQueryKeys) / sizeof(*kQueryKeys)) {
            cur_query_key_idx = 0;
            last_full_status = millis();
          }
        }
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
       climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_FAN_ONLY});
  traits.set_supported_fan_modes({climate::CLIMATE_FAN_LOW,
                                  climate::CLIMATE_FAN_MEDIUM,
                                  climate::CLIMATE_FAN_HIGH});
  return traits;
}

void OutEquipAC::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    auto m = *call.get_mode();
    if (m == climate::CLIMATE_MODE_OFF) {
      EnqueueFrame(ACFramer::Key::Mode, 1);
      EnqueueFrame(ACFramer::Key::Power, 1);
    } else {
      uint16_t ac_mode = 0;
      if (m == climate::CLIMATE_MODE_COOL)
        ac_mode = 1;
      else if (m == climate::CLIMATE_MODE_HEAT)
        ac_mode = 2;
      else if (m == climate::CLIMATE_MODE_FAN_ONLY)
        ac_mode = 3;

      if (ac_mode != 0)
        EnqueueFrame(ACFramer::Key::Mode, ac_mode);
      EnqueueFrame(ACFramer::Key::Power, 2);
    }
  }

  if (call.get_target_temperature().has_value()) {
    // ESPHome provides target temperature in Celsius, convert to Fahrenheit
    // for the board
    float fahrenheit = (*call.get_target_temperature() * 9.0f / 5.0f) + 32.0f;
    EnqueueFrame(ACFramer::Key::SetTemperature,
                 static_cast<uint16_t>(std::round(fahrenheit)));
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
    EnqueueFrame(ACFramer::Key::FanSpeed, speed);
  }
}

void OutEquipAC::WriteFrame(ACFramer &framer) {
  expecting_key = framer.GetKey();
  this->write_array(framer.buffer(), framer.buffer_pos());
  last_frame_sent = millis();
  num_frames_tx_++;
}

void OutEquipAC::MaybeSendCurFrame() {
  if (!txQueue.empty()) {
    WriteFrame(txQueue.front());
    txQueue.pop();
    return;
  }
  if (millis() - last_full_status < 1000) {
    return;
  }
  ACFramer txFramer;
  txFramer.NewFrame(kQueryKeys[cur_query_key_idx], ACFramer::kQueryVal);
  WriteFrame(txFramer);
}

bool OutEquipAC::EnqueueFrame(ACFramer::Key key, uint16_t value) {
  ACFramer txFramer;
  if (!txFramer.NewFrame(key, value))
    return false;
  txQueue.push(txFramer);
  return true;
}

constexpr ACFramer::Key OutEquipAC::kQueryKeys[];

} // namespace outequip_ac
} // namespace esphome
