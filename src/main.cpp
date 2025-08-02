#ifdef ARDUINO
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <ElegantOTA.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <LittleFS.h>
#include <NetWizard.h>
#include <WebSerial.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ir_Coolix.h>

#include <numeric>
#include <optional>
#include <queue>

#include "Version.h"
#include "ac_framer.h"
#include "config.h"
#include "multiserial.h"

#ifndef PUSH_SAMPLE_INTERVAL_IN_S
#define DISABLE_INFLUX
#endif

#ifndef DISABLE_INFLUX
#include <InfluxDbClient.h>
#endif  // DISABLE_INFLUX

#define AC_BAUD_RATE 115200

constexpr int kWiFiReconnectTimeoutInS = 10;
constexpr int kDataRefreshRateInS = 1;

MultiSerial mSerial;
AsyncWebServer server(80);
NetWizard netWizard(&server);
long last_wifi_connect_attempt = 0;

HardwareSerial acSerial(1);
ACFramer rxFramer;
IRCoolixAC ir(IR_TX_PIN);

constexpr ACFramer::Key kQueryKeys[] = {ACFramer::Key::Power,
                                        ACFramer::Key::Mode,
                                        ACFramer::Key::SetTemperature,
                                        ACFramer::Key::FanSpeed,
                                        ACFramer::Key::UndervoltProtect,
                                        ACFramer::Key::IntakeAirTemp,
                                        ACFramer::Key::OutletAirTemp,
                                        ACFramer::Key::LCD,
                                        ACFramer::Key::Voltage};
constexpr ACFramer::Key kSetKeys[] = {ACFramer::Key::Power,
                                      ACFramer::Key::Mode,
                                      ACFramer::Key::SetTemperature,
                                      ACFramer::Key::FanSpeed,
                                      ACFramer::Key::UndervoltProtect,
                                      ACFramer::Key::LCD,
                                      ACFramer::Key::Light,
                                      ACFramer::Key::Active};
constexpr size_t kNumQueryKeys = sizeof(kQueryKeys) / sizeof(*kQueryKeys);
size_t cur_query_key_idx = 0;
long last_frame_sent = 0;
long last_full_status = 0;
long num_frames_tx = 0;
long num_frames_rx = 0;
long num_frames_failed = 0;
long num_spurious_bytes_rx = 0;
std::queue<ACFramer> txQueue;
std::optional<ACFramer::Key> expecting_key;

#ifndef DISABLE_INFLUX
// InfluxDB data point
Point sensor("outequip-ac");
long last_influxdb_push = 0;
WiFiUDP Udp;
#endif

// Latest status from board.
ACFramer::OnOffValue cur_power_state = ACFramer::OnOffValue::Query;
ACFramer::ModeValue cur_mode = ACFramer::ModeValue::Query;
uint8_t cur_set_temp = 0;
uint8_t cur_set_temp_f = 0;
uint8_t cur_fan_speed = 0;
float cur_undervolt = std::nan("");
uint16_t cur_overvolt = 0;
uint16_t cur_intake_temp = 0;
uint16_t cur_outlet_temp = 0;
ACFramer::OnOffValue cur_lcd = ACFramer::OnOffValue::Query;
float cur_voltage = std::nan("");

// IR blast state.
uint8_t new_set_temp = 0;         // What we're trying to set temp to.
uint8_t expecting_fan_speed = 0;  // Used to confirm IR commands.
int8_t cur_ir_retry = -1;         // Negative if we haven't tried yet.
constexpr uint8_t kMaxIrRetries = 5;

void ConnectWiFi() {
  last_wifi_connect_attempt = millis();
  netWizard.autoConnect("OutEquipAC", "");
}

void WriteFrame(ACFramer& framer) {
  if (framer.GetKey() != kQueryKeys[cur_query_key_idx]) {
    mSerial.printf("\t\t\tTx: %s=%s\n", framer.GetKeyAsString(),
                   framer.GetValueAsString());
  }
  expecting_key = framer.GetKey();
  acSerial.write(framer.buffer(), framer.buffer_pos());
  last_frame_sent = millis();
  ++num_frames_tx;
}

// Send the current query only if it's been long enough since the last refresh.
void MaybeSendCurFrame() {
  // Send a frame from the queue first if possible, and return if so.
  if (!txQueue.empty()) {
    WriteFrame(txQueue.front());
    txQueue.pop();
    return;
  }

  // Send the next status query frame if it's time to do so.
  if (millis() - last_full_status < kDataRefreshRateInS * 1000) {
    return;
  }
  ACFramer txFramer;
  txFramer.NewFrame(kQueryKeys[cur_query_key_idx], ACFramer::kQueryVal);
  WriteFrame(txFramer);
}

bool EnqueueFrame(ACFramer::Key key, uint16_t value,
                  bool allow_invalid = false) {
  ACFramer txFramer;
  if (!txFramer.NewFrame(key, value, allow_invalid)) {
    return false;
  }
  if (allow_invalid) {
    mSerial.printf("Enqueued: %d=%d\n", static_cast<uint8_t>(key), value);
  } else {
    mSerial.printf("Enqueued: %s=%s\n", txFramer.GetKeyAsString(),
                   txFramer.GetValueAsString());
  }
  txQueue.push(txFramer);
  return true;
}

bool TrySet(const String& key, const String& value) {
  auto k =
      std::find_if(std::begin(kSetKeys), std::end(kSetKeys),
                   [key](auto k) { return key == ACFramer::KeyToString(k); });
  if (k != std::end(kSetKeys)) {
    if (*k == ACFramer::Key::SetTemperature) {
      // We set temperature over IR.
      // First, change the fan speed to we can detect IR receipt.
      // When we receive the reply for that, we'll blast IR, verify fan speed,
      // and retry as necessary.
      new_set_temp = value.toInt();
      // Sanitize.
      if (new_set_temp < 16 || (new_set_temp > 30 && new_set_temp < 63) ||
          new_set_temp > 86) {
        new_set_temp = 0;
        return false;
      }
      // Query fan speed, which will kick off our IR blast logic.
      return EnqueueFrame(ACFramer::Key::FanSpeed, ACFramer::kQueryVal);
    }
    EnqueueFrame(*k, ACFramer::kQueryVal);
    return EnqueueFrame(*k, value.toInt());
  }
  return false;
}

void HandleSet(AsyncWebServerRequest* request) {
  String response;

  auto params = request->params();
  for (int i = 0; i < params; i++) {
    const auto* p = request->getParam(i);
    if (p->isFile() || !p->isPost()) {
      continue;
    }
    if (!TrySet(p->name(), p->value())) {
      response += "Invalid " + p->name() + " value :" + p->value() + "\n";
    }
  }

  request->send(200, "text/plain", response);
}

bool convertToJson(const ACFramer::OnOffValue value, JsonVariant variant) {
  return variant.set(ACFramer::OnOffValueToString(value));
}

bool convertToJson(const ACFramer::ModeValue value, JsonVariant variant) {
  return variant.set(ACFramer::ModeValueToString(value));
}

void HandleVarDump(AsyncWebServerRequest* request) {
  AsyncResponseStream* response =
      request->beginResponseStream("application/json");
  JsonDocument json_doc;

  json_doc[ACFramer::KeyToString(ACFramer::Key::Power)] = cur_power_state;
  json_doc[ACFramer::KeyToString(ACFramer::Key::Mode)] = cur_mode;
  json_doc[ACFramer::KeyToString(ACFramer::Key::SetTemperature)] = cur_set_temp;
  json_doc["setTempF"] = (cur_set_temp_f);
  json_doc[ACFramer::KeyToString(ACFramer::Key::FanSpeed)] = cur_fan_speed;
  json_doc[ACFramer::KeyToString(ACFramer::Key::UndervoltProtect)] =
      cur_undervolt;
  json_doc[ACFramer::KeyToString(ACFramer::Key::OvervoltProtect)] =
      cur_overvolt;
  json_doc[ACFramer::KeyToString(ACFramer::Key::IntakeAirTemp)] =
      cur_intake_temp;
  json_doc[ACFramer::KeyToString(ACFramer::Key::OutletAirTemp)] =
      cur_outlet_temp;
  json_doc[ACFramer::KeyToString(ACFramer::Key::LCD)] = cur_lcd;
  json_doc[ACFramer::KeyToString(ACFramer::Key::Voltage)] = cur_voltage;

  serializeJsonPretty(json_doc, *response);
  request->send(response);
}

void HandleStats(AsyncWebServerRequest* request) {
  AsyncResponseStream* response =
      request->beginResponseStream("application/json");

  JsonDocument json_doc;
  json_doc["millis"] = millis();
  json_doc["freeHeap"] = ESP.getFreeHeap();
  json_doc["minFreeHeap"] = ESP.getMinFreeHeap();
  json_doc["maxAllocHeap"] = ESP.getMaxAllocHeap();

  serializeJsonPretty(json_doc, *response);
  request->send(response);
}

void HandleWebSerialMessage(const String& message) {
  if (message.startsWith("set ")) {
    auto valueDelimiter = message.indexOf('=');
    if (valueDelimiter == -1) {
      mSerial.println("Invalid set command. Syntax: set <key>=<value>");
      return;
    }
    const String key = message.substring(4, valueDelimiter);
    const String value = message.substring(valueDelimiter + 1);
    if (!TrySet(key, value)) {
      mSerial.printf(
          "Invalid key (%s) or value (%s). Valid keys:%s", key, value,
          std::accumulate(std::begin(kSetKeys), std::end(kSetKeys), String(),
                          [](String acc, auto k) {
                            return acc + "\n\t" + ACFramer::KeyToString(k);
                          })
              .c_str());
    }
    return;
  }

  if (message.startsWith("debugSet ")) {
    auto valueDelimiter = message.indexOf('=');
    if (valueDelimiter == -1) {
      mSerial.println(
          "Invalid debugSet command. Syntax: debugSet <key>=<value>");
      return;
    }
    auto key = message.substring(9, valueDelimiter).toInt();
    auto value = message.substring(valueDelimiter + 1).toInt();
    EnqueueFrame(static_cast<ACFramer::Key>(key), value, true);
    return;
  }

  if (message == "restart") {
    ESP.restart();
    return;
  }

  if (message == "resetConfig") {
    netWizard.reset();
    ESP.restart();
    return;
  }

  mSerial.println("Unknown command. Valid commands:"
                  "\n\tset"
                  "\n\tdebugSet"
                  "\n\trestart"
                  "\n\tresetConfig");
}

void DumpFailedFrame(const ACFramer& framer) {
  ++num_frames_failed;
  mSerial.printf("(Failed frame (%d:%d) 0x",
                 static_cast<uint8_t>(framer.GetKey()), framer.GetValue());
  for (int i = 0; i < framer.buffer_pos(); i++) {
    mSerial.printf(" %02x", framer.buffer()[i]);
  }
  mSerial.printf(", %d)\n", framer.buffer_pos());
}

void DumpHexAndAscii(const uint8_t c) {
  ++num_spurious_bytes_rx;
  mSerial.printf("0x%02x", c);
  switch (c) {
    case '\r':
      mSerial.print(" (\\r)");
      break;
    case '\n':
      mSerial.print(" (\\n)");
      break;
    default:
      mSerial.printf(" (%c)", c);
  }
}

// cppcheck-suppress unusedFunction
void setup() {
  Serial.begin(115200);
  delay(2000);  // Wait for monitor to open.
  mSerial.printf("OutEquip AC v%s initializing...\n", VERSION);

  // Set up our IR transmitter.
  mSerial.printf("Initializing IR transmitter at pin %d...\n", IR_TX_PIN);
  ir.begin();
  ir.calibrate();

  mSerial.printf("Starting AC serial communication at %d baud...\n",
                 AC_BAUD_RATE);
  acSerial.setRxBufferSize(1024);
  acSerial.begin(AC_BAUD_RATE, SERIAL_8N1, AC_RX_PIN, AC_TX_PIN);

  // WiFi
#ifdef HOSTNAME
  netWizard.setHostname(HOSTNAME);
#endif
  ConnectWiFi();

  // mDNS
  if (!MDNS.begin(HOSTNAME)) {
    mSerial.println("Error setting up MDNS responder!");
  }
  mSerial.printf("mDNS responder started at %s.local\n", HOSTNAME);

  // WebSerial
  WebSerial.begin(&server);
  WebSerial.onMessage(HandleWebSerialMessage);
  mSerial.enable_web_serial();

  // OTA Updates
  ElegantOTA.begin(&server, OTA_USER, OTA_PASS);

  // SPI Flash Files System
  LittleFS.begin();

  // Web Server
  server.on("/set", HTTP_POST, HandleSet);
  server.on("/var_dump", HandleVarDump);
  server.on("/version", [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain",
                  HOSTNAME " " VERSION " (Built " BUILD_TIMESTAMP ")");
  });
  server.on("/quitquitquit",
            [](AsyncWebServerRequest* request) { ESP.restart(); });
  server.on("/stats", HandleStats);
  server.serveStatic("/", LittleFS, "/htdocs/")
      .setDefaultFile("thermostat.html");
  server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "404: Not found");
  });
  server.begin();

#ifndef DISABLE_INFLUX
  // Influx sensor.
  sensor.addTag("host", HOSTNAME);
  last_influxdb_push = millis();  // Don't push right away at first loop.
#endif

  // Make sure the unit is ready to send/receive.
  // Not strictly necessary, but here for correctness.
  EnqueueFrame(ACFramer::Key::Active, 0);
  // Give it a second before sending frames.
  last_frame_sent = millis();
}

void loop() {
  // Handle WiFi.
  netWizard.loop();
  // Check if we're connected (or recently attempted reconnecting to) WiFi.
  if (WiFi.status() != WL_CONNECTED &&
      millis() - last_wifi_connect_attempt > kWiFiReconnectTimeoutInS * 1000) {
    mSerial.println(
        "WiFi appears disconnected. Attempting to reconnect.");
    ConnectWiFi();
  }

  // Handle updates.
  ElegantOTA.loop();

  // Handle WebSerial messages.
  WebSerial.loop();

  // Send a query if it's been long enough since our last one.
  if (last_frame_sent < millis() - kDataRefreshRateInS * 1000) {
    MaybeSendCurFrame();
  }

  // Handle serial data from control board.
  while (acSerial.available()) {
    uint8_t c = acSerial.read();
    if (!rxFramer.FrameData(c)) {
      mSerial.print("Rx: ");
      if (rxFramer.buffer_pos() > 0) {
        DumpFailedFrame(rxFramer);
      } else {
        DumpHexAndAscii(c);
      }
      mSerial.println();
      rxFramer.Reset();
    } else if (rxFramer.HasFullFrame()) {
      ++num_frames_rx;
      const auto key = rxFramer.GetKey();
      const auto value = rxFramer.GetValue();

      // Save state.
      switch (key) {
        case ACFramer::Key::Power:
          cur_power_state = static_cast<ACFramer::OnOffValue>(value);
          break;
        case ACFramer::Key::Mode:
          cur_mode = static_cast<ACFramer::ModeValue>(value);
          break;
        case ACFramer::Key::SetTemperature: {
          const uint8_t expected_set_temp =
              static_cast<uint16_t>(cur_set_temp_f - 32) * 5 / 9;
          if (expected_set_temp != value) {
            uint8_t new_set_temp_f = ceil(value * 1.8 + 32);
            mSerial.printf(
                "New set temp: Current F (%d) doesn't match C (got %d, "
                "expected %d). Updating F (%d).\n",
                cur_set_temp_f, value, expected_set_temp, new_set_temp_f);
            cur_set_temp_f = new_set_temp_f;
          }
          cur_set_temp = value;
          break;
        }
        case ACFramer::Key::FanSpeed:
          cur_fan_speed = value;
          break;
        case ACFramer::Key::UndervoltProtect:
          cur_undervolt = value / 10.0;
          break;
        case ACFramer::Key::OvervoltProtect:
          cur_overvolt = value;
          break;
        case ACFramer::Key::IntakeAirTemp:
          cur_intake_temp = value;
          break;
        case ACFramer::Key::OutletAirTemp:
          cur_outlet_temp = value;
          break;
        case ACFramer::Key::LCD:
          cur_lcd = static_cast<ACFramer::OnOffValue>(value);
          break;
        case ACFramer::Key::Voltage:
          cur_voltage = value / 10.0;
          break;
        case ACFramer::Key::Amperage:
        case ACFramer::Key::Light:
        case ACFramer::Key::Swing:
        case ACFramer::Key::Active:
          // Ignore.
          break;
      }

      // Handle handshake/activation.
      // Not strictly necessary, but here for correctness.
      if (key == ACFramer::Key::Active && value == 2) {
        // We're being asked if we are ready. Always respond yes.
        EnqueueFrame(ACFramer::Key::Active, 1);
      }

      // Check if we got a response to our current outstanding query.
      if (expecting_key.has_value()) {
        if (*expecting_key == key) {
          expecting_key.reset();
          if (key == kQueryKeys[cur_query_key_idx]) {
            // Got our next query key. Increment our query index.
            if (++cur_query_key_idx >=
                sizeof(kQueryKeys) / sizeof(*kQueryKeys)) {
              // We've finished our full set of queries this round.
              cur_query_key_idx = 0;
              last_full_status = millis();
            }
          } else {
            mSerial.printf("Rx: %s=%s\n", rxFramer.GetKeyAsString(),
                           rxFramer.GetValueAsString());
            // Check if we're trying to IR blast.
            if (new_set_temp > 0 && key == ACFramer::Key::FanSpeed) {
              if (expecting_fan_speed == value) {
                // We got what we expected, no need to keep expecting.
                mSerial.printf("Confirmed set temp: %d\n", new_set_temp);
                if (new_set_temp > 30) {
                  cur_set_temp_f = new_set_temp;
                }
                new_set_temp = 0;
                expecting_fan_speed = 0;
                cur_ir_retry = -1;
                // Query the new temperature.
                EnqueueFrame(ACFramer::Key::SetTemperature,
                             ACFramer::kQueryVal);
              } else {
                // We haven't yet succeeded with a blast.
                if (cur_ir_retry < 0) {
                  // We're about try blasting this new temp for the first time.
                  cur_ir_retry = 0;
                  // Save our fan speed so that we know when we've succeeded.
                  expecting_fan_speed = cur_fan_speed;
                  // Set up the IR blast to be sent, but don't send it yet.
                  ir.on();
                  ir.setTemp(new_set_temp);
                  switch (cur_mode) {
                    case ACFramer::ModeValue::Query:
                    case ACFramer::ModeValue::Cool:
                    case ACFramer::ModeValue::Eco:
                    case ACFramer::ModeValue::Sleep:
                    case ACFramer::ModeValue::Turbo:
                    case ACFramer::ModeValue::Wet:
                    case ACFramer::ModeValue::Fan:
                      ir.setMode(kCoolixCool);
                      break;
                    case ACFramer::ModeValue::Heat:
                      ir.setMode(kCoolixHeat);
                      break;
                  }
                  switch (expecting_fan_speed) {
                    case 1:
                      ir.setFan(4);
                      break;
                    case 2:
                      ir.setFan(2);
                      break;
                    case 3:
                      ir.setFan(1);
                      break;
                    case 4:
                      ir.setFan(5);
                      break;
                    case 5:
                    default:
                      ir.setFan(7);
                      break;
                  }
                  // Change the fan speed, which will kick off an IR blast.
                  EnqueueFrame(ACFramer::Key::FanSpeed,
                               cur_fan_speed == 1 ? 2 : (cur_fan_speed - 1));
                } else if (cur_ir_retry <= kMaxIrRetries) {
                  // (Re)Try IR blast.
                  mSerial.printf("Sending IR, retry %d/%d: %s\n", cur_ir_retry,
                                 kMaxIrRetries, ir.toString().c_str());
                  ir.send(1);
                  ++cur_ir_retry;
                  // Query fan speed to confirm.
                  EnqueueFrame(ACFramer::Key::FanSpeed, 0);
                } else {
                  // All of our retries failed.
                  mSerial.printf("IR blast failed after %d retries.\n",
                                 kMaxIrRetries);
                  // Set the fan speed back to what it was.
                  EnqueueFrame(ACFramer::Key::FanSpeed, expecting_fan_speed);
                  new_set_temp = 0;
                  expecting_fan_speed = 0;
                  cur_ir_retry = -1;
                }
              }
            }
          }
        } else {
          mSerial.printf("Unexpected response to %s: %s=%s\n",
                         ACFramer::KeyToString(key), rxFramer.GetKeyAsString(),
                         rxFramer.GetValueAsString());
        }
      } else {
        mSerial.printf("Unexpected frame: %s=%s\n", rxFramer.GetKeyAsString(),
                       rxFramer.GetValueAsString());
      }

      // Finished with handling this received frame. Send off the next one.
      rxFramer.Reset();
      MaybeSendCurFrame();
    }
    delay(10);
  }

#ifndef DISABLE_INFLUX
  // Send to Influx every so often.
  if (millis() - last_influxdb_push > PUSH_SAMPLE_INTERVAL_IN_S * 1000) {
    sensor.clearFields();
    sensor.addField("power", ACFramer::OnOffValueToString(cur_power_state));
    sensor.addField("mode", ACFramer::ModeValueToString(cur_mode));
    sensor.addField("set_temp", cur_set_temp);
    sensor.addField("set_temp_f", cur_set_temp_f);
    sensor.addField("fan_speed", cur_fan_speed);
    sensor.addField("undervolt", cur_undervolt);
    sensor.addField("overvolt", cur_overvolt);
    sensor.addField("intake_temp", cur_intake_temp);
    sensor.addField("outlet_temp", cur_outlet_temp);
    sensor.addField("lcd", ACFramer::OnOffValueToString(cur_lcd));
    sensor.addField("voltage", cur_voltage);

    sensor.addField("uptime_ms", millis());
    sensor.addField("frames_tx", num_frames_tx);
    sensor.addField("frames_rx", num_frames_rx);
    sensor.addField("frames_failed", num_frames_failed);
    sensor.addField("spurious_bytes_rx", num_spurious_bytes_rx);

    // Debug output for influxdb write.
    auto idb_line = sensor.toLineProtocol();
    mSerial.println(idb_line);

    // Write point to influxdb
    Udp.beginPacket(kInfluxHost, kInfluxPort);
    Udp.write(reinterpret_cast<const uint8_t*>(idb_line.c_str()),
              idb_line.length());
    Udp.endPacket();
    last_influxdb_push = millis();
  }
#endif  // DISABLE_INFLUX
}
#elif !defined(PIO_UNIT_TESTING)
int main(int arc, char** argv) {
  // Nothing to do for non-arduino environments.
}
#endif  // ARDUINO