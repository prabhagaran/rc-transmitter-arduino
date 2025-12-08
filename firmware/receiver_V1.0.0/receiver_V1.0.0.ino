// RX_packet_crc.ino
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <stddef.h> // for offsetof

#define CE_PIN 7
#define CSN_PIN 8

RF24 radio(CE_PIN, CSN_PIN);
const uint64_t pipeAddress = 0xF0F0F0F0E1LL;
#define MAX_CHANNELS 8

struct Packet {
  uint8_t version;
  uint8_t device_id;
  uint8_t channel_count;
  int16_t channels[MAX_CHANNELS];
  uint16_t crc;
} __attribute__((packed));

Packet pktRecv;

// CRC-16-CCITT (same as TX)
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
  radio.openReadingPipe(0, pipeAddress);
  radio.startListening();

  Serial.println("RX (structured packet) ready");
}

void loop() {
  if (radio.available()) {
    // read dynamic payload (could be whole Packet)
    uint8_t len = radio.getDynamicPayloadSize();
    if (len > sizeof(Packet)) {
      // Too large: discard or read min
      Serial.print("Payload too big (");
      Serial.print(len);
      Serial.println("). Discarding.");
      uint8_t dump[len];
      radio.read(dump, len);
      return;
    }

    // Read into our struct buffer
    radio.read(&pktRecv, len);

    // For CRC we need to compute over the bytes before crc field.
    size_t crc_offset = offsetof(Packet, crc);
    // However payload may be smaller (if sender used fewer bytes). Ensure we only compute over what's present
    size_t compute_len = min(crc_offset, (size_t)len); // bytes available before CRC
    uint8_t *raw = (uint8_t*)&pktRecv;
    uint16_t computed = crc16_ccitt(raw, compute_len);

    // Extract received CRC if payload contains it
    uint16_t received_crc = 0;
    if (len >= (int)(crc_offset + sizeof(uint16_t))) {
      // CRC bytes are present at end of payload; read safely (platform endian matches between TX/RX)
      // copy two bytes to avoid unaligned issues
      uint8_t b0 = raw[crc_offset];
      uint8_t b1 = raw[crc_offset + 1];
      received_crc = (uint16_t)(b0 | (b1 << 8)); // little-endian AVR ordering
    } else {
      // No CRC field present
      received_crc = 0xFFFF; // mark as invalid
    }

    // Validate CRC
    bool crc_ok = (received_crc == computed);

    Serial.print("RX len=");
    Serial.print(len);
    Serial.print(" v=");
    Serial.print(pktRecv.version);
    Serial.print(" id=");
    Serial.print(pktRecv.device_id);
    Serial.print(" ch=");
    Serial.print(pktRecv.channel_count);
    Serial.print("  CRCrecv=0x");
    Serial.print(received_crc, HEX);
    Serial.print(" CRCcalc=0x");
    Serial.print(computed, HEX);
    Serial.print(" -> ");
    Serial.println(crc_ok ? "OK" : "BAD");

    if (crc_ok) {
      // print channels (only up to channel_count and within bounds)
      int cc = pktRecv.channel_count;
      if (cc > MAX_CHANNELS) cc = MAX_CHANNELS;
      Serial.print("Channels: ");
      for (int i = 0; i < cc; ++i) {
        Serial.print(pktRecv.channels[i]);
        if (i < cc - 1) Serial.print(", ");
      }
      Serial.println();
      // TODO: map to servos / ESC outputs
    } else {
      // Optionally ignore corrupted packet
      Serial.println("Dropping corrupted packet.");
    }
  }
}
