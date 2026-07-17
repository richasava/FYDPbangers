/**
 * ALIVE WCL - ESP32 bridge
 * ------------------------
 * STM32 --UART(COBS+protobuf)--> ESP32 --Wi-Fi/MQTT--> Mosquitto broker --> UE5
 *
 * Reads COBS-delimited protobuf BiometricFrames from the STM32 on UART2,
 * decodes them with nanopb, and publishes JSON-ish + raw protobuf to MQTT.
 * Also subscribes to alive/haptics and forwards HapticCommands back to the STM32.
 *
 * Board: any ESP32 DevKit. Libraries: PubSubClient (MQTT).
 * Drop these files next to this .ino so the Arduino IDE compiles them:
 *   alive.pb.h, alive.pb.c, pb_common.c, pb_decode.c, pb_encode.c   (nanopb)
 *   cobs.h, cobs.c                                                  (from Core/)
 *
 * Wiring (see firmware/WCL/README.md):
 *   STM32 PA9  (TX) -> ESP32 GPIO16 (RX2)
 *   STM32 PA10 (RX) <- ESP32 GPIO17 (TX2)
 *   Common GND. Power ESP32 from 5V/VIN, NOT the STM32 3.3V rail.
 */

#include <WiFi.h>
#include <PubSubClient.h>

extern "C" {
  #include "alive.pb.h"
  #include "pb_decode.h"
  #include "pb_encode.h"
  #include "cobs.h"
}

// ---- User config -----------------------------------------------------------
static const char* WIFI_SSID   = "YOUR_SSID";
static const char* WIFI_PASS   = "YOUR_PASSWORD";
static const char* MQTT_HOST   = "192.168.1.100";  // host PC running Mosquitto
static const uint16_t MQTT_PORT = 1883;

static const char* TOPIC_BIOMETRICS = "alive/biometrics";
static const char* TOPIC_HAPTICS    = "alive/haptics";

// ---- UART to STM32 ---------------------------------------------------------
// NOTE: ESP32-C6 has only 2 UARTs and NO Serial2. Use Serial1.
// GPIO16/17 are the UART0 console pins on the C6, so we use GPIO4/5 instead.
#define STM32_UART   Serial1
#define STM32_BAUD   921600
#define UART_RX_PIN  5    // ESP32 RX  <- STM32 PA9 (TX)
#define UART_TX_PIN  4    // ESP32 TX  -> STM32 PA10 (RX)

WiFiClient   net;
PubSubClient mqtt(net);

// COBS reassembly buffer for the incoming byte stream.
static uint8_t  rx_frame[cobs_encode_bufsize(alive_BiometricFrame_size) + 4];
static size_t   rx_len = 0;

// ---------------------------------------------------------------------------
static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(250); Serial.print("."); }
  Serial.printf(" connected: %s\n", WiFi.localIP().toString().c_str());
}

// Forward a haptic command from the broker down to the STM32.
static void onMqtt(char* topic, byte* payload, unsigned int len) {
  if (strcmp(topic, TOPIC_HAPTICS) != 0) return;

  alive_HapticCommand cmd = alive_HapticCommand_init_zero;
  pb_istream_t is = pb_istream_from_buffer(payload, len);
  if (!pb_decode(&is, alive_HapticCommand_fields, &cmd)) return;

  // Re-encode + COBS + deliver over UART to the STM32.
  uint8_t pb[alive_HapticCommand_size];
  pb_ostream_t os = pb_ostream_from_buffer(pb, sizeof(pb));
  if (!pb_encode(&os, alive_HapticCommand_fields, &cmd)) return;

  uint8_t framed[cobs_encode_bufsize(sizeof(pb)) + 1];
  size_t n = cobs_encode(pb, os.bytes_written, framed);
  framed[n++] = 0x00;
  STM32_UART.write(framed, n);
}

static void connectMqtt() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqtt);
  while (!mqtt.connected()) {
    if (mqtt.connect("alive-bau-bridge")) {
      mqtt.subscribe(TOPIC_HAPTICS, 1);   // QoS 1 for haptics
      Serial.println("MQTT connected");
    } else {
      Serial.printf("MQTT rc=%d, retry\n", mqtt.state());
      delay(1000);
    }
  }
}

// Decode one reassembled COBS block and publish it.
static void handleFrame(const uint8_t* cobs, size_t len) {
  uint8_t decoded[alive_BiometricFrame_size];
  size_t dlen = cobs_decode(cobs, len, decoded);
  if (dlen == 0) return;

  alive_BiometricFrame f = alive_BiometricFrame_init_zero;
  pb_istream_t is = pb_istream_from_buffer(decoded, dlen);
  if (!pb_decode(&is, alive_BiometricFrame_fields, &f)) return;

  // Republish the raw protobuf on the biometrics topic at QoS 0.
  mqtt.publish(TOPIC_BIOMETRICS, decoded, dlen, false);

  // Human-readable mirror for debugging.
  Serial.printf("seq=%u roll=%.1f pitch=%.1f yaw=%.1f imu=%d\n",
                f.seq, f.imu_roll, f.imu_pitch, f.imu_yaw, f.imu_valid);
}

// Pull bytes from the STM32, split on 0x00, hand complete blocks off.
static void pumpUart() {
  while (STM32_UART.available()) {
    uint8_t b = STM32_UART.read();
    if (b == 0x00) {
      if (rx_len > 0) handleFrame(rx_frame, rx_len);
      rx_len = 0;
    } else if (rx_len < sizeof(rx_frame)) {
      rx_frame[rx_len++] = b;
    } else {
      rx_len = 0;   // overflow, resync on next delimiter
    }
  }
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < 2000)) { delay(10); }

  Serial.println();
  Serial.println("ESP32-C6 ready, listening for STM32 frames");

  // Bring the UART up first so the STM32 link works even before WiFi/MQTT.
  STM32_UART.begin(STM32_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  connectWifi();   // blocks until associated - set WIFI_SSID/WIFI_PASS
  connectMqtt();   // blocks until broker reachable - set MQTT_HOST
}

void loop() {
  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();
  pumpUart();
}
