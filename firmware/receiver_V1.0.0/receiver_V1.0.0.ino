// ==== Uncomment to enable debugging on Serial ====
 #define DEBUG

// RX_with_ack + Servo Output + Failsafe

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <stddef.h>
#include <Servo.h>

#define CE_PIN 7
#define CSN_PIN 8
RF24 radio(CE_PIN, CSN_PIN);
const uint64_t pipeAddress = 0xF0F0F0F0E1LL;

#define MAX_CHANNELS 8

// ----- Servo Pins -----
#define SERVO0_PIN 3
#define SERVO1_PIN 5
#define SERVO2_PIN 6
#define SERVO3_PIN 9

Servo servo0, servo1, servo2, servo3;

// ----- Failsafe config -----
const unsigned long FAILSAFE_TIMEOUT = 300;   // ms without packets -> failsafe
unsigned long lastPacketTime = 0;
bool failsafeActive = false;

// Failsafe channel values (-1000..+1000)
const int16_t FS_CH0 = 0;     // e.g. steering center
const int16_t FS_CH1 = 0;     // e.g. throttle neutral
const int16_t FS_CH2 = 0;
const int16_t FS_CH3 = 0;

struct Packet {
  uint8_t version;
  uint8_t device_id;
  uint8_t channel_count;
  int16_t channels[MAX_CHANNELS]; // -1000..+1000
  uint16_t crc;
} __attribute__((packed));

struct TelemetryAck {
  uint16_t seq;
  uint16_t battery_mV;
  uint8_t  rssi_est;
  uint8_t  status;   // bit0=link OK, bit1=failsafe
} __attribute__((packed));

Packet pktRecv;
TelemetryAck ackOut;

uint16_t seq_counter = 0;

// ----- CRC function -----
uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= (uint16_t)(*data++) << 8;
    for (uint8_t i = 0; i < 8; i++) {
      if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
      else crc <<= 1;
    }
  }
  return crc;
}

// ----- Convert -1000..+1000 → 1000–2000us -----
int rcToUs(int16_t v) {
  if (v < -1000) v = -1000;
  if (v >  1000) v = 1000;
  return 1500 + (v * 500 / 1000);
}

uint8_t measure_link_quality_estimate() { return 200; }
uint16_t measure_battery_mV() { return 7400; }

// ----- Apply failsafe positions -----
void applyFailsafe() {
  failsafeActive = true;

  servo0.writeMicroseconds(rcToUs(FS_CH0));
  servo1.writeMicroseconds(rcToUs(FS_CH1));
  servo2.writeMicroseconds(rcToUs(FS_CH2));
  servo3.writeMicroseconds(rcToUs(FS_CH3));

#ifdef DEBUG
  Serial.println("** FAILSAFE ACTIVE: no packets **");
#endif
}

// ================== SETUP ==================
void setup() {
#ifdef DEBUG
  Serial.begin(115200);
  Serial.println("Receiver starting...");
#endif

  // Attach servos
  servo0.attach(SERVO0_PIN);
  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  servo3.attach(SERVO3_PIN);

  // Start centered
  servo0.writeMicroseconds(1500);
  servo1.writeMicroseconds(1500);
  servo2.writeMicroseconds(1500);
  servo3.writeMicroseconds(1500);

  // nRF24 init
  radio.begin();
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.enableDynamicPayloads();
  radio.enableAckPayload();
  radio.setRetries(5, 15);
  radio.openReadingPipe(0, pipeAddress);
  radio.startListening();

  ackOut.seq = 0;
  ackOut.battery_mV = 0;
  ackOut.rssi_est = 0;
  ackOut.status = 0;

  lastPacketTime = millis();

#ifdef DEBUG
  Serial.println("RX ready.");
#endif
}

// ================== LOOP ==================
void loop() {
  // 1) Handle incoming packets (if any)
  if (radio.available()) {
    uint8_t pipeNum;
    radio.available(&pipeNum);
    uint8_t len = radio.getDynamicPayloadSize();

    if (len > sizeof(Packet)) {
      uint8_t dump[len];
      radio.read(dump, len);
#ifdef DEBUG
      Serial.print("RX: Oversized payload ");
      Serial.println(len);
#endif
    } else {
      memset(&pktRecv, 0, sizeof(pktRecv));
      radio.read(&pktRecv, len);

      size_t crc_offset = offsetof(Packet, crc);
      size_t compute_len = min(crc_offset, (size_t)len);
      uint8_t *raw = (uint8_t*)&pktRecv;

      uint16_t computed_crc = crc16_ccitt(raw, compute_len);

      uint16_t received_crc = 0xFFFF;
      if (len >= crc_offset + 2) {
        received_crc = raw[crc_offset] | (raw[crc_offset + 1] << 8);
      }

      bool crc_ok = (computed_crc == received_crc);

#ifdef DEBUG
      Serial.print("RX len="); Serial.print(len);
      Serial.print(" CRC=0x"); Serial.print(received_crc, HEX);
      Serial.print(" calc=0x"); Serial.print(computed_crc, HEX);
      Serial.print(" -> ");
      Serial.println(crc_ok ? "OK" : "BAD");
#endif

      if (crc_ok) {
        // Valid packet -> update lastPacketTime, clear failsafe
        lastPacketTime = millis();
        failsafeActive = false;

#ifdef DEBUG
        Serial.print("Channels: ");
        for (int i = 0; i < pktRecv.channel_count; i++) {
          Serial.print(pktRecv.channels[i]);
          Serial.print(i < pktRecv.channel_count - 1 ? ", " : "");
        }
        Serial.println();
#endif

        // Drive servos from channels
        int16_t ch0 = pktRecv.channels[0];
        int16_t ch1 = pktRecv.channels[1];
        int16_t ch2 = pktRecv.channels[2];
        int16_t ch3 = pktRecv.channels[3];

        servo0.writeMicroseconds(rcToUs(ch0));
        servo1.writeMicroseconds(rcToUs(ch1));
        servo2.writeMicroseconds(rcToUs(ch2));
        servo3.writeMicroseconds(rcToUs(ch3));

        // Prepare ACK telemetry
        ackOut.seq        = seq_counter++;
        ackOut.battery_mV = measure_battery_mV();
        ackOut.rssi_est   = measure_link_quality_estimate();

        // status: bit0 = link OK, bit1 = failsafe
        ackOut.status = 0;
        ackOut.status |= (1 << 0);            // link OK
        if (failsafeActive) ackOut.status |= (1 << 1);

        uint8_t p = (pipeNum <= 5) ? pipeNum : 0;
        radio.writeAckPayload(p, &ackOut, sizeof(ackOut));

#ifdef DEBUG
        Serial.print("ACK sent seq=");
        Serial.print(ackOut.seq);
        Serial.print(" status=0x");
        Serial.println(ackOut.status, HEX);
#endif
      }
    }
  }

  // 2) Check for failsafe timeout
  if (!failsafeActive && (millis() - lastPacketTime > FAILSAFE_TIMEOUT)) {
    applyFailsafe();
    // ackOut.status will be set with failsafe flag next time a packet arrives (or you can also
    // send some standalone telemetry if you design it that way)
  }
}
