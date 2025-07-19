#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <ElegantOTA.h>
#include <WebSerial.h>
#include <WiFi.h>

#include <queue>

#include "Version.h"
#include "ac_framer.h"
#include "config.h"
#include "multiserial.h"

#define AC_BAUD_RATE 115200

constexpr int kDataRefreshRateInMs = 2000;

MultiSerial mSerial;
AsyncWebServer server(80);
long last_wifi_connect_attempt = 0;

HardwareSerial acSerial(2);
ACFramer rxFramer;

constexpr ACFramer::Key kQueryKeys[] = {ACFramer::Key::Power,
                                        ACFramer::Key::Mode,
                                        ACFramer::Key::SetTemperature,
                                        ACFramer::Key::FanSpeed,
                                        ACFramer::Key::UndervoltProtect,
                                        ACFramer::Key::OvervoltProtect,
                                        ACFramer::Key::IntakeAirTemp,
                                        ACFramer::Key::OutletAirTemp,
                                        ACFramer::Key::LCD,
                                        ACFramer::Key::Voltage,
                                        ACFramer::Key::Amperage,
                                        ACFramer::Key::Light};
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
std::queue<ACFramer> txQueue;

void ConnectWiFi() {
  last_wifi_connect_attempt = millis();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  mSerial.print("Disconnected from WiFi access point. Reason: ");
  mSerial.println(info.wifi_sta_disconnected.reason);
  mSerial.println("Attempting to reconnect.");
  ConnectWiFi();
}

void WriteFrame(const ACFramer& framer) {
  mSerial.printf("Tx: %s: %d\n", ACFramer::KeyToString(framer.GetKey()),
                 framer.GetValue());
  acSerial.write(framer.buffer(), framer.buffer_pos());
  last_frame_sent = millis();
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
  if (millis() - last_full_status < kDataRefreshRateInMs) {
    return;
  }
  ACFramer txFramer;
  txFramer.NewFrame(kQueryKeys[cur_query_key_idx], ACFramer::kQueryVal);
  WriteFrame(txFramer);
}

bool EnqueueFrame(ACFramer::Key key, uint16_t value) {
  ACFramer txFramer;
  if (!txFramer.NewFrame(key, value)) {
    return false;
  }
  mSerial.printf("Enqueued: %s: %d\n", ACFramer::KeyToString(txFramer.GetKey()),
                 txFramer.GetValue());
  txQueue.push(txFramer);
  return true;
}

bool TrySet(const String& key, const String& value) {
  auto k = std::find_if(std::begin(kSetKeys), std::end(kSetKeys),
                        [key](auto k) { return key == ACFramer::KeyToId(k); });
  if (k != std::end(kSetKeys)) {
    EnqueueFrame(*k, ACFramer::kQueryVal);
    return EnqueueFrame(*k, value.toInt());
  }
  return false;
}

void HandleSet(AsyncWebServerRequest* request) {
  String response;

  mSerial.println("HandleSet()");
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
      mSerial.printf("Invalid key (%s) or value (%s).\n", key, value);
    }
    return;
  }
  mSerial.println("Unknown command.");
}

void DumpFailedFrame(const ACFramer& framer) {
  mSerial.print("(Failed frame: 0x");
  for (int i = 0; i < framer.buffer_pos(); i++) {
    mSerial.printf(" %02x", framer.buffer()[i]);
  }
  mSerial.printf(", %d)\n", framer.buffer_pos());
}

void DumpHexAndAscii(const uint8_t c) {
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
  // Wait for serial console to open.
  while (!Serial) {
    delay(10);
  }
  delay(2000);  // Wait for monitor to open.
  mSerial.printf("OutEquip AC v%s initializing...\n", VERSION);

  mSerial.printf("Starting AC serial communication at %d baud...\n",
                 AC_BAUD_RATE);
  acSerial.setRxBufferSize(1024);
  acSerial.begin(AC_BAUD_RATE, SERIAL_8N1, AC_RX_PIN, AC_TX_PIN);

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.onEvent(WiFiStationDisconnected,
               WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  ConnectWiFi();
  mSerial.printf("Connecting to SSID '%s'...", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED &&
         millis() < WIFI_CONNECT_WAIT_IN_S * 1000) {
    delay(500);
    mSerial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    mSerial.print("\nConnected, IP address: ");
    mSerial.println(WiFi.localIP());
  } else {
    mSerial.printf("\nUnable to connect after %ds. Continuing.\n",
                   WIFI_CONNECT_WAIT_IN_S);
  }

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

  // Web Server
  server.on("/", [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain",
                  HOSTNAME " " VERSION " (Built " BUILD_TIMESTAMP ")");
  });
  server.on("/set", HTTP_POST, HandleSet);
  server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "404: Not found");
  });
  server.begin();
}

void loop() {
  // Check if we're connected (or recently attempted reconnecting to) WiFi.
  if (WiFi.status() != WL_CONNECTED &&
      millis() - last_wifi_connect_attempt > WIFI_CONNECT_WAIT_IN_S * 1000) {
    mSerial.println(
        "WiFi still appears disconnected. Attempting to reconnect.");
    WiFi.disconnect();
    ConnectWiFi();
  }

  // Handle updates.
  ElegantOTA.loop();

  // Handle WebSerial messages.
  WebSerial.loop();

  // Send a query if it's been long enough since our last one.
  if (last_frame_sent < millis() - kDataRefreshRateInMs) {
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
      auto key = rxFramer.GetKey();
      auto value = rxFramer.GetValue();
      auto unknown = rxFramer.GetUnknown();
      rxFramer.Reset();
      mSerial.printf("Rx: %s: %d\n", ACFramer::KeyToString(key), value);
      if (unknown != 0x01) {
        mSerial.printf("Found unexpected value in Unknown byte: 0x%02x\n",
                       unknown);
      }

      // Check if we got a response to our current outstanding query.
      if (key == kQueryKeys[cur_query_key_idx]) {
        // Sure did. Increment our query index.
        if (++cur_query_key_idx >= sizeof(kQueryKeys) / sizeof(*kQueryKeys)) {
          // We've finished our full set of queries this round.
          cur_query_key_idx = 0;
          last_full_status = millis();
        }
      }
      MaybeSendCurFrame();
    }
    delay(10);
  }
}