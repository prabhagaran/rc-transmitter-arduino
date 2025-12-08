// Transmitter_sample.ino
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

#define CE_PIN 7
#define CSN_PIN 8

RF24 radio(CE_PIN, CSN_PIN);
const uint64_t pipeAddress = 0xF0F0F0F0E1LL; // 5-byte address (common pattern)

struct Telemetry {
  uint8_t addr;       // vehicle/address id
  int16_t throttle;   // -1000..1000 mapped
  int16_t steer;      // -1000..1000 mapped
  uint8_t buttons;    // bit mask of buttons
};

Telemetry data;

void setup() {
  Serial.begin(115200);
  radio.begin();
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.enableDynamicPayloads();
  radio.setRetries(5, 15); // delay, count
  radio.openWritingPipe(pipeAddress);
  radio.stopListening();

  // initial sample values
  data.addr = 1;
  data.throttle = 255;
  data.steer = 255;
  data.buttons = 1;

  Serial.println("TX ready");
}

unsigned long lastMillis = 0;
void loop() {
  // sample/modify data here. For demo we'll send a changing throttle value
  unsigned long t = millis();
  if (t - lastMillis >= 200) { // send 5Hz
    lastMillis = t;
    // simple demo pattern
    data.throttle += 100;
    if (data.throttle > 1000) data.throttle = -1000;

    bool ok = radio.write(&data, sizeof(data));
    if (ok) {
      Serial.print("Sent OK: addr=");
      Serial.print(data.addr);
      Serial.print(" thr=");
      Serial.print(data.throttle);
      Serial.print(" steer=");
      Serial.print(data.steer);
      Serial.print(" btn=");
      Serial.println(data.buttons, BIN);
    } else {
      Serial.println("Send FAILED");
    }
  }
}
