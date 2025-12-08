// ==== Uncomment to enable debugging on Serial ====
// #define DEBUG

// TX_with_ack_joystick.ino
// Transmitter: reads 4 joystick channels, sends packet, receives ACK.

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <stddef.h>

// ---------------- nRF24 config ----------------
#define CE_PIN 7
#define CSN_PIN 8
RF24 radio(CE_PIN, CSN_PIN);
const uint64_t pipeAddress = 0xF0F0F0F0E1LL;

#define MAX_CHANNELS 8

// ---------------- Joystick config ----------------
const byte NUM_CH = 4; // 4 channels A0-A3
const byte chPins[NUM_CH] = {A0, A1, A2, A3};

int rawValue[NUM_CH];
int filteredValue[NUM_CH];

// ===== Packet format =====
struct Packet {
  uint8_t  version;
  uint8_t  device_id;
  uint8_t  channel_count;
  int16_t  channels[MAX_CHANNELS];
  uint16_t crc;
} __attribute__((packed));

Packet pkt;

// ===== ACK payload structure =====
struct TelemetryAck {
  uint16_t seq;
  uint16_t battery_mV;
  uint8_t  rssi_est;
  uint8_t  status;
} __attribute__((packed));

TelemetryAck ackBuf;

// ===== CRC =====
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

// -------- Read & smooth joystick --------
void readJoysticks() {
  for (int ch = 0; ch < NUM_CH; ch++) {
    int v = analogRead(chPins[ch]);
    rawValue[ch] = v;

    // Faster response, less smoothing
    filteredValue[ch] = (filteredValue[ch] * 3 + v) / 4;
  }
}

// -------- Map one channel to -1000..+1000 --------
int mapJoystick(int ch) {
  const int ADC_MID = 512;
  int x = filteredValue[ch] - ADC_MID;

  const int dead = 20;
  if (x > -dead && x < dead) x = 0;

  const int range = 400;
  if (x >  range) x =  range;
  if (x < -range) x = -range;

  long out = (long)x * 1000 / range;
  return (int)out;
}

// -------- Debug function --------
void printChannels() {
#ifdef DEBUG
  Serial.print("RAW: ");
  for (int ch = 0; ch < NUM_CH; ch++) {
    Serial.print(rawValue[ch]);
    Serial.print('\t');
  }

  Serial.print(" | VAL: ");
  for (int ch = 0; ch < NUM_CH; ch++) {
    Serial.print(mapJoystick(ch));
    Serial.print('\t');
  }
  Serial.println();
#endif
}

// ================= SETUP =================
void setup() {
#ifdef DEBUG
  Serial.begin(115200);
  Serial.println("TX starting...");
#endif

  // nRF24 init
  radio.begin();
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.enableDynamicPayloads();
  radio.enableAckPayload();
  radio.setRetries(5, 15);
  radio.openWritingPipe(pipeAddress);
  radio.stopListening();

  // packet init
  pkt.version = 1;
  pkt.device_id = 1;
  pkt.channel_count = NUM_CH;

  // startup joystick smoothing
  for (int ch = 0; ch < NUM_CH; ch++) {
    int sum = 0;
    for (int i = 0; i < 10; i++) {
      sum += analogRead(chPins[ch]);
      delay(2);
    }
    filteredValue[ch] = sum / 10;
  }

#ifdef DEBUG
  Serial.println("TX ready.");
#endif
}

// ================= LOOP =================
void loop() {

  // Send every 20ms (50Hz)
  if (millis() - lastSend >= 20) {
    lastSend = millis();

    // Read joysticks
    readJoysticks();

    // Fill packet channels
    for (int ch = 0; ch < NUM_CH; ch++) {
      pkt.channels[ch] = mapJoystick(ch);
    }

    // Zero remaining unused channels
    for (int i = NUM_CH; i < MAX_CHANNELS; i++) pkt.channels[i] = 0;

    // Compute CRC
    uint8_t *raw = (uint8_t*)&pkt;
    size_t crcOffset = offsetof(Packet, crc);
    pkt.crc = crc16_ccitt(raw, crcOffset);

    // Send packet
    radio.stopListening();
    bool ok = radio.write(&pkt, sizeof(Packet));

#ifdef DEBUG
    Serial.print("TX result: ");
    Serial.println(ok ? "OK" : "FAIL");
#endif

    // Give radio time to receive ACK
    delay(3);

    // Check ACK payload
#ifdef DEBUG
    if (radio.isAckPayloadAvailable()) {
      radio.read(&ackBuf, sizeof(TelemetryAck));
      Serial.print("ACK: seq=");
      Serial.print(ackBuf.seq);
      Serial.print(" batt=");
      Serial.print(ackBuf.battery_mV);
      Serial.print(" rssi=");
      Serial.println(ackBuf.rssi_est);
    } else {
      Serial.println("No ACK");
    }
#endif

    printChannels();
  }
}
