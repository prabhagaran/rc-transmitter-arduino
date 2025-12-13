// ==== Uncomment to enable RF debug on Serial ====
// #define DEBUG


#include <SPI.h>
#include <nRF24L01.h>

12345678911121314151617181920212223242526272810
// ---------------- nRF24 config ----------------
#define CE_PIN 7
#define CSN_PIN 8
RF24 radio(CE_PIN, CSN_PIN);
const uint64_t pipeAddress = 0xF0F0F0F0E1LL;

#define MAX_CHANNELS 8

// ---------------- Joystick config ----------------
const byte NUM_CH = 4; // 4 channels A0-A3


#include <RF24.h>
#include <stddef.h>
#include <EEPROM.h>


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

// ---------------- Buttons ----------------
#define BUTTON_LEFT  2
#define BUTTON_RIGHT 3
#define BUTTON_SEL   4
#define BUTTON_BACK  5

byte leftButtonState  = 7;
byte rightButtonState = 7;
byte selButtonState   = 7;
byte backButtonState  = 7;

// Debounced Rising Edge (DIYguy style)
#define DRE(signal, state) (state = ((state << 1) | (signal & 1)) & 15) == 7

// ---------------- Channel configuration (MENU + EEPROM) ----------------
bool    chReverse[NUM_CH] = {false, false, false, false}; // false=NORMAL, true=REVERSED
uint8_t chEP[NUM_CH]      = {100, 100, 100, 100};         // 0..125 (%)
int8_t  chSubtrim[NUM_CH] = {0, 0, 0, 0};                 // -100..+100 (small center offset)

// EEPROM config struct
struct Config {
  uint8_t magic;
  bool    chReverse[NUM_CH];
  uint8_t chEP[NUM_CH];
  int8_t  chSubtrim[NUM_CH];
};

const uint8_t CONFIG_MAGIC = 0x42;
const int EEPROM_ADDR = 0;

// ---------------- Menu state ----------------
bool inMenu = false;
int  menuIndex = 0;
int  chIndex   = 0;

const char* menuItems[] = {
  "Channel Reverse",
  "End Points",
  "Subtrim",
  "Save & Exit"
};
const int MENU_COUNT = sizeof(menuItems) / sizeof(menuItems[0]);

enum MenuLevel {
  MENU_TOP,
  MENU_CH_REVERSE,
  MENU_EP_LIST,
  MENU_EP_EDIT,
  MENU_SUBTRIM_LIST,
  MENU_SUBTRIM_EDIT
};

MenuLevel menuLevel = MENU_TOP;

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

// ---------------- EEPROM helpers ----------------
void loadConfig() {
  Config cfg;
  EEPROM.get(EEPROM_ADDR, cfg);

  if (cfg.magic != CONFIG_MAGIC) {
    // defaults
    for (int i = 0; i < NUM_CH; i++) {
      chReverse[i]  = false;
      chEP[i]       = 100;
      chSubtrim[i]  = 0;
    }
    cfg.magic = CONFIG_MAGIC;
    memcpy(cfg.chReverse, chReverse, sizeof(chReverse));
    memcpy(cfg.chEP,      chEP,      sizeof(chEP));
    memcpy(cfg.chSubtrim, chSubtrim, sizeof(chSubtrim));
    EEPROM.put(EEPROM_ADDR, cfg);
    Serial.println("EEPROM: defaults created");
  } else {
    memcpy(chReverse, cfg.chReverse, sizeof(chReverse));
    memcpy(chEP,      cfg.chEP,      sizeof(chEP));
    memcpy(chSubtrim, cfg.chSubtrim, sizeof(chSubtrim));
    Serial.println("EEPROM: config loaded");
  }
}

void saveConfig() {
  Config cfg;
  cfg.magic = CONFIG_MAGIC;
  memcpy(cfg.chReverse, chReverse, sizeof(chReverse));
  memcpy(cfg.chEP,      chEP,      sizeof(chEP));
  memcpy(cfg.chSubtrim, chSubtrim, sizeof(chSubtrim));
  EEPROM.put(EEPROM_ADDR, cfg);
  Serial.println("Saved to EEPROM");
}

// ---------------- Menu display helpers ----------------
void showTopMenu() {
  Serial.print("> ");
  Serial.print(menuIndex + 1);
  Serial.print(". ");
  Serial.println(menuItems[menuIndex]);
}

void showChReverseList() {
  Serial.println("\n[Channel Reverse]");
  for (int i = 0; i < NUM_CH; i++) {
    Serial.print(i == chIndex ? "> " : "  ");
    Serial.print("CH");
    Serial.print(i + 1);
    Serial.print(" : ");
    Serial.println(chReverse[i] ? "REVERSED" : "NORMAL");
  }
  Serial.println("LEFT/RIGHT: select channel");
  Serial.println("SELECT    : toggle");
  Serial.println("BACK      : main menu");
}

void showEPList() {
  Serial.println("\n[End Points]");
  for (int i = 0; i < NUM_CH; i++) {
    Serial.print(i == chIndex ? "> " : "  ");
    Serial.print("CH");
    Serial.print(i + 1);
    Serial.print(" EP : ");
    Serial.print(chEP[i]);
    Serial.println("%");
  }
  Serial.println("LEFT/RIGHT: select channel");
  Serial.println("SELECT    : edit EP");
  Serial.println("BACK      : main menu");
}

void showEPEdit() {
  Serial.print("\n[Edit CH");
  Serial.print(chIndex + 1);
  Serial.println(" EP]");
  Serial.print("Value: ");
  Serial.print(chEP[chIndex]);
  Serial.println("%");
  Serial.println("LEFT:-  RIGHT:+  BACK:Done");
}

void showSubtrimList() {
  Serial.println("\n[Subtrim]");
  for (int i = 0; i < NUM_CH; i++) {
    Serial.print(i == chIndex ? "> " : "  ");
    Serial.print("CH");
    Serial.print(i + 1);
    Serial.print(" : ");
    Serial.println(chSubtrim[i]);
  }
  Serial.println("LEFT/RIGHT: select");
  Serial.println("SELECT    : edit");
  Serial.println("BACK      : main menu");
}

void showSubtrimEdit() {
  Serial.print("\n[Edit CH");
  Serial.print(chIndex + 1);
  Serial.println(" Subtrim]");
  Serial.print("Value: ");
  Serial.println(chSubtrim[chIndex]);
  Serial.println("LEFT:-  RIGHT:+  BACK:Done");
}

// ---------------- Joystick reading ----------------
void readJoysticks() {
  for (int ch = 0; ch < NUM_CH; ch++) {
    int v = analogRead(chPins[ch]);
    rawValue[ch] = v;
    // Faster response, less smoothing
    filteredValue[ch] = (filteredValue[ch] * 3 + v) / 4;
  }
}

// Map joystick channel to -1000..+1000
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

// Debug channels
void printChannels() {
#ifdef DEBUG
  Serial.print("CH: ");
  for (int ch = 0; ch < NUM_CH; ch++) {
    Serial.print(pkt.channels[ch]);
    Serial.print('\t');
  }
  Serial.println();
#endif
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Serial.println("TX starting...");

  pinMode(BUTTON_LEFT,  INPUT_PULLUP);
  pinMode(BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(BUTTON_SEL,   INPUT_PULLUP);
  pinMode(BUTTON_BACK,  INPUT_PULLUP);

  loadConfig();

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

  Serial.println("TX ready. Press SELECT to open menu.");
}

// ================= LOOP =================
void loop() {
  // Read buttons (active LOW)
  bool L = !digitalRead(BUTTON_LEFT);
  bool R = !digitalRead(BUTTON_RIGHT);
  bool S = !digitalRead(BUTTON_SEL);
  bool B = !digitalRead(BUTTON_BACK);

  // ---------- NOT IN MENU: normal TX + possible menu enter ----------
  if (!inMenu) {
    // Check if user wants to enter menu
    if (DRE(S, selButtonState)) {
      inMenu    = true;
      menuIndex = 0;
      menuLevel = MENU_TOP;
      Serial.println("\n=== MENU ===");
      showTopMenu();
      return;  // skip TX this loop
    }

    // Normal TX operation
    if (millis() - lastSend >= 20) {
      lastSend = millis();

      readJoysticks();

      for (int ch = 0; ch < NUM_CH; ch++) {
        int v = mapJoystick(ch);   // -1000..+1000

        // ---- SUBTRIM ----
        v += chSubtrim[ch] * 10;   // each step ≈ 1% of full range

        // ---- REVERSE ----
        if (chReverse[ch]) v = -v;

        // ---- END POINT ----
        v = (v * chEP[ch]) / 100;

        // ---- CLAMP ----
        if (v >  1000) v =  1000;
        if (v < -1000) v = -1000;

        pkt.channels[ch] = (int16_t)v;
      }

      // Zero unused channels
      for (int i = NUM_CH; i < MAX_CHANNELS; i++) pkt.channels[i] = 0;

      // CRC
      uint8_t *raw = (uint8_t*)&pkt;
      size_t crcOffset = offsetof(Packet, crc);
      pkt.crc = crc16_ccitt(raw, crcOffset);

      // Send packet
      radio.stopListening();
      bool ok = radio.write(&pkt, sizeof(Packet));

#ifdef DEBUG
      Serial.print("TX: ");
      Serial.println(ok ? "OK" : "FAIL");
      delay(3);
      if (radio.isAckPayloadAvailable()) {
        radio.read(&ackBuf, sizeof(TelemetryAck));
        Serial.print("ACK batt=");
        Serial.print(ackBuf.battery_mV);
        Serial.print(" rssi=");
        Serial.println(ackBuf.rssi_est);
      }
      printChannels();
#endif
    }

    return; // done for this loop
  }

  // ---------- IN MENU: handle menu logic, no TX updates ----------

  // TOP MENU
  if (menuLevel == MENU_TOP) {
    if (DRE(B, backButtonState)) {
      inMenu = false;
      Serial.println("Exit menu");
    }

    if (DRE(R, rightButtonState)) {
      menuIndex = (menuIndex + 1) % MENU_COUNT;
      showTopMenu();
    }

    if (DRE(L, leftButtonState)) {
      menuIndex = (menuIndex + MENU_COUNT - 1) % MENU_COUNT;
      showTopMenu();
    }

    if (DRE(S, selButtonState)) {
      chIndex = 0;
      if (menuIndex == 0) {
        menuLevel = MENU_CH_REVERSE;
        showChReverseList();
      } 
      else if (menuIndex == 1) {
        menuLevel = MENU_EP_LIST;
        showEPList();
      } 
      else if (menuIndex == 2) {
        menuLevel = MENU_SUBTRIM_LIST;
        showSubtrimList();
      }
      else {
        // Save & Exit → Save & stay in main menu
        saveConfig();
        menuLevel = MENU_TOP;
        menuIndex = 0;
        Serial.println("\nSaved. Back to MAIN MENU");
        showTopMenu();
      }
    }
  }

  // CHANNEL REVERSE LIST
  else if (menuLevel == MENU_CH_REVERSE) {
    if (DRE(B, backButtonState)) {
      menuLevel = MENU_TOP;
      showTopMenu();
    }

    if (DRE(R, rightButtonState)) {
      chIndex = (chIndex + 1) % NUM_CH;
      showChReverseList();
    }

    if (DRE(L, leftButtonState)) {
      chIndex = (chIndex + NUM_CH - 1) % NUM_CH;
      showChReverseList();
    }

    if (DRE(S, selButtonState)) {
      chReverse[chIndex] = !chReverse[chIndex];
      showChReverseList();
    }
  }

  // END POINT LIST
  else if (menuLevel == MENU_EP_LIST) {
    if (DRE(B, backButtonState)) {
      menuLevel = MENU_TOP;
      showTopMenu();
    }

    if (DRE(R, rightButtonState)) {
      chIndex = (chIndex + 1) % NUM_CH;
      showEPList();
    }

    if (DRE(L, leftButtonState)) {
      chIndex = (chIndex + NUM_CH - 1) % NUM_CH;
      showEPList();
    }

    if (DRE(S, selButtonState)) {
      menuLevel = MENU_EP_EDIT;
      showEPEdit();
    }
  }

  // END POINT EDIT
  else if (menuLevel == MENU_EP_EDIT) {
    if (DRE(B, backButtonState)) {
      menuLevel = MENU_EP_LIST;
      showEPList();
    }

    if (DRE(R, rightButtonState)) {
      if (chEP[chIndex] < 125) chEP[chIndex]++;
      showEPEdit();
    }

    if (DRE(L, leftButtonState)) {
      if (chEP[chIndex] > 0) chEP[chIndex]--;
      showEPEdit();
    }
  }

  // SUBTRIM LIST
  else if (menuLevel == MENU_SUBTRIM_LIST) {
    if (DRE(B, backButtonState)) {
      menuLevel = MENU_TOP;
      showTopMenu();
    }

    if (DRE(R, rightButtonState)) {
      chIndex = (chIndex + 1) % NUM_CH;
      showSubtrimList();
    }

    if (DRE(L, leftButtonState)) {
      chIndex = (chIndex + NUM_CH - 1) % NUM_CH;
      showSubtrimList();
    }

    if (DRE(S, selButtonState)) {
      menuLevel = MENU_SUBTRIM_EDIT;
      showSubtrimEdit();
    }
  }

  // SUBTRIM EDIT
  else if (menuLevel == MENU_SUBTRIM_EDIT) {
    if (DRE(B, backButtonState)) {
      menuLevel = MENU_SUBTRIM_LIST;
      showSubtrimList();
    }

    if (DRE(R, rightButtonState)) {
      if (chSubtrim[chIndex] < 100) chSubtrim[chIndex]++;
      showSubtrimEdit();
    }

    if (DRE(L, leftButtonState)) {
      if (chSubtrim[chIndex] > -100) chSubtrim[chIndex]--;
      showSubtrimEdit();
    }
  }
}
