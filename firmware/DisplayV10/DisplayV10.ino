#include <EEPROM.h>
#include <SPI.h>
#include <Wire.h>
#include "RF24.h"  //Download and Install (See above)
#include <Adafruit_GFX.h>
#include <Adafruit_SH1106.h>
//#include "readVCC.h"

//***enable it for debug
//#define DEBUG

// buttons for menu select button , back button, inc button dec button
#define SEL_BUTTON 3 // select buttom similar to ok
#define BACK_BUTTON 10 // back button
#define INC_BUTTON 2 // Increment button
#define DEC_BUTTON 9 // Decrement button


// Joysticks
#define JOYSTICK_1 A1
#define JOYSTICK_2 A0
#define JOYSTICK_3 A3
#define JOYSTICK_4 A2




#define  CE_PIN  7 //The pins to be used for CE and CSN
#define  CSN_PIN 8

const uint64_t pipeOut[] PROGMEM = {
  0xE9E8F0F0B1LL, 0xE9E8F0F0B2LL, 0xE9E8F0F0B3LL, 0xE9E8F0F0B4LL, 0xE9E8F0F0B5LL,
  0xE9E8F0F0B6LL, 0xE9E8F0F0B7LL, 0xE9E8F0F0B8LL, 0xE9E8F0F0B9LL, 0xE9E8F0F0B0LL,
  0xE9E8F0F0C1LL, 0xE9E8F0F0C2LL, 0xE9E8F0F0C3LL, 0xE9E8F0F0C4LL, 0xE9E8F0F0C5LL,
  0xE9E8F0F0C6LL, 0xE9E8F0F0C7LL, 0xE9E8F0F0C8LL, 0xE9E8F0F0C9LL, 0xE9E8F0F0C0LL
};

const int maxVehicleNumber = (sizeof(pipeOut) / (sizeof(uint64_t)));

uint64_t pgm_read_64(const void *ptr, uint8_t index) {
  uint64_t result;
  memcpy_P( &result, (uint8_t*)ptr + (index * 8), sizeof(uint64_t) ); // ptr is counting in bytes!
  return result;
}

RF24 radio(CE_PIN, CSN_PIN);

// macro function for button debouncing
#define DRE(signal, state) (state=(state<<1)|(signal&1)&15)==7


// button state
uint8_t buttonState1 = 0;
uint8_t buttonState2 = 0;
uint8_t buttonState3 = 0;
uint8_t buttonState4 = 0;

// display screen state for menu
uint8_t activeScreen = 0;
uint8_t count = 1;
bool menuActive = false;
uint8_t menuRow = 1;
uint8_t submenuRow = 1;
uint8_t incDec = 1;
uint8_t option = 0;
bool submenu = false;
uint8_t axis1 = 0;
uint8_t chOption = 0;
bool ch1PN = false;
int vehicleNumber = 1; // Vehicle number one is active by default
float txVcc;
float txBatt;

const float diodeDrop = 0.72;
const float cutoffVoltage = 4.4;
boolean batteryOkTx = false;

// Smoothing constants
const float alpha = 0.2; // Adjust this value for the desired smoothing level (0.0 to 1.0)

// Variables to store raw, smoothed, and neutral joystick values
int rawValues[4];
int smoothedValues[4];
int offsetValues[4];


byte chREV_PVE_[4][3] = {
  {0, 0, 0},
  {0, 0, 0},
  {0, 0, 0},
  {0, 0, 0},
};
byte chREV_NVE_[4][3] = {
  {0, 0, 0},
  {0, 0, 0},
  {0, 0, 0},
  {0, 0, 0},
};

const int EEPROM_INDEX_PVE = 0;
const int EEPROM_INDEX_NVE = EEPROM_INDEX_PVE + sizeof(chREV_PVE_);
const int EEPROM_SIZE = EEPROM_INDEX_NVE + sizeof(chREV_NVE_);

const char *getLabelByScreen(int screen = 0);
int getOffsetByScreen(int screen = 0);
int getOffsetByScreen1(int screen = 0);

const char *getLabelByScreen_1(int screen = 0);
const char *getLabelByScreen_2(int screen = 0);
const char *getLabelByScreen_3(int screen = 0);


#define OLED_RESET 4
Adafruit_SH1106 display(OLED_RESET);

#if (SH1106_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SH1106.h!");
#endif


struct dataStruct1 {     //This is the NRF data sent out by the Slave. Max of 32 bytes
  float vcc; // vehicle vcc voltage
  float batteryVoltage; // vehicle battery voltage
  boolean batteryOk = true; // the vehicle battery voltage is OK!
  byte channel = 1; // the channel number
} myTele;

struct RcData {
  byte axis1; // Aileron (Steering for car)
  byte axis2; // Elevator
  byte axis3; // Throttle
  byte axis4; // Rudder
  byte pot1;
  byte pot2;
};
RcData data;



void draw(void) {
  switch (activeScreen) {
    case 1:// prints the wellcome screen
      break;
    case 2:// prints the voltage levels of transmitter(TxVcc) and receivers (RXVcc)
      display.clearDisplay();
      display.setCursor(0, 56);
      display.print("M");
      display.setCursor(100, 56);
      display.print("B");
      display.display();
      break;
    case 3:// prints the menu option for axis 1,axis 2,axis3,asis4
      drawMenuScreen();
      break;
    case 4:// prints the menu option for axis 1,axis 2,axis3,asis4
      drawMenuScreen_1();
      break;
    case 5:// prints the menu option for axis 1,axis 2,axis3,asis4
      break;
    case 6:// prints the menu option for axis 1,axis 2,axis3,asis4
      break;
    case 7:// prints the menu option for axis 1,axis 2,axis3,asis4
      break;

    case 8:// prints the menu option for axis 1,axis 2,axis3,asis4
      drawsubMenuScreen_1();
      break;

    default:
      break;


  }
}

void drawMenuScreen() {
  display.clearDisplay();
  display.drawFastHLine(0, 6, 50, WHITE);
  display.drawFastHLine(75, 6, 127, WHITE);
  display.setTextSize(1);
  display.setTextColor(WHITE);
   for (int i = 1; i <= 4; ++i) {
    display.setCursor(10, 25 + (i - 1) * 10);
    display.print(getLabelByScreen(i));
    if (menuRow) {

      display.setCursor(0, 25 + (menuRow - 1) * 10);
      display.print(">");
    }
  }
  display.display();
}
void drawMenuScreen_1() {
  display.clearDisplay();
  display.drawFastHLine(0, 6, 50, WHITE);
  display.drawFastHLine(86, 6, 127, WHITE);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(51, 3);
  display.print("RE");
  display.setCursor(10, 15);
  display.print("v");
  display.setCursor(20, 15);
  display.print(getOffsetByScreen1(submenuRow));



  for (int i = 1; i <= 4; ++i) {
    display.setCursor(10, 25 + (i - 1) * 10);
    display.print(getLabelByScreen_1(i));
    display.setCursor(60, 25 + (submenuRow - 1) * 10);
    display.print(chREV_PVE_[submenuRow-1][menuRow - 1 ]);
    display.setCursor(90, 25 + (submenuRow - 1) * 10);
    display.print(chREV_NVE_[submenuRow-1][menuRow - 1 ]);
    if (submenuRow) {

      display.setCursor(0, 25 + (submenuRow - 1) * 10);
      display.print(">");
    }
  }
  display.display();
}

void drawsubMenuScreen_1() {
  display.clearDisplay();
  display.drawFastHLine(0, 6, 50, WHITE);
  display.drawFastHLine(86, 6, 127, WHITE);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(51, 3);
  display.print("RE");
  display.setCursor(10, 15);
  display.print("v:");
  display.setCursor(20, 15);
  display.print(getOffsetByScreen1(submenuRow));


  for (int i = 1; i <= 4; ++i) {
    display.setCursor(10, 25 + (i - 1) * 10);
    display.print(getLabelByScreen_1(i));
    display.setCursor(60, 25 + (submenuRow - 1) * 10);
    display.print(chREV_PVE_[submenuRow-1][menuRow - 1 ]);
    display.setCursor(90, 25 + (submenuRow - 1) * 10);
    display.print(chREV_NVE_[submenuRow-1][menuRow - 1 ]);
    if (submenuRow) {

      display.setCursor(50 + (incDec - 1 ) * 30, 25 + (submenuRow - 1) * 10);
      display.print(">");
    }
  }
  display.display();
}


const char *getLabelByScreen(int screen = 0) {
  switch (screen == 0 ? activeScreen : screen) {
    case 1: return "RE";
    case 2: return "TA";
    case 3: return "OF";
    case 4: return "V";
    default: return "";
  }
}

const char *getLabelByScreen_1(int screen = 0) {
  switch (screen == 0 ? activeScreen : screen) {
    case 1: return "C1";//channel reversing
    case 2: return "C2";//travel adjust
    case 3: return "C3";//setting offset for menu
    case 4: return "C4";//setting offset for menu
    default: return "";
  }
}

int getOffsetByScreen1(int screen = 0) {
  switch (screen == 0 ? activeScreen : screen) {
    case 1: return  data.axis1;
    case 2: return  data.axis2;
    case 3: return  data.axis3;
    case 4: return  data.axis4;
    default: return 0;
  }
}

void readEEPROMValues() {
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 3; ++j) {
      chREV_PVE_[i][j] = EEPROM.read(EEPROM_INDEX_PVE + i * 3 + j);
      chREV_NVE_[i][j] = EEPROM.read(EEPROM_INDEX_NVE + i * 3 + j);
#ifdef DEBUG
      Serial.print( chREV_PVE_[i][j]);
      Serial.print(" ");
      Serial.print(  chREV_NVE_[i][j]);
      Serial.print(" ");
#endif

    }
    Serial.println();
  }
}

void calibrate() {
  // Read and store average joystick values as offsets
  int sum[4] = {0, 0, 0, 0};
  int samples = 200; // Increase the number of samples for calibration

  for (int i = 0; i < samples; i++) {
    sum[0] += analogRead(JOYSTICK_1);
    sum[1] += analogRead(JOYSTICK_2);
    sum[2] += analogRead(JOYSTICK_3);
    sum[3] += analogRead(JOYSTICK_4);
    delay(10);
  }

  offsetValues[0] = sum[0] / samples;
  offsetValues[1] = sum[1] / samples;
  offsetValues[2] = sum[2] / samples;
  offsetValues[3] = sum[3] / samples;

  Serial.println("Calibration complete!");
}

void setupRadio()
{
  radio.begin();                    //Initialize the nRF24L01 Radio
  radio.setChannel(108);            //Above most WiFi frequencies
  radio.setDataRate(RF24_250KBPS);  //Fast enough.. Better range
  radio.setPALevel(RF24_PA_MAX);//PA (Power Amplifier) Level can be one of four levels: RF24_PA_MIN, RF24_PA_LOW, RF24_PA_HIGH and RF24_PA_MAX
  radio.setAutoAck(pgm_read_64(&pipeOut, vehicleNumber - 1), true);
  radio.enableAckPayload();
  radio.enableDynamicPayloads();
  radio.setRetries(7, 5);  //delay, re-try count
  radio.openWritingPipe(pgm_read_64(&pipeOut, vehicleNumber - 1));
  radio.write( &data, sizeof(data));

}
void readJoystick()
{

  // Read raw joystick values
  rawValues[0] = analogRead(JOYSTICK_1);
  rawValues[1] = analogRead(JOYSTICK_2);
  rawValues[2] = analogRead(JOYSTICK_3);
  rawValues[3] = analogRead(JOYSTICK_4);

  // Apply smoothing
  for (int i = 0; i < 4; i++) {
    smoothedValues[i] = alpha * (rawValues[i] - offsetValues[i]) + (1 - alpha) * smoothedValues[i];
  }

  // Map the smoothed values to the range 1 to 100
  int mappedValues[4];
  for (int i = 0; i < 4; i++) {
    mappedValues[i] = map(smoothedValues[i], 0, 1023, 1, 100);
    if (mappedValues[i] == -1 )
    {
      mappedValues[i] = 0;
    }
  }

  //chREV_PVE_[i][j] = EEPROM.read(EEPROM_INDEX_PVE + i * 3 + j);
  //chREV_NVE_[i][j] = EEPROM.read(EEPROM_INDEX_NVE + i * 3 + j);
  //readJoysticks();
  data.axis1  = map(mappedValues[0] - chREV_PVE_[0][2] + chREV_NVE_[0][2], chREV_PVE_[0][0], chREV_NVE_[0][0], chREV_PVE_[0][1], chREV_NVE_[0][1]); //read values from the Joysticks
  data.axis2  = map(mappedValues[1] - chREV_PVE_[1][2] + chREV_NVE_[1][2], chREV_PVE_[1][0], chREV_NVE_[1][0], chREV_PVE_[1][1], chREV_NVE_[1][1]);
  data.axis3  = map(mappedValues[2] - chREV_PVE_[2][2] + chREV_NVE_[2][2], chREV_PVE_[2][0], chREV_NVE_[2][0], chREV_PVE_[2][1], chREV_NVE_[2][1]); // 45 - 135°
  data.axis4  = map(mappedValues[3] - chREV_PVE_[3][2] + chREV_NVE_[3][2], chREV_PVE_[3][0], chREV_NVE_[3][0], chREV_PVE_[3][1], chREV_NVE_[3][1]);
  data.pot1 = map(analogRead(A6), 0, 1023, 0, 100);
  data.pot2 = map(analogRead(A7), 0, 1023, 0, 100);

  #ifdef DEBUG
    Serial.print("ax1 : ");
    Serial.print(" ");
    Serial.print(data.axis1);
    Serial.print(" ");
    Serial.print("ax2 : ");
    Serial.print(" ");
    Serial.print(data.axis2);
    Serial.print(" ");
     Serial.print("ax3 : ");
    Serial.print(" ");
    Serial.print(data.axis3);
    Serial.print(" ");
    Serial.print("ax4 : ");
    Serial.print(" ");
    Serial.print(data.axis4);
    Serial.print(" ");
    Serial.println();
    
    
#endif
  
}

void transmitRadio()
{
  if (radio.write( &data, sizeof(data) )) {  //Transmit data to the Slave
    if ( radio.isAckPayloadAvailable() ) {       //If an Acknowledge is received, read the data
      radio.read(&myTele, sizeof(myTele));
    }
  }
}
void setup()
{
  pinMode(SEL_BUTTON , INPUT_PULLUP);
  pinMode(BACK_BUTTON, INPUT_PULLUP);
  pinMode(INC_BUTTON, INPUT_PULLUP);
  pinMode(DEC_BUTTON, INPUT_PULLUP);
  display.begin(SH1106_SWITCHCAPVCC, 0x3C);
#ifdef DEBUG
  Serial.begin(115200);
#endif

  readEEPROMValues();

  display.drawPixel(10, 10, WHITE);
  activeScreen = 1;
  draw();
  delay(2000);
  activeScreen = 2;
  draw();
  setupRadio();
  calibrate();

}
void loop()
{
  readJoystick();
  transmitRadio();
  handleButtonPress();

/*#ifdef DEBUG
  /*Serial.print(myTele.vcc); Serial.print("\t");
  Serial.print(myTele.batteryVoltage); Serial.print("\t");
  Serial.print(myTele.batteryOk); Serial.print("\t");
  Serial.print(myTele.channel); Serial.print("\t");
  Serial.println();
#endif*/



}

void handleButtonPress() {
  if (DRE(digitalRead(SEL_BUTTON), buttonState1)) {
#ifdef DEBUG
    Serial.println("Menu ButtonPressed");
#endif
    menuActive = 1;
    menu();
    count = 1;
  }

  if (count == 1) {
    activeScreen = 2;
    draw();
    menuRow = 1;
    count = 0;
  }
}
void menu() {
#ifdef DEBUG
  Serial.println("entered to Menu");
  activeScreen = 3;
  draw();
#endif
  while (menuActive) {
    if (DRE(digitalRead(BACK_BUTTON), buttonState2)) {
      menuActive = 0;
#ifdef DEBUG
      Serial.println("BackButtonPressed");
#endif
    }
    if (DRE(digitalRead(SEL_BUTTON), buttonState1)) {
      axis1 = 1;
      CH_REV();
    }
    if (DRE(digitalRead(INC_BUTTON), buttonState3)) {
      menuRow = constrain(menuRow + 1, 1, 3);
      activeScreen = 3;
      draw();
#ifdef DEBUG
      Serial.println(menuRow);
#endif
      //draw();
    } else if (DRE(digitalRead(DEC_BUTTON), buttonState4)) {
      menuRow = constrain(menuRow - 1, 1, 3);
      activeScreen = 3;
      draw();
#ifdef DEBUG
      Serial.print(menuRow);
      Serial.println(menuRow);
#endif
    }
  }
}

void CH_REV()
{
#ifdef DEBUG
  Serial.println("Entered in channelrev");
#endif
  activeScreen = 4;
  draw();
  submenuRow = 1;
  while (axis1) {
    if (DRE(digitalRead(BACK_BUTTON), buttonState2)) {
      axis1 = 0;
      option = 0;
      activeScreen = 3;
      draw();
    }
    if (DRE(digitalRead(SEL_BUTTON), buttonState1)) {
      submenu = 1;
      sub_Menu();

    }
    if (DRE(digitalRead(INC_BUTTON), buttonState3))
    {
      submenuRow = constrain(submenuRow + 1, 1, 4);
      activeScreen = 4;
      draw();
#ifdef DEBUG
      Serial.print("menuRow : ");
      Serial.print(menuRow);
      Serial.print(" ");
      Serial.print("submenuRow : ");
      Serial.println( submenuRow);
#endif
    }
    else if (DRE(digitalRead(DEC_BUTTON), buttonState4))
    {
      submenuRow = constrain(submenuRow - 1, 1, 4);
      activeScreen = 4;
      draw();
#ifdef DEBUG
      Serial.print("menuRow : ");
      Serial.print(menuRow);
      Serial.print(" ");
      Serial.print("submenuRow : ");
      Serial.println( submenuRow);
#endif
    }
  }
}

void sub_Menu()
{
#ifdef DEBUG
  Serial.println("Entered in Submenu");
#endif
  activeScreen = 8;
  draw();

  while (submenu)
  {
    if (DRE(digitalRead(BACK_BUTTON), buttonState2)) {
      submenu = 0;
      chOption = 0;
      activeScreen = 4;
      draw();
    }

    if (DRE(digitalRead(SEL_BUTTON), buttonState1)) {


      ch1PN = 1;
      ch1_PN();
    }

    if (DRE(digitalRead(INC_BUTTON), buttonState3))
    {
      incDec = constrain(incDec + 1, 1, 2);
      activeScreen = 8;
      draw();
    }
    else if (DRE(digitalRead(DEC_BUTTON), buttonState4))
    {
      incDec = constrain(incDec - 1, 1, 2);
      activeScreen = 8;
      draw();
    }

  }
}

void ch1_PN()
{
  while (ch1PN)
  {
    readJoystick();
    transmitRadio();
    activeScreen = 8;
    draw();

    if (DRE(digitalRead(BACK_BUTTON), buttonState2)) {
      EEPROM.update(EEPROM_INDEX_PVE + (submenuRow - 1) * 3 + menuRow - 1, chREV_PVE_[submenuRow - 1][menuRow - 1]);
      EEPROM.update(EEPROM_INDEX_NVE + (submenuRow - 1) * 3 + menuRow - 1, chREV_NVE_[submenuRow - 1][menuRow - 1]);
#ifdef DEBUG
      Serial.print(submenuRow - 1);
      Serial.print(" ");
      Serial.print(menuRow - 1);
#endif
      submenu = 1;
      ch1PN  = 0;
      activeScreen = 8;
      draw();
    }


    if (DRE(digitalRead(INC_BUTTON), buttonState3)) {
      byte *_ChREV;
      static int prev ;
      switch (incDec) {
        case 1: _ChREV  = &chREV_PVE_[submenuRow - 1][menuRow - 1]; break;
        case 2: _ChREV  = &chREV_NVE_[submenuRow - 1 ][menuRow - 1]; break;

      }
      if (menuRow == 3)
      {
        *_ChREV = constrain(*_ChREV + 1, 0, 100);
        activeScreen = 8;

        if(*_ChREV != prev)
        {
          draw();
          prev = *_ChREV;
        }
        

      }
      else {
        *_ChREV = constrain(*_ChREV + 1, 0, 100);
        activeScreen = 8;
        readJoystick();
        transmitRadio();
         if(*_ChREV != prev)
        {
          draw();
          prev = *_ChREV;
        }

      }

    } else if (DRE(digitalRead(DEC_BUTTON), buttonState4)) {
      byte *_ChREV;
      static int prev;
      switch (incDec) {
        case 1: _ChREV  = &chREV_PVE_[submenuRow - 1 ][menuRow - 1]; break;
        case 2: _ChREV  = &chREV_NVE_[submenuRow - 1 ][menuRow - 1]; break;

      }
      if (menuRow == 3)
      {
        *_ChREV = constrain(*_ChREV - 1, 0, 100);
        activeScreen = 8;
       if(*_ChREV != prev)
        {
          draw();
          prev = *_ChREV;
        }

      }
      else {
        *_ChREV = constrain(*_ChREV - 1, 0, 100);
        activeScreen = 8;
       if(*_ChREV != prev)
        {
          draw();
          prev = *_ChREV;
        }
        readJoystick();
        transmitRadio();

      }
    }
  }
}
