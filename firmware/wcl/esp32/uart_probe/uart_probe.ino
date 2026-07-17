/**
 * ALIVE WCL - UART probe (ESP32-C6)
 * ---------------------------------
 * Minimal bring-up test. NO WiFi, NO MQTT, NO protobuf. Its only job is to
 * prove that bytes from the STM32 actually arrive on the ESP32's UART.
 *
 * Once you see hex bytes scrolling here, the physical link + baud are good and
 * you can move on to the full wcl_esp32_bridge sketch.
 *
 * Board:  "ESP32C6 Dev Module"
 * Tools:  USB CDC On Boot -> ENABLED   (required, or Serial prints nothing)
 *
 * Wiring (TX must cross to RX):
 *   STM32 PA9  (TX) --> ESP32 GPIO5  (RX1)
 *   STM32 PA10 (RX) <-- ESP32 GPIO4  (TX1)
 *   GND <-> GND   (mandatory common ground)
 */

#define STM32_UART   Serial1     // C6 has NO Serial2 - use Serial1
#define STM32_BAUD   921600      // must match wcl.c WCL_UART_BAUD
#define UART_RX_PIN  5           // ESP32 receives on this pin (<- STM32 PA9)
#define UART_TX_PIN  4           // ESP32 transmits on this pin (-> STM32 PA10)

uint32_t total = 0;             // running count of received bytes

void setup() {
  Serial.begin(115200);
  // Give the USB CDC link a moment to enumerate before the first print.
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < 2000)) { delay(10); }

  Serial.println();
  Serial.println("ESP32-C6 ready, listening for STM32 frames");
  Serial.printf("UART1: RX=GPIO%d TX=GPIO%d @ %d baud\n",
                UART_RX_PIN, UART_TX_PIN, STM32_BAUD);

  STM32_UART.begin(STM32_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
}

void loop() {
  // Heartbeat so you know the sketch is alive even if no UART data arrives.
  static uint32_t last = 0;
  if (millis() - last > 2000) {
    last = millis();
    Serial.printf("[alive] rx bytes so far: %lu\n", (unsigned long)total);
  }

  // Dump every received byte as hex. A COBS frame ends in 0x00, so you should
  // see runs of bytes terminated by "00".
  while (STM32_UART.available()) {
    uint8_t b = STM32_UART.read();
    total++;
    Serial.printf("%02X ", b);
    if (b == 0x00) Serial.println("  <-- frame end");
  }
}
