// Connectivity.
#define HOSTNAME "outequip-ac"
#define WIFI_SSID "..."
#define WIFI_PASS "..."
#define WIFI_CONNECT_WAIT_IN_S 10

// Hardware pins.
#define AC_TX_PIN 16
#define AC_RX_PIN 18

// InfluxDB stats reporting.
const IPAddress kInfluxHost(192,168,8,1);  // Influx DB Host
constexpr uint16_t kInfluxPort=8096;  // Influx DB UDP Port
// InfluxDB sample push interval.
#define PUSH_SAMPLE_INTERVAL_IN_S 10