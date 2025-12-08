// Receiver_sample.ino
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

#define CE_PIN 7
#define CSN_PIN 8

RF24 radio(CE_PIN, CSN_PIN);
const uint64_t pipeAddress = 0xF0F0F0F0E1LL;

struct Telemetry {
  uint8_t addr;
  int16_t throttle;
  int16_t steer;
  uint8_t buttons;
};

Telemetry recvData;

void setup() {
  Serial.begin(115200);
  radio.begin();
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.enableDynamicPayloads();
  radio.setRetries(5, 15);
  radio.openReadingPipe(0, pipeAddress);
  radio.startListening();
  Serial.println("RX ready, listening...");
}

void loop() {
  if (radio.available()) {
    while (radio.available()) {
      uint8_t len = radio.getDynamicPayloadSize();
      radio.read(&recvData, min((size_t)len, sizeof(recvData)));
    }
    Serial.print("Got: addr=");
    Serial.print(recvData.addr);
    Serial.print(" thr=");
    Serial.print(recvData.throttle);
    Serial.print(" steer=");
    Serial.print(recvData.steer);
    Serial.print(" btn=");
    Serial.println(recvData.buttons, BIN);

    // Example: respond by toggling an LED or send telemetry back (not implemented)
  }
}
