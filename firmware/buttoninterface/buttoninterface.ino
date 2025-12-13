// ===== TX with Model Memory (menu + EEPROM) =====
// Upload & open Serial Monitor @ 115200 to test menu/model memory

#include <EEPROM.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <stddef.h>

// ---------------- hardware / rf / joystick ----------------
#define CE_PIN 7
#define CSN_PIN 8
RF24 radio(CE_PIN, CSN_PIN);
const uint64_t pipeAddress = 0xF0F0F0F0E1LL;

const byte NUM_CH = 4;
const byte chPins[NUM_CH] = {A0, A1, A2, A3};
int rawValue[NUM_CH];
int filteredValue[NUM_CH];

// ---------------- buttons ----------------
#define BUTTON_LEFT  2
#define BUTTON_RIGHT 3
#define BUTTON_SEL   4
#define BUTTON_BACK  5

byte leftButtonState  = 7;
byte rightButtonState = 7;
byte selButtonState   = 7;
byte backButtonState  = 7;

// Debounced Rising Edge
#define DRE(signal, state) (state = ((state << 1) | (signal & 1)) & 15) == 7

// ---------------- model memory config ----------------
const int NUM_MODELS = 4;
const int NAME_LEN = 8; // model name length

const int MAX_CHANNELS = 8; // packet size

struct Model {
  bool    chReverse[NUM_CH];
  uint8_t chEP[NUM_CH];
  int8_t  chSubtrim[NUM_CH];
  char    name[NAME_LEN + 1];
};

struct FullConfig {
  uint8_t magic;
  uint8_t activeModel;      // index 0..NUM_MODELS-1
  Model   models[NUM_MODELS];
};

const uint8_t CONFIG_MAGIC = 0x5A;
const int EEPROM_ADDR = 0;

// runtime active settings (loaded from active model)
bool    chReverse[NUM_CH];
uint8_t chEP[NUM_CH];
int8_t  chSubtrim[NUM_CH];

FullConfig cfg; // in-memory full config

// ---------------- menu state ----------------
bool inMenu = false;
int menuIndex = 0;
int chIndex = 0;
int modelIndex = 0;

const char* menuItems[] = {
  "Model Select",
  "Channel Reverse",
  "End Points",
  "Subtrim",
  "Save & Exit"
};
const int MENU_COUNT = sizeof(menuItems) / sizeof(menuItems[0]);

enum MenuLevel {
  MENU_TOP,
  MENU_MODEL_LIST,
  MENU_CH_REVERSE,
  MENU_EP_LIST,
  MENU_EP_EDIT,
  MENU_SUBTRIM_LIST,
  MENU_SUBTRIM_EDIT
};
MenuLevel menuLevel = MENU_TOP;

// ---------------- Packet ----------------
struct Packet {
  uint8_t  version;
  uint8_t  device_id;
  uint8_t  channel_count;
  int16_t  channels[MAX_CHANNELS];
  uint16_t crc;
} __attribute__((packed));

Packet pkt;

// ---------------- CRC helper ----------------
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

// ---------------- EEPROM / config helpers ----------------
void createDefaults() {
  cfg.magic = CONFIG_MAGIC;
  cfg.activeModel = 0;
  for (int m = 0; m < NUM_MODELS; ++m) {
    for (int c = 0; c < NUM_CH; ++c) {
      cfg.models[m].chReverse[c] = false;
      cfg.models[m].chEP[c] = 100;
      cfg.models[m].chSubtrim[c] = 0;
    }
    snprintf(cfg.models[m].name, NAME_LEN + 1, "MODEL%d", m + 1);
  }
  EEPROM.put(EEPROM_ADDR, cfg);
  Serial.println("EEPROM: defaults created");
}

void loadFullConfig() {
  EEPROM.get(EEPROM_ADDR, cfg);
  if (cfg.magic != CONFIG_MAGIC) {
    createDefaults();
  } else {
    // valid config loaded
    Serial.println("EEPROM: config loaded");
  }
  // load active model settings into runtime arrays
  uint8_t a = cfg.activeModel;
  if (a >= NUM_MODELS) a = 0;
  for (int c = 0; c < NUM_CH; ++c) {
    chReverse[c] = cfg.models[a].chReverse[c];
    chEP[c]      = cfg.models[a].chEP[c];
    chSubtrim[c] = cfg.models[a].chSubtrim[c];
  }
  modelIndex = a;
}

void saveFullConfig() {
  cfg.magic = CONFIG_MAGIC;
  EEPROM.put(EEPROM_ADDR, cfg);
  Serial.println("EEPROM: full config saved");
}

// copy runtime settings into the model slot (overwrite model)
void saveCurrentToModel(int m) {
  if (m < 0 || m >= NUM_MODELS) return;
  for (int c = 0; c < NUM_CH; ++c) {
    cfg.models[m].chReverse[c] = chReverse[c];
    cfg.models[m].chEP[c]      = chEP[c];
    cfg.models[m].chSubtrim[c] = chSubtrim[c];
  }
  Serial.print("Current settings saved to ");
  Serial.println(cfg.models[m].name);
}

// load model m into runtime (and set activeModel)
void loadModelToRuntime(int m) {
  if (m < 0 || m >= NUM_MODELS) return;
  cfg.activeModel = m;
  for (int c = 0; c < NUM_CH; ++c) {
    chReverse[c] = cfg.models[m].chReverse[c];
    chEP[c]      = cfg.models[m].chEP[c];
    chSubtrim[c] = cfg.models[m].chSubtrim[c];
  }
  modelIndex = m;
  Serial.print("Loaded model: ");
  Serial.println(cfg.models[m].name);
}

// ---------------- menu display ----------------
void showTopMenu() {
  Serial.print("> ");
  Serial.print(menuIndex + 1);
  Serial.print(". ");
  Serial.println(menuItems[menuIndex]);
}

void showModelList() {
  Serial.println("\n[Model Select]");
  for (int i = 0; i < NUM_MODELS; ++i) {
    Serial.print(i == modelIndex ? "> " : "  ");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(cfg.models[i].name);
    if (i == cfg.activeModel) Serial.print(" (active)");
    Serial.println();
  }
  Serial.println("SELECT: Load model  RIGHT: Save current -> model  BACK: main");
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

// ---------------- joystick read & mapping ----------------
void readJoysticks() {
  for (int ch = 0; ch < NUM_CH; ch++) {
    int v = analogRead(chPins[ch]);
    rawValue[ch] = v;
    filteredValue[ch] = (filteredValue[ch] * 3 + v) / 4;
  }
}

int mapJoystick(int ch) {
  const int ADC_MID = 512;
  int x = filteredValue[ch] - ADC_MID;
  const int dead = 20;
  if (x > -dead && x < dead) x = 0;
  const int range = 400;
  if (x > range) x = range;
  if (x < -range) x = -range;
  long out = (long)x * 1000 / range;
  return (int)out;
}

// ---------------- TX loop sending ----------------
unsigned long lastSend = 0;
void sendChannels() {
  readJoysticks();
  for (int ch = 0; ch < NUM_CH; ++ch) {
    int v = mapJoystick(ch);   // -1000..+1000
    // apply subtrim (small center offset)
    v += chSubtrim[ch] * 10;
    // apply reverse
    if (chReverse[ch]) v = -v;
    // apply endpoint scaling
    v = (v * chEP[ch]) / 100;
    // clamp
    if (v > 1000) v = 1000;
    if (v < -1000) v = -1000;
    pkt.channels[ch] = (int16_t)v;
  }
  for (int i = NUM_CH; i < MAX_CHANNELS; ++i) pkt.channels[i] = 0;
  uint8_t *raw = (uint8_t*)&pkt;
  size_t crcOffset = offsetof(Packet, crc);
  pkt.crc = crc16_ccitt(raw, crcOffset);
  radio.stopListening();
  radio.write(&pkt, sizeof(Packet));
}

// debug print
void printChannels() {
  Serial.print("CH: ");
  for (int i = 0; i < NUM_CH; ++i) {
    Serial.print(pkt.channels[i]);
    Serial.print('\t');
  }
  Serial.println();
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  Serial.println("TX w/ Model Memory starting...");

  pinMode(BUTTON_LEFT, INPUT_PULLUP);
  pinMode(BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(BUTTON_SEL, INPUT_PULLUP);
  pinMode(BUTTON_BACK, INPUT_PULLUP);

  loadFullConfig();            // loads cfg + copies active model to runtime

  // nRF init
  radio.begin();
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.enableDynamicPayloads();
  radio.enableAckPayload();
  radio.setRetries(5, 15);
  radio.openWritingPipe(pipeAddress);
  radio.stopListening();

  pkt.version = 1;
  pkt.device_id = 1;
  pkt.channel_count = NUM_CH;

  // init smoothing
  for (int ch = 0; ch < NUM_CH; ++ch) {
    int sum = 0;
    for (int i = 0; i < 10; ++i) { sum += analogRead(chPins[ch]); delay(2); }
    filteredValue[ch] = sum / 10;
  }

  Serial.println("Ready. Press SELECT to open menu.");
}

// ---------------- Loop ----------------
void loop() {
  // read buttons (active LOW)
  bool L = !digitalRead(BUTTON_LEFT);
  bool R = !digitalRead(BUTTON_RIGHT);
  bool S = !digitalRead(BUTTON_SEL);
  bool B = !digitalRead(BUTTON_BACK);

  // when not in menu, send channels at 50Hz and allow entering menu
  if (!inMenu) {
    if (DRE(S, selButtonState)) {
      inMenu = true; menuIndex = 0; menuLevel = MENU_TOP;
      Serial.println("\n=== MENU ==="); showTopMenu();
      return;
    }
    if (millis() - lastSend >= 20) { lastSend = millis(); sendChannels(); }
    return;
  }

  // ---------- IN MENU ----------
  if (menuLevel == MENU_TOP) {
    if (DRE(B, backButtonState)) { inMenu = false; Serial.println("Exit menu"); }
    if (DRE(R, rightButtonState)) { menuIndex = (menuIndex + 1) % MENU_COUNT; showTopMenu(); }
    if (DRE(L, leftButtonState))  { menuIndex = (menuIndex + MENU_COUNT - 1) % MENU_COUNT; showTopMenu(); }

    if (DRE(S, selButtonState)) {
      chIndex = 0;
      if (menuIndex == 0) { menuLevel = MENU_MODEL_LIST; showModelList(); }
      else if (menuIndex == 1) { menuLevel = MENU_CH_REVERSE; showChReverseList(); }
      else if (menuIndex == 2) { menuLevel = MENU_EP_LIST; showEPList(); }
      else if (menuIndex == 3) { menuLevel = MENU_SUBTRIM_LIST; showSubtrimList(); }
      else { // Save & Exit -> write full configuration to EEPROM and remain in main menu
        saveFullConfig();
        menuLevel = MENU_TOP; menuIndex = 0;
        Serial.println("\nSaved. Back to MAIN MENU"); showTopMenu();
      }
    }
  }

  // ---------- MODEL LIST ----------
  else if (menuLevel == MENU_MODEL_LIST) {
    if (DRE(B, backButtonState)) { menuLevel = MENU_TOP; showTopMenu(); }
    if (DRE(R, rightButtonState)) { modelIndex = (modelIndex + 1) % NUM_MODELS; showModelList(); }
    if (DRE(L, leftButtonState))  { modelIndex = (modelIndex + NUM_MODELS - 1) % NUM_MODELS; showModelList(); }

    // SELECT -> load model into runtime & set active
    if (DRE(S, selButtonState)) {
      loadModelToRuntime(modelIndex);
      showModelList();
    }

    // RIGHT button also used as 'Save current runtime to this model' when held quickly:
    // we used RIGHT to step; but to save we also allow long save via pressing RIGHT twice.
    // For simplicity: use a RIGHT press when already selected -> save current runtime to model
    // (Implementation: if S pressed on the selected model we already loaded; use a special combo? Simpler:)
    // We'll use RIGHT while modelIndex == cfg.activeModel as "save current". For clarity, also allow RIGHT+SELECT combination later.
    // Here we also support 'press RIGHT while holding SELECT' is impractical; instead allow RIGHT when modelIndex is selected again to save.
    // For a simple explicit action: if user presses RIGHT and waits 300ms and presses it again -> not implemented.
    // So we'll implement: LONG PRESS not implemented. Instead we use a second-button mapping: when on a model, pressing RIGHT will cycle, pressing LEFT cycles; pressing SELECT loads; pressing RIGHT twice quickly saves.
    // To avoid complexity we add explicit 'Save current to model' via pressing RIGHT while holding SELECT is messy. So add alternative: pressing RIGHT and then SELECT will save. For simplicity now, implement: press RIGHT to move; press SELECT to load. To save current settings into the model press and hold BACK then press SELECT (not ideal).
    // To keep things simple and deterministic, implement: Press RIGHT while on a model -> next model. Press SELECT -> loads. Press BACK -> main.
    // Provide a separate quick "Save current to model" via menu: user can go to Model Select and then press RIGHT to Save (we will detect a second quick press) -- But to avoid complexity we implement a dedicated button action below:
    // If user presses RIGHT + SELECT simultaneously, we will save current runtime to the selected model.
    // Implement combo: if R and S both true at same loop -> saveCurrentToModel(modelIndex).
    if (R && S) { // simultaneous RIGHT+SELECT -> save current runtime into selected model
      saveCurrentToModel(modelIndex);
      // update cfg.activeModel to this model to reflect user's choice
      cfg.activeModel = modelIndex;
      showModelList();
    }
  }

  // ---------- CHANNEL REVERSE ----------
  else if (menuLevel == MENU_CH_REVERSE) {
    if (DRE(B, backButtonState)) { menuLevel = MENU_TOP; showTopMenu(); }
    if (DRE(R, rightButtonState)) { chIndex = (chIndex + 1) % NUM_CH; showChReverseList(); }
    if (DRE(L, leftButtonState))  { chIndex = (chIndex + NUM_CH - 1) % NUM_CH; showChReverseList(); }
    if (DRE(S, selButtonState))   { chReverse[chIndex] = !chReverse[chIndex]; showChReverseList(); }
  }

  // ---------- EP LIST ----------
  else if (menuLevel == MENU_EP_LIST) {
    if (DRE(B, backButtonState)) { menuLevel = MENU_TOP; showTopMenu(); }
    if (DRE(R, rightButtonState)) { chIndex = (chIndex + 1) % NUM_CH; showEPList(); }
    if (DRE(L, leftButtonState))  { chIndex = (chIndex + NUM_CH - 1) % NUM_CH; showEPList(); }
    if (DRE(S, selButtonState))   { menuLevel = MENU_EP_EDIT; showEPEdit(); }
  }

  // ---------- EP EDIT ----------
  else if (menuLevel == MENU_EP_EDIT) {
    if (DRE(B, backButtonState)) { menuLevel = MENU_EP_LIST; showEPList(); }
    if (DRE(R, rightButtonState)) { if (chEP[chIndex] < 125) chEP[chIndex]++; showEPEdit(); }
    if (DRE(L, leftButtonState))  { if (chEP[chIndex] > 0) chEP[chIndex]--; showEPEdit(); }
  }

  // ---------- SUBTRIM LIST ----------
  else if (menuLevel == MENU_SUBTRIM_LIST) {
    if (DRE(B, backButtonState)) { menuLevel = MENU_TOP; showTopMenu(); }
    if (DRE(R, rightButtonState)) { chIndex = (chIndex + 1) % NUM_CH; showSubtrimList(); }
    if (DRE(L, leftButtonState))  { chIndex = (chIndex + NUM_CH - 1) % NUM_CH; showSubtrimList(); }
    if (DRE(S, selButtonState))   { menuLevel = MENU_SUBTRIM_EDIT; showSubtrimEdit(); }
  }

  // ---------- SUBTRIM EDIT ----------
  else if (menuLevel == MENU_SUBTRIM_EDIT) {
    if (DRE(B, backButtonState)) { menuLevel = MENU_SUBTRIM_LIST; showSubtrimList(); }
    if (DRE(R, rightButtonState)) { if (chSubtrim[chIndex] < 100) chSubtrim[chIndex]++; showSubtrimEdit(); }
    if (DRE(L, leftButtonState))  { if (chSubtrim[chIndex] > -100) chSubtrim[chIndex]--; showSubtrimEdit(); }
  }
}
