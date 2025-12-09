// ===== RC Menu + Channel Reverse + Endpoints + EEPROM (Serial test) =====

#include <EEPROM.h>

// Buttons
#define BUTTON_LEFT  2   // - or previous
#define BUTTON_RIGHT 3   // + or next
#define BUTTON_SEL   4   // select / enter
#define BUTTON_BACK  5   // back / exit

byte leftButtonState  = 7;
byte rightButtonState = 7;
byte selButtonState   = 7;
byte backButtonState  = 7;

// Debounced Rising Edge macro (DIY999 style)
#define DRE(signal, state) (state = ((state << 1) | (signal & 1)) & 15) == 7

// ---- Channels for reverse settings ----
const int NUM_CHANNELS = 4;
bool chReverse[NUM_CHANNELS] = {false, false, false, false};  // false=NORMAL, true=REVERSED

// ---- Endpoint settings (percent: 0..125) ----
uint8_t steerEP    = 100;   // steering endpoint %
uint8_t throttleEP = 100;   // throttle endpoint %

// ---- EEPROM config struct ----
struct Config {
  uint8_t magic;
  bool    chReverse[NUM_CHANNELS];
  uint8_t steerEP;
  uint8_t throttleEP;
};

const uint8_t CONFIG_MAGIC = 0x42;
const int EEPROM_ADDR = 0;

// ---- Menu state ----
bool inMenu = false;

// top-level menu items
const char* menuItems[] = {
  "Channel Reverse",
  "Steering End Point",
  "Throttle End Point",
  "Save & Exit"
};
const int MENU_COUNT = sizeof(menuItems) / sizeof(menuItems[0]);
int menuIndex = 0;

// menu levels / modes
enum MenuLevel {
  MENU_TOP,
  MENU_CH_REVERSE,
  MENU_EP_STEER,
  MENU_EP_THROTTLE
};

MenuLevel menuLevel = MENU_TOP;

// channel reverse submenu state
int chRevIndex = 0;  // which channel selected (0..NUM_CHANNELS-1)

// ---------- EEPROM helpers ----------
void loadConfig() {
  Config cfg;
  EEPROM.get(EEPROM_ADDR, cfg);

  if (cfg.magic != CONFIG_MAGIC) {
    // no valid config -> defaults
    Serial.println("EEPROM: No valid config, using defaults.");
    for (int i = 0; i < NUM_CHANNELS; i++) {
      chReverse[i] = false;
    }
    steerEP    = 100;
    throttleEP = 100;

    // save defaults
    cfg.magic = CONFIG_MAGIC;
    for (int i = 0; i < NUM_CHANNELS; i++) {
      cfg.chReverse[i] = chReverse[i];
    }
    cfg.steerEP    = steerEP;
    cfg.throttleEP = throttleEP;
    EEPROM.put(EEPROM_ADDR, cfg);
    Serial.println("EEPROM: Defaults saved.");
  } else {
    // load from EEPROM
    for (int i = 0; i < NUM_CHANNELS; i++) {
      chReverse[i] = cfg.chReverse[i];
    }
    steerEP    = cfg.steerEP;
    throttleEP = cfg.throttleEP;

    Serial.println("EEPROM: Config loaded.");
  }

  // Print loaded values
  Serial.println("Current config:");
  for (int i = 0; i < NUM_CHANNELS; i++) {
    Serial.print("  CH");
    Serial.print(i + 1);
    Serial.print(" reverse: ");
    Serial.println(chReverse[i] ? "REVERSED" : "NORMAL");
  }
  Serial.print("  Steering EP : ");
  Serial.print(steerEP);
  Serial.println("%");
  Serial.print("  Throttle EP : ");
  Serial.print(throttleEP);
  Serial.println("%");
}

void saveConfig() {
  Config cfg;
  cfg.magic = CONFIG_MAGIC;
  for (int i = 0; i < NUM_CHANNELS; i++) {
    cfg.chReverse[i] = chReverse[i];
  }
  cfg.steerEP    = steerEP;
  cfg.throttleEP = throttleEP;

  EEPROM.put(EEPROM_ADDR, cfg);
  Serial.println("\nEEPROM: Config saved.");
}

// ---------- Display helpers ----------
void showTopMenu() {
  Serial.print("> ");
  Serial.print(menuIndex + 1);
  Serial.print(". ");
  Serial.println(menuItems[menuIndex]);
}

void showChannelReverse() {
  Serial.print("\n[Channel Reverse]\n");
  Serial.print("Channel: CH");
  Serial.print(chRevIndex + 1);
  Serial.print("  State: ");
  Serial.println(chReverse[chRevIndex] ? "REVERSED" : "NORMAL");

  Serial.println("LEFT/RIGHT: change channel");
  Serial.println("SELECT    : toggle normal/reversed");
  Serial.println("BACK      : return to main menu");
}

void showSteerEP() {
  Serial.print("\n[Steering End Point]\n");
  Serial.print("Steering EP: ");
  Serial.print(steerEP);
  Serial.println("%");
  Serial.println("LEFT  : -1%");
  Serial.println("RIGHT : +1%");
  Serial.println("BACK  : return to main menu");
}

void showThrottleEP() {
  Serial.print("\n[Throttle End Point]\n");
  Serial.print("Throttle EP: ");
  Serial.print(throttleEP);
  Serial.println("%");
  Serial.println("LEFT  : -1%");
  Serial.println("RIGHT : +1%");
  Serial.println("BACK  : return to main menu");
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_LEFT,  INPUT_PULLUP);
  pinMode(BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(BUTTON_SEL,   INPUT_PULLUP);
  pinMode(BUTTON_BACK,  INPUT_PULLUP);

  loadConfig();

  Serial.println("\nSystem Ready.");
  Serial.println("Press SELECT to enter menu.");
}

// ---------- Loop ----------
void loop() {
  // Read buttons (invert because INPUT_PULLUP)
  bool leftPressed  = !digitalRead(BUTTON_LEFT);
  bool rightPressed = !digitalRead(BUTTON_RIGHT);
  bool selPressed   = !digitalRead(BUTTON_SEL);
  bool backPressed  = !digitalRead(BUTTON_BACK);

  if (!inMenu) {
    // --------- NOT in menu: only SELECT enters menu ---------
    if (DRE(selPressed, selButtonState)) {
      inMenu = true;
      menuLevel = MENU_TOP;
      menuIndex = 0;
      Serial.println("\n=== MENU ENTERED ===");
      showTopMenu();
    }
  } else {
    // --------- IN MENU ---------
    if (menuLevel == MENU_TOP) {

      // BACK: exit menu
      if (DRE(backPressed, backButtonState)) {
        inMenu = false;
        Serial.println("\n=== MENU EXITED ===");
      }

      // RIGHT: next item
      if (DRE(rightPressed, rightButtonState)) {
        menuIndex++;
        if (menuIndex >= MENU_COUNT) menuIndex = 0;
        showTopMenu();
      }

      // LEFT: previous item
      if (DRE(leftPressed, leftButtonState)) {
        menuIndex--;
        if (menuIndex < 0) menuIndex = MENU_COUNT - 1;
        showTopMenu();
      }

      // SELECT: enter submenu / action
      if (DRE(selPressed, selButtonState)) {
        if (menuIndex == 0) {
          // Channel Reverse submenu
          menuLevel = MENU_CH_REVERSE;
          chRevIndex = 0;
          showChannelReverse();
        } else if (menuIndex == 1) {
          // Steering EP submenu
          menuLevel = MENU_EP_STEER;
          showSteerEP();
        } else if (menuIndex == 2) {
          // Throttle EP submenu
          menuLevel = MENU_EP_THROTTLE;
          showThrottleEP();
        } else if (menuIndex == 3) {
          // Save & Exit
          saveConfig();
          inMenu = false;
          Serial.println("=== MENU EXITED ===");
        }
      }

    } else if (menuLevel == MENU_CH_REVERSE) {
      // --------- CHANNEL REVERSE SUBMENU ---------

      // BACK → return to top menu
      if (DRE(backPressed, backButtonState)) {
        menuLevel = MENU_TOP;
        Serial.println("\nBack to MAIN MENU");
        showTopMenu();
      }

      // RIGHT → next channel
      if (DRE(rightPressed, rightButtonState)) {
        chRevIndex++;
        if (chRevIndex >= NUM_CHANNELS) chRevIndex = 0;
        showChannelReverse();
      }

      // LEFT → previous channel
      if (DRE(leftPressed, leftButtonState)) {
        chRevIndex--;
        if (chRevIndex < 0) chRevIndex = NUM_CHANNELS - 1;
        showChannelReverse();
      }

      // SELECT → toggle NORMAL/REVERSED
      if (DRE(selPressed, selButtonState)) {
        chReverse[chRevIndex] = !chReverse[chRevIndex];
        showChannelReverse();
      }

    } else if (menuLevel == MENU_EP_STEER) {
      // --------- STEERING EP SUBMENU ---------

      // BACK → return to top menu
      if (DRE(backPressed, backButtonState)) {
        menuLevel = MENU_TOP;
        Serial.println("\nBack to MAIN MENU");
        showTopMenu();
      }

      // LEFT → -1%
      if (DRE(leftPressed, leftButtonState)) {
        if (steerEP > 0) steerEP--;
        showSteerEP();
      }

      // RIGHT → +1%
      if (DRE(rightPressed, rightButtonState)) {
        if (steerEP < 125) steerEP++;
        showSteerEP();
      }

    } else if (menuLevel == MENU_EP_THROTTLE) {
      // --------- THROTTLE EP SUBMENU ---------

      // BACK → return to top menu
      if (DRE(backPressed, backButtonState)) {
        menuLevel = MENU_TOP;
        Serial.println("\nBack to MAIN MENU");
        showTopMenu();
      }

      // LEFT → -1%
      if (DRE(leftPressed, leftButtonState)) {
        if (throttleEP > 0) throttleEP--;
        showThrottleEP();
      }

      // RIGHT → +1%
      if (DRE(rightPressed, rightButtonState)) {
        if (throttleEP < 125) throttleEP++;
        showThrottleEP();
      }
    }
  }

  // No delay() needed – DRE + loop speed handles debounce
}
