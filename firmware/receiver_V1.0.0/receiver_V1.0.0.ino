// RX_with_ack.ino
// Receiver: validates packet, then sends ACK telemetry payload back to transmitter.

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <stddef.h>

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

struct TelemetryAck {
  uint16_t seq;
  uint16_t battery_mV;
  uint8_t rssi_est;
  uint8_t status;
} __attribute__((packed));

Packet pktRecv;
TelemetryAck ackOut;

uint16_t seq_counter = 0;

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

void setup() {
  Serial.begin(115200);
  radio.begin();
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.enableDynamicPayloads();
  radio.enableAckPayload(); // allow writing ack payloads
  radio.setRetries(5, 15);
  radio.openReadingPipe(0, pipeAddress);
  radio.startListening();

  // init ack structure
  ackOut.seq = 0;
  ackOut.battery_mV = 0;
  ackOut.rssi_est = 0;
  ackOut.status = 0;

  Serial.println("RX w/ ACK ready");
}

uint8_t measure_link_quality_estimate() {
  // crude placeholder. RF24 doesn't expose hardware RSSI.
  // You can infer quality from observed packet timing, LQI heuristics or ACK success rates.
  // For demo, return 200 (good).
  return 200;
}

uint16_t measure_battery_mV() {
  // Demo: Simulate reading battery voltage via an analog pin if available.
  // Replace this with real ADC reading and scaling as needed.
  // Example: return 7400 for 7.4V pack
  // If you have a voltage divider connect to A0 and compute real voltage.
  return 7400; // simulated 7.4V
}

void loop() {
  if (radio.available()) {
    uint8_t pipeNum;
    // read pipe number that has data (optional)
    bool got = radio.available(&pipeNum);
    uint8_t len = radio.getDynamicPayloadSize();
    if (len > sizeof(Packet)) {
      // discard if too large
      uint8_t dump[len];
      radio.read(dump, len);
      Serial.print("RX: payload too big (");
      Serial.print(len);
      Serial.println("). Discard.");
      return;
    }

    // read payload into pktRecv (zero rest)
    memset(&pktRecv, 0, sizeof(pktRecv));
    radio.read(&pktRecv, len);

    // compute CRC over bytes before crc field (up to offsetof(Packet, crc))
    size_t crc_offset = offsetof(Packet, crc);
    size_t compute_len = min(crc_offset, (size_t)len);
    uint8_t *raw = (uint8_t*)&pktRecv;
    uint16_t computed = crc16_ccitt(raw, compute_len);

    // extract received CRC if present
    uint16_t received_crc = 0xFFFF;
    if (len >= (int)(crc_offset + sizeof(uint16_t))) {
      received_crc = (uint16_t)(raw[crc_offset] | (raw[crc_offset + 1] << 8)); // little-endian
    }

    bool crc_ok = (received_crc == computed);

    Serial.print("RX len=");
    Serial.print(len);
    Serial.print(" v=");
    Serial.print(pktRecv.version);
    Serial.print(" id=");
    Serial.print(pktRecv.device_id);
    Serial.print(" ch=");
    Serial.print(pktRecv.channel_count);
    Serial.print(" CRCrcv=0x");
    Serial.print(received_crc, HEX);
    Serial.print(" CRCcalc=0x");
    Serial.print(computed, HEX);
    Serial.print(" -> ");
    Serial.println(crc_ok ? "OK" : "BAD");

    if (crc_ok) {
      // (Optional) do something with channels here (map to servos)
      int cc = pktRecv.channel_count;
      if (cc > MAX_CHANNELS) cc = MAX_CHANNELS;
      Serial.print("Channels: ");
      for (int i = 0; i < cc; ++i) {
        Serial.print(pktRecv.channels[i]);
        if (i < cc - 1) Serial.print(", ");
      }
      Serial.println();

      // Prepare ACK telemetry payload
      ackOut.seq = seq_counter++;
      ackOut.battery_mV = measure_battery_mV();
      ackOut.rssi_est = measure_link_quality_estimate();
      ackOut.status = 0; // flags: bit0=OK, bit1=LOWBATT, etc. set as needed

      // Write ACK payload back to transmitter on the same pipe we received from.
      // We retrieved 'pipeNum' above from radio.available(&pipeNum).
      // Use that pipe number for writeAckPayload.
      // If pipeNum is out of range, default to pipe 0.
      uint8_t p = (pipeNum <= 5) ? pipeNum : 0;
      bool wrote = radio.writeAckPayload(p, &ackOut, sizeof(ackOut));
      if (!wrote) {
        Serial.println("Failed to queue ACK payload");
      } else {
        Serial.print("Queued ACK: seq=");
        Serial.print(ackOut.seq);
        Serial.print(" batt=");
        Serial.print(ackOut.battery_mV);
        Serial.print(" rssi=");
        Serial.println(ackOut.rssi_est);
      }
    } else {
      Serial.println("Corrupted packet: dropping");
      // Optionally you can still reply with NACK-like smaller payload via dynamic ack or flags
    }
  }
}
