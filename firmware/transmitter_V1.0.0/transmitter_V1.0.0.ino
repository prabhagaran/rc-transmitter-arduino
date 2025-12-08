// TX_with_ack.ino
// Transmitter: sends structured packet and reads ACK payload telemetry from receiver.

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <stddef.h>

#define CE_PIN 7
#define CSN_PIN 8
RF24 radio(CE_PIN, CSN_PIN);
const uint64_t pipeAddress = 0xF0F0F0F0E1LL;
#define MAX_CHANNELS 8

// Packet sent from TX -> RX
struct Packet {
  uint8_t version;
  uint8_t device_id;
  uint8_t channel_count;
  int16_t channels[MAX_CHANNELS];
  uint16_t crc;
} __attribute__((packed));

Packet pkt;

// Telemetry ack sent RX -> TX (ACK payload)
struct TelemetryAck {
  uint16_t seq;        // sequence number from receiver side
  uint16_t battery_mV; // battery voltage in mV
  uint8_t rssi_est;    // crude RSSI estimate (0-255)
  uint8_t status;      // status flags (bitmask)
} __attribute__((packed));

TelemetryAck ackBuf;

// CRC-16-CCITT (0x1021), init 0xFFFF
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

unsigned long lastSend = 0;
int demoTick = 0;
uint16_t ackCount = 0;

void setup() {
  Serial.begin(115200);
  radio.begin();
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.enableDynamicPayloads();
  radio.enableAckPayload();    // enable ACK payload functionality
  radio.setRetries(5, 15);
  radio.openWritingPipe(pipeAddress);
  radio.stopListening();

  // init packet
  pkt.version = 1;
  pkt.device_id = 1;
  pkt.channel_count = 4;
  for (int i = 0; i < MAX_CHANNELS; ++i) pkt.channels[i] = 0;

  Serial.println("TX w/ ACK ready");
}

void loop() {
  unsigned long now = millis();
  if (now - lastSend >= 200) { // 5Hz
    lastSend = now;
    // demo channel values
    pkt.channels[0] = (int16_t)(sin(demoTick * 0.2) * 1000);
    pkt.channels[1] = (int16_t)(cos(demoTick * 0.25) * 1000);
    pkt.channels[2] = (int16_t)((demoTick % 200) - 100);
    pkt.channels[3] = 0;

    demoTick++;

    // compute CRC over bytes before crc field
    uint8_t *raw = (uint8_t*)&pkt;
    size_t crc_offset = offsetof(Packet, crc);
    pkt.crc = crc16_ccitt(raw, crc_offset);

    // send packet
    radio.stopListening();
    bool ok = radio.write(&pkt, sizeof(Packet));
    if (!ok) {
      Serial.println("TX: send FAILED");
    } else {
      Serial.print("TX: sent seq?");
      Serial.print(" id=");
      Serial.print(pkt.device_id);
      Serial.print(" ch0=");
      Serial.print(pkt.channels[0]);
      Serial.print(" CRC=0x");
      Serial.print(pkt.crc, HEX);
      Serial.print("  -- waiting for ACK...");
      // check for ACK payload
      // short delay to allow ack payload to be received by radio hardware
      // note: this is not blocking on the radio; isAckPayloadAvailable checks the radio FIFO
      delay(5);
      if (radio.isAckPayloadAvailable()) {
        // read ack
        radio.read(&ackBuf, sizeof(TelemetryAck));
        Serial.print(" ACK rcvd: seq=");
        Serial.print(ackBuf.seq);
        Serial.print(" batt=");
        Serial.print(ackBuf.battery_mV);
        Serial.print("mV rssi=");
        Serial.print(ackBuf.rssi_est);
        Serial.print(" status=0x");
        Serial.println(ackBuf.status, HEX);
        ackCount++;
      } else {
        Serial.println(" no ACK payload");
      }
    }
  }
}
