// ===== RC Menu + Reverse + EP + Subtrim + EEPROM =====

#include <EEPROM.h>

// Buttons
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

// ---- Channels ----
const int NUM_CHANNELS = 4;
bool    chReverse[NUM_CHANNELS] = {false, false, false, false};
uint8_t chEP[NUM_CHANNELS]      = {100, 100, 100, 100};
int8_t  chSubtrim[NUM_CHANNELS] = {0, 0, 0, 0};   // NEW

// ---- EEPROM ----
struct Config {
  uint8_t magic;
  bool    chReverse[NUM_CHANNELS];
  uint8_t chEP[NUM_CHANNELS];
  int8_t  chSubtrim[NUM_CHANNELS];
};

const uint8_t CONFIG_MAGIC = 0x42;
const int EEPROM_ADDR = 0;

// ---- Menu ----
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

// ---------- EEPROM ----------
void loadConfig() {
  Config cfg;
  EEPROM.get(EEPROM_ADDR, cfg);

  if (cfg.magic != CONFIG_MAGIC) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
      chReverse[i] = false;
      chEP[i] = 100;
      chSubtrim[i] = 0;
    }
    cfg.magic = CONFIG_MAGIC;
    memcpy(cfg.chReverse, chReverse, sizeof(chReverse));
    memcpy(cfg.chEP, chEP, sizeof(chEP));
    memcpy(cfg.chSubtrim, chSubtrim, sizeof(chSubtrim));
    EEPROM.put(EEPROM_ADDR, cfg);
    Serial.println("EEPROM: defaults created");
  } else {
    memcpy(chReverse, cfg.chReverse, sizeof(chReverse));
    memcpy(chEP, cfg.chEP, sizeof(chEP));
    memcpy(chSubtrim, cfg.chSubtrim, sizeof(chSubtrim));
    Serial.println("EEPROM: config loaded");
  }
}

void saveConfig() {
  Config cfg;
  cfg.magic = CONFIG_MAGIC;
  memcpy(cfg.chReverse, chReverse, sizeof(chReverse));
  memcpy(cfg.chEP, chEP, sizeof(chEP));
  memcpy(cfg.chSubtrim, chSubtrim, sizeof(chSubtrim));
  EEPROM.put(EEPROM_ADDR, cfg);
  Serial.println("Saved to EEPROM");
}

// ---------- Display ----------
void showTopMenu() {
  Serial.print("> ");
  Serial.print(menuIndex + 1);
  Serial.print(". ");
  Serial.println(menuItems[menuIndex]);
}

void showSubtrimList() {
  Serial.println("\n[Subtrim]");
  for (int i = 0; i < NUM_CHANNELS; i++) {
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

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_LEFT, INPUT_PULLUP);
  pinMode(BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(BUTTON_SEL, INPUT_PULLUP);
  pinMode(BUTTON_BACK, INPUT_PULLUP);

  loadConfig();
  Serial.println("Press SELECT to enter menu");
}

// ---------- Loop ----------
void loop() {
  bool L = !digitalRead(BUTTON_LEFT);
  bool R = !digitalRead(BUTTON_RIGHT);
  bool S = !digitalRead(BUTTON_SEL);
  bool B = !digitalRead(BUTTON_BACK);

  if (!inMenu) {
    if (DRE(S, selButtonState)) {
      inMenu = true;
      menuIndex = 0;
      menuLevel = MENU_TOP;
      Serial.println("\n=== MENU ===");
      showTopMenu();
    }
    return;
  }

  // ---------- TOP MENU ----------
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
      if (menuIndex == 2) {
        menuLevel = MENU_SUBTRIM_LIST;
        showSubtrimList();
      } else if (menuIndex == 3) {
        saveConfig();
        menuIndex = 0;
        showTopMenu();
      }
    }
  }

  // ---------- SUBTRIM LIST ----------
  else if (menuLevel == MENU_SUBTRIM_LIST) {
    if (DRE(B, backButtonState)) {
      menuLevel = MENU_TOP;
      showTopMenu();
    }
    if (DRE(R, rightButtonState)) {
      chIndex = (chIndex + 1) % NUM_CHANNELS;
      showSubtrimList();
    }
    if (DRE(L, leftButtonState)) {
      chIndex = (chIndex + NUM_CHANNELS - 1) % NUM_CHANNELS;
      showSubtrimList();
    }
    if (DRE(S, selButtonState)) {
      menuLevel = MENU_SUBTRIM_EDIT;
      showSubtrimEdit();
    }
  }

  // ---------- SUBTRIM EDIT ----------
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
