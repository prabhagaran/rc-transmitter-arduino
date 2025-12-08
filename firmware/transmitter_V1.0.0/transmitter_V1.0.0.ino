// TX_packet_crc.ino
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <stddef.h> // for offsetof

#define CE_PIN 7
#define CSN_PIN 8

RF24 radio(CE_PIN, CSN_PIN);
const uint64_t pipeAddress = 0xF0F0F0F0E1LL;
#define MAX_CHANNELS 8

// Packed packet definition
struct Packet {
  uint8_t version;              // protocol version
  uint8_t device_id;            // transmitter device id / model id
  uint8_t channel_count;        // number of valid channels (n <= MAX_CHANNELS)
  int16_t channels[MAX_CHANNELS]; // channel values, -1000 .. 1000 (unused slots ignored)
  uint16_t crc;                 // CRC-16-CCITT over all preceding bytes
} __attribute__((packed));

Packet pkt;

// CRC-16-CCITT implementation (poly 0x1021, init 0xFFFF)
uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= (uint16_t)(*data++) << 8;
    for (uint8_t i = 0; i < 8; ++i) {
      if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
      else crc <<= 1;
    }
  }
  return crc;
}

void setup() {
  Serial.begin(115200);
  radio.begin();
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.enableDynamicPayloads();
  radio.setRetries(5, 15);
  radio.openWritingPipe(pipeAddress);
  radio.stopListening();

  // initial packet meta
  pkt.version = 1;
  pkt.device_id = 1;      // change per-transmitter
  pkt.channel_count = 4;  // we'll use 4 channels for this demo

  // initialize channels (center = 0)
  for (int i = 0; i < MAX_CHANNELS; ++i) pkt.channels[i] = 0;

  Serial.println("TX (structured packet) ready");
}

unsigned long lastSend = 0;
int demoTick = 0;

void loop() {
  unsigned long now = millis();
  if (now - lastSend >= 200) { // 5Hz
    lastSend = now;

    // Example: produce some test patterns on channels
    pkt.channels[0] = (int16_t)((sin(demoTick * 0.2) * 1000)); // throttle-like
    pkt.channels[1] = (int16_t)((cos(demoTick * 0.25) * 1000)); // steer-like
    pkt.channels[2] = (int16_t)((demoTick % 200) - 100); // small movement
    pkt.channels[3] = 0; // reserved

    // update demo counter
    demoTick++;

    // compute CRC across packet up to (but not including) crc field
    uint8_t *raw = (uint8_t*)&pkt;
    size_t crc_offset = offsetof(Packet, crc);
    uint16_t c = crc16_ccitt(raw, crc_offset);
    pkt.crc = c; // store CRC in packet (little-endian on AVR; receiver will treat same)

    // send
    bool ok = radio.write(&pkt, sizeof(Packet));
    if (ok) {
      Serial.print("Sent v");
      Serial.print(pkt.version);
      Serial.print(" id=");
      Serial.print(pkt.device_id);
      Serial.print(" ch=");
      Serial.print(pkt.channel_count);
      Serial.print(" -> ");
      for (int i = 0; i < pkt.channel_count; ++i) {
        Serial.print(pkt.channels[i]);
        if (i < pkt.channel_count - 1) Serial.print(", ");
      }
      Serial.print("  CRC=0x");
      Serial.println(pkt.crc, HEX);
    } else {
      Serial.println("Send failed");
    }
  }
}
