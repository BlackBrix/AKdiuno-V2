/***************************************************************************************************
 * AKduino Part 2: Controlling a Dual Mono AK4490 DAC with an Si570 as a clock generator and an 
 * Amanero as an I2S source.
 *
 * Project Page: http://www.dimdim.gr
 *
 * v1.72    24/12/2017 : - Minor changes to make compatible with current stm32duino core 
 *                         (changed HardWire.h to Wire.h and other minor stuff).
 *                       - First public release as part of completed dual mono DAC project.
 * 
 * v1.66    10/10/2017 : - Minor volume bugfix.
 *                       - SuperSlow filter still problematic.
 *                       - Enabled DAC synchronization feature (experimental..).
 * 
 * v1.64    30/09/2017 : - Bugfixes.
 *
 * v1.60    20/09/2017 : - Added support of rotary encoder and IR remote control.
 *                       - 3.5" TFT support.
 *
 * v1.50    07/01/2017 : - Added support of rotary encoder for volume control.
 *                       - Bugfixes related to DSD.
 *
 * v1.41    06/01/2017 : - Added support for dual mono mode.
 *
 * v1.36    03/01/2017 : - Added very basic TFT support.
 *
 * v1.35    20/12/2016 : - Code cleanup for first public release.
 *
 * v1.33    19/12/2016 : - Added full control of sound parameters through serial port.
 *
 * v1.27    18/12/2016 : - First functional version.
 *                       - Automatic switching between PCM and DSD by monitoring DSDPIN.    
 ***************************************************************************************************/

/***************************************************************************************************
  Code starts here
 ***************************************************************************************************/
#include <Wire.h>                                   // Library for I2C
TwoWire WIRE2(2);                                   // Use Hardware I2C2 (SCL2 = PB10, SDA2 = PB11)
#define Wire WIRE2                                  // To ease use of the "classic" Arduino libraries with the stm32duino's hardware I2C implementation

#include <RotaryEncoder.h>                          // Library for the encoders
#include "Adafruit_MCP23008.h"                      // Library for the I2C port expander(s)
#include "Si570.h"                                  // Library for the Si570 programmable oscillator
#include <irmp.h>                                   // Library for the IR remote

/*************************************************************************************************** 
  Si570 Stuff..
****************************************************************************************************/

#define SI570_I2C_ADDRESS 0x55                      // Si570 I2C address (should always be 0x55)
#define SI570_FACTORY_FREQUENCY 22579200            // Change to factory set frequency of the Si570 (Mouser part)
//#define SI570_FACTORY_FREQUENCY 56320000          // Change to factory set frequency of the Si570 (Ebay part)
Si570 *vfo;                                         // Set up the Si570.

/*************************************************************************************************** 
  IR receiver Stuff..
****************************************************************************************************/
#define SERIALX Serial1
#define PIN_LED PB0
IRMP_DATA irmp_data;
HardwareTimer timer(2);
unsigned loopcount = 0; // loop counter
int prev_result;

// Remote control codes. They correspond to an old remote that I use for testing - change to match your remote's.
#define POWER_CODE 18      // Code for power on/off
#define VOLUP_CODE 12      // Code for Volume up
#define VOLDOWN_CODE 6     // Code for Volume down
#define MUTE_CODE  2       // Code for mute
#define SETTINGS_CODE 11   // Code for DAC Settings ("Setup" button on my remote)
#define GLOBAL_SET_CODE 27 // Code for global DAC Settings ("Title" button on my remote)
#define SELECT_CODE 64     // Code for Select button
#define LEFT_CODE 25       // Code for left arrow
#define RIGHT_CODE 17      // Code for right arrow

/*************************************************************************************************** 
  TFT Stuff..
  Define used fonts etc.
****************************************************************************************************/
#include <Adafruit_GFX.h>
#include <gfxfont.h>
#include "Adafruit_ILI9481_8bit_STM.h"
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include "Arimo_Bold_64.h"
#include "Arimo_Bold_32.h"
#include "Arimo_Bold_26.h"
#include "Arimo_106.h"

Adafruit_ILI9481_8bit_STM tft = Adafruit_ILI9481_8bit_STM();

// Color definitions
#define BLACK    0x0000
#define BLUE     0x001F
#define RED      0xF800
#define GREEN    0x07E0
#define CYAN     0x07FF
#define MAGENTA  0xF81F
#define YELLOW   0xFFE0 
#define WHITE    0xFFFF

/*************************************************************************************************** 
  More definitions..
****************************************************************************************************/

#define VOLUPPIN PB14                               // RotEnc A terminal for rotary encoder.
#define VOLDOWNPIN PB13                             // RotEnc B terminal for rotary encoder.
#define SELECTPIN PB15                              // Switch to select function for rotary encoder.
#define PDN_4490 PC13                               // Set PDN pin of AK4490s
#define DSDPIN PA11                                 // Set DSD pin any digital pin. You should connect this pin to your USB-to-I2S module's "DSD indicator" pin.
#define POWERPIN PB12                               // Power relay pin.
#define MCP_ISO_INT PA11                            // Set MCP_ISO I/O expander interrupt pin
#define INTERVAL_BOUNCE 2                           // Time in milliseconds to debounce the rotary encoder
#define INTERVAL_SWITCHBOUNCE 200                   // Time in milliseconds to debounce switch

#define eeprom 0x50                                 // device address for the 24LC256 EEPROM chip

RotaryEncoder encoder(VOLDOWNPIN, VOLUPPIN);        // Setup the Rotary Encoder

int irdir=0;                                        // Variable to hold the IR Remote's "rotation" direction

Adafruit_MCP23008 mcp_iso;                          // Set up the isolated I/O expander.

// Remove comment slashes ("//") to set code up for dual mono mode. 
#define DUALMONO                                    

// Remove comment slashes ("//") from your USB interface. Keep all other interfaces commented out.
#define Amanero
// #define DIYINHK_XMOS
// #define JLSounds
// #define WaveIO

// Set whether you will be using a power on/off relay. Comment out ALWAYSON if you will be using a remote for power on/off.
#define ALWAYSON

#ifdef ALWAYSON
  bool poweron = true;        // Variable to hold whether the DAC is in power-on state or not
#else
  bool poweron = false;       // Variable to hold whether the DAC is in power-on state or not
#endif ALWAYSON

/*************************************************************************************************** 
  Make sure you use the correct chip address(es) for your AK IC(s). 
  The chip address is determined by how CAD0 & CAD1 are wired.
****************************************************************************************************/

byte ak4490 = 0x13;       // device ak4490, 0010011 = 0x13 (if CAD1=CAD0=H, see datasheet p. 50)
//byte ak4490 = 0x10;     // device ak4490, 0010000 = 0x10 (if CAD1=CAD0=L, see datasheet p. 50)
//byte ak4490 = 0x12;     // device ak4490, 0010010 = 0x12 (if CAD1=H, CAD0=L, see datasheet p. 50)
//byte ak4490 = 0x11;     // device ak4490, 0010001 = 0x11 (if CAD1=L, CAD0=H, see datasheet p. 50)

#ifdef DUALMONO
//byte ak4490r = 0x13;    // device ak4490, 0010011 = 0x13 (if CAD1=CAD0=H, see datasheet p. 50)
//byte ak4490r = 0x10;    // device ak4490, 0010000 = 0x10 (if CAD1=CAD0=L, see datasheet p. 50)
byte ak4490r = 0x12;      // device ak4490, 0010010 = 0x12 (if CAD1=H, CAD0=L, see datasheet p. 50)
//byte ak4490r = 0x11;    // device ak4490, 0010001 = 0x11 (if CAD1=L, CAD0=H, see datasheet p. 50)
#endif DUALMONO

byte error;
byte r;
bool interrupt = 0;

// AK4490 Parameter variables
int volume = 255;         // Variable to hold the volume. From 0 to 255 (0.5db steps).
int volumeOld = 255;      // Previous state.
bool mute = 0;            // Variable to hold mute status.
bool muteOld = 0;         // Previous state.
bool DACAutoMode = true;  // Variable to hold status of AK4490 manual or auto mode. TRUE = auto, FALSE = manual.
bool DACAutoModeOld = true;  // Previous state.
int dacMode = 1;          // Variable to hold whether the DAC chips are in PCM or DSD Mode. PCM = 1, DSD = 2.
int dacModeOld = 0;       // Previous state.
int dacFilter = 1;        // Variable to hold selected DAC filter. Values from 1 to 5.
int dacFilterOld = 0;     // Previous state.
bool superSlow = false;   // Variable to hold DAC superslow mode. True = superslow enabled. False = superslow disabled.
bool superSlowOld = false;   // Previous state.
bool directDsd = false;   // Variable to hold DAC DirectDSD mode. True = DSD Direct enabled. False = DSD Direct disabled.
bool directDsdOld = false;   // Previous state.
int dsdFilter = 50;       // Variable to hold DSD cutoff filter frequency. 50 = KHz, 150 = 150KHz.
int dsdFilterOld = 50;    // Previous state.
int soundMode = 1;        // Variable to hold DAC SoundMode setting. Settings 1 to 3.
int soundModeOld = 1;     // Previous state.
bool InvertPhase = 0;     // Variable to hold DAC output phase inversion. Settings 0 or 1.
bool InvertPhaseOld = 1;  // Previous state.
int SR_DAC;               // Variable to hold incoming SR to the DAC chips.
int SR_DACOld;            // Previous state.

// General Parameters
bool preamp = 1;          // Variable to hold whether the DAC is running in preamp mode (with volume control) or not.
bool preampOld = 1;       // Previous state.
int I2S_Input;            // Variable to hold selected I2S input. 0 is s/pdif from AK4118, 1 is I2S input.
int I2S_Input_Old = 3;    // Previous state.
int SR_I2S;               // Sampling rate output from I2S input (usualy USB-to-I2S interface).
int SR_I2SOld = 0;        // Previous state.
int dsdSignal = 0;        // Signal type from I2S input (usualy USB-to-I2S interface). PCM = 0, DSD = 1.
int dsdSignalOld = 2;     // Previous state.
int MCLK = 0;             // Variable to hold selected MCLK frequency.
int MCLKOld = 0;          // Previous state.
int MCLK_SET = 0;         // Variable to hold selected MCLK frequencies set. 0 for 22/24MHz, 1 for 45/49MHz.
int MCLK_L = 10;          // Variable to hold selected MCLK frequency for 44.1KHz family & DSD.
int MCLK_H = 11;          // Variable to hold selected MCLK frequency for 48KHz family.
bool Vol2Digit = false;   // Variable to hold whether the volume to be displayed is one or two digits wid
bool Vol2DigitOld = false;   // Previous state.

// Character array to store the names of the filters.
char* myFilters[]={"Invalid", "Linear Phase Sharp Roll-off", "Linear Phase Slow Roll-off", "Short Delay Sharp Roll-off", "Short Delay Slow Roll-off", "Super Slow Roll-off", 0};

// Character array to store the names of the inputs.
char* myInputs[]={"Invalid", "COAX 1", "COAX 2", "COAX 3", "COAX 4", "OPT 1", "OPT 2", "OPT 3", "OPT 4", "USB", 0};

// Character array to store the names of the DSD types or Sampling Rates.
// SR = 0 = Invalid, SR = 1..5 = DSD, SR = 6..16 = PCM
char* mySR[]={"Invalid", "DSD1024", "DSD512", "DSD256", "DSD128", "DSD64", "768KHz", "705.6K", "384KHz", "352.8K", "192KHz", "176.4K", "96KHz", "88.2KHz", "48KHz", "44.1KHz", "32KHz", 0};

uint16_t xoffset,yoffset;

int count = 0;
char b[20], c[20], printBuff[32];

// Button stuff
unsigned long keyPrevMillis = 0;
const unsigned long keySampleIntervalMs = 25;
byte longKeyPressCountMax = 80;            // Duration of button press to be considered "long". 80 * 25 = 2000 ms
byte mediumKeyPressCountMin = 40;          // Duration of button press to be considered "medium". 40 * 25 = 1000 ms
byte KeyPressCount = 0;
byte prevKeyState = HIGH;                  // button is active low
const byte keyPin = SELECTPIN;             // button is connected to pin SELECTPIN and GND
int key = 0;                               // Variable to hold the state of the button

volatile int newPos;                       // Rotary encoder variable

unsigned long displayMillis = 0;           // Stores last recorded time for display interval
unsigned long debounceMillis = 0;          // Stores last recorded time for switch debounce interval
unsigned long selectMillis = 0;            // Stores last recorded time for being in select mode

boolean select1Mode=false;                 // To indicate whether in Input options select mode or not
//boolean select2Mode=false;                 // To indicate whether in settings select mode or not
unsigned long select1Millis = 0;           // Stores last recorded time for being in select mode
//unsigned long select2Millis = 0;           // Stores last recorded time for being in select mode

byte select1 = 1;                          // To record first level select position (FIL, VOL, etc)

// The order of selection when clicking the select switch
#define FILT 1          // Filter selection, also number of param variable
#define DIRDSD 2        // DirectDSD selection, also number of param variable
#define DSDFILT 3       // Lock speed setting, also number of param variable
#define SOUND 4           // Dither setting, also number of param variable
#define INVRT 5           // Jitter eliminator selection, also number of param variable
#define CLK 6           // MCLK setting, also number of param variable

#define MAXPARAM 7      // Total number of parameters to keep track of for Settings menu.

int param = 0;          // Hold the number of the parameter to be changed

#define INTERVAL_SELECT 6                  // Time in sec to exit select mode when no activity

bool do_select_filter = 0;

// Variables that store the width of each parameter , to aid with "erasing" the text.
int param_1_width;
int param_2_width;
int param_3_width;
int param_4_width;
int param_5_width;
int param_6_width;
int param_7_width;
int param_8_width;

// Variable that stores the height of each line of parameters, to aid with "erasing" the text.
int param_height;

// Variables that store the state of selection of each parameter.
bool param_1_select;
bool param_2_select;
bool param_3_select;
bool param_4_select;
bool param_5_select;
bool param_6_select;
bool param_7_select;
bool param_8_select;

/************************ MAIN PROGRAM ************************************************************/

void setup()
{
  Serial.begin(115200);                 // Start the serial port (for troubleshooting)
  Serial.println("Serial port initialized");
  
  tft.begin();                          // Start the TFT
  tft.setRotation(3);                   // Set the correct orientation
  tft.fillScreen(ILI9481_BLACK);        // Clear the screen
  tft.setCursor(0, 80);
  tft.setFont(&Arimo_Bold_64);
  tft.println("AKM Controller");
  delay(1000);
  
  Wire.begin();                        // Join The I2C Bus As A Master 
  Serial.println("I2C port initialized");

  pinMode(MCP_ISO_INT, INPUT);          // Set up the interrupt pin for the mcp_iso I/O expander.
  
  // Set up isolated MCP23008 pins
  mcp_iso.begin(2);                     // use address 0x22 (A0=A2=L, A1=H)
  Serial.println("mcp_iso initialized");
  mcp_iso.pinMode(0, INPUT);
  mcp_iso.pinMode(1, INPUT);
  mcp_iso.pinMode(2, INPUT);
  mcp_iso.pinMode(3, INPUT);
  mcp_iso.pinMode(4, INPUT);
  mcp_iso.pinMode(5, OUTPUT);
  mcp_iso.pinMode(6, OUTPUT);
  mcp_iso.pinMode(7, OUTPUT);
  WriteRegister(0x22, 0x02, B00001111); // GPINTEN - enable interrupt-on-change detection for the AK4118 INT0 pin
  WriteRegister(0x22, 0x03, B00000000); // DEFVAL - default pin value. Change from this triggers an interrupt.
  WriteRegister(0x22, 0x04, B00001111); // INTCON - for the AK4118 INT0 pin
  Serial.println("Isolated MCP23008 initialized");
  tft.setCursor(0, 140);
  tft.setFont(&Arimo_Bold_26);
  tft.println("Isolated MCP23008 initialized");

  // IR decoder setup
  irmp_init();   // initialize irmp
  timer2_init(); // initialize timer2
  //Serial.println("IR decoder stuff:");
  //Serial.println(((F_CPU / F_INTERRUPTS)/8) - 1);
  //Serial.println(F_CPU);
  //Serial.println(F_INTERRUPTS);

  // Set up the pin modes
  pinMode(VOLUPPIN, INPUT);                               // Button switch or Encoder pin for volume up
  digitalWrite(VOLUPPIN, HIGH);                           // If H/W debouncing is implemented, set to LOW
  attachInterrupt(VOLUPPIN, interruptFunction, CHANGE);   // Attach interrrupt to this pin

  pinMode(VOLDOWNPIN, INPUT);                             // Button switch or Encoder pin for volume down
  digitalWrite(VOLDOWNPIN, HIGH);                         // If H/W debouncing is implemented, set to LOW
  attachInterrupt(VOLDOWNPIN, interruptFunction, CHANGE); // Attach interrrupt to this pin

  pinMode(SELECTPIN, INPUT_PULLUP);                       // Button switch or Encoder pin for Select  
  
  pinMode(PDN_4490,OUTPUT);                               // AK4490 PDN pin. 
  digitalWrite(PDN_4490, LOW);                            // Keep PDN pin low.

  pinMode(POWERPIN, OUTPUT);                              // Power relay control pin
  digitalWrite(POWERPIN, LOW);                            // Keep low on powerup

  if ((readData(eeprom, 199) == 199) && (readData(eeprom, 200) == 200) && (readData(eeprom, 201) == 201))
    {
      Serial.println("EEPROM has already been initialized");
    }
  else initEEPROM();
  
  readSettings();                                         // Read settings variables from EEPROM

  if (MCLK_SET == 0)
    {
      MCLK_L = 6;          // MCLK frequency for 44.1KHz family = 22.5792 MHz
      MCLK_H = 7;          // MCLK frequency for 48KHz family = 24.576 MHz
      Serial.println("22/24MHz clocks selected");
    }
  else if (MCLK_SET == 1)
    {
      MCLK_L = 10;          // MCLK frequency for 44.1KHz family = 45.1584 MHz
      MCLK_H = 11;          // MCLK frequency for 48KHz family = 49.152 MHz
      Serial.println("45/49MHz clocks selected");
    }

  if (poweron == false)
    {
      Serial.println("DAC in power off condition, going to sleep..");
      tft.setCursor(0, 200);
      tft.setFont(&Arimo_Bold_26);
      tft.println("DAC in power off condition, going to sleep..");
      delay(4000);
      tft.fillScreen(BLACK);
    }
  
  if (poweron == true)                                     // If DAC is always powered on, initialize everything..
    {
      Si570Init();
      AK4490Init();
      I2S_Input = 1;

      DACAutoMode = false;                                      // Set initial DAC mode to Auto or Manual
      SetDACAutoMode();                                         // Set initial DAC mode to Auto or Manual
      USB_SR();
      SetDACMode();
      SetDAC_SR();
      SetVol(volume);
      
      displayDACHelp();
      dispMain(true);
    }

}                               

/*********************** Loop Section *************************************************************/

void loop()                 
{
  if (I2S_Input == 1)                         // If using the I2S input
    {
      USB_SR();                               // Check signal type & sampling rate
      
      if (dsdSignal != dsdSignalOld)
        {
          Serial.println("Signal type changed");
          //Serial.print("dsdSignal: "); Serial.println(dsdSignal);
          //Serial.print("dacMode: "); Serial.println(dacMode);
          
          if ((dsdSignal == 1) && (dacMode == 1)) // toggle PCM & DSD modes
            {
              dacMode = 2;                        // Set to DSD
              SetDACMode();
              MCLK = MCLK_L;
              SetMCLK(MCLK);
              if (select1Mode==false)
                {
                  dispMain(false);
                }
              Serial.println("Signal is DSD");
            }
      
          if ((dsdSignal == 0) && (dacMode == 2)) // toggle PCM & DSD modes
            {
              directDsd = 0;
              EnableDirectDSD(ak4490);
              #ifdef DUALMONO
                EnableDirectDSD(ak4490r);
              #endif DUALMONO
              dacMode = 1;                        // Set to PCM
              SetDACMode();
              if (select1Mode==false)
                {
                  dispMain(false);
                }
              Serial.println("Signal is PCM");
            }
          dsdSignalOld = dsdSignal;
        }
      if (SR_I2S != SR_I2SOld)
        {
          Serial.print(F("I2S sampling rate has changed, new one is "));
          Serial.println(mySR[SR_I2S]);

          SR_DAC = SR_I2S;
          SetDAC_SR();
          
          if ((SR_DAC == 15) || (SR_DAC == 13) || (SR_DAC == 11) || (SR_DAC == 9) || (SR_DAC == 5) || (SR_DAC == 4) || (SR_DAC == 3) || (SR_DAC == 2))
            {
              MCLK = MCLK_L;
              SetMCLK(MCLK);
              if (select1Mode==false)
                {
                  dispMain(false);
                }
              SR_I2SOld = SR_I2S;
            }
          else 
            {
              MCLK = MCLK_H;
              SetMCLK(MCLK);
              if (select1Mode==false)
                {
                  dispMain(false);
                }
              SR_I2SOld = SR_I2S;
            }
        }
    }

// ----------------------------------------------- Serial Port Prompt Code -------------------------------------------------------------------

char command = getCommand();
switch (command)
  {
    case 'v':
      displayMCLKHelp();
      break;
    case 'w':
      displayDACHelp();
      break;
    case 'z':
      displayDACHelp();
      break;
    case '%':
      PrintDebugReg(ak4490);
      break;
    case 'Q':
      SetMCLK(1);
      break;
    case 'W':
      SetMCLK(2);
      break;
    case 'E':
      SetMCLK(3);
      break;
    case 'R':
      SetMCLK(4);
      break;
    case 'T':
      SetMCLK(5);
      break;
    case 'Y':
      SetMCLK(6);
      break;
    case 'U':
      SetMCLK(7);
      break;
    case 'I':
      SetMCLK(8);
      break;
    case 'O':
      SetMCLK(9);
      break;
    case 'P':
      SetMCLK(10);
      break;
    case '{':
      SetMCLK(11);
      break;
    case 'A':
      dacMode = 1;
      SetDACMode();
      break;
    case 'B':
      dacMode = 2;
      SetDACMode();
      break;
    case 'C':
      directDsd = 1;
      EnableDirectDSD(ak4490);
      #ifdef DUALMONO
        EnableDirectDSD(ak4490r);
      #endif DUALMONO
      break;
    case 'D':
      directDsd = 0;
      EnableDirectDSD(ak4490);
      #ifdef DUALMONO
        EnableDirectDSD(ak4490r);
      #endif DUALMONO
      break;
    case 'X':
      dsdFilter = 50;
      SetDSDFilter(ak4490);
      #ifdef DUALMONO
        SetDSDFilter(ak4490r);
      #endif DUALMONO
      break;
    case 'F':
      dsdFilter = 150;
      SetDSDFilter(ak4490);
      #ifdef DUALMONO
        SetDSDFilter(ak4490r);
      #endif DUALMONO
      break;
    case 'G':
      dacFilter = 1;
      SetFilter(ak4490);
      #ifdef DUALMONO
        SetFilter(ak4490r);
      #endif DUALMONO
      break;
    case 'H':
      dacFilter = 2;
      SetFilter(ak4490);
      #ifdef DUALMONO
        SetFilter(ak4490r);
      #endif DUALMONO
      break;
    case 'V':
      dacFilter = 3;
      SetFilter(ak4490);
      #ifdef DUALMONO
        SetFilter(ak4490r);
      #endif DUALMONO
      break;
    case 'J':
      dacFilter = 4;
      SetFilter(ak4490);
      #ifdef DUALMONO
        SetFilter(ak4490r);
      #endif DUALMONO
      break;
    case 'K':
      dacFilter = 5;
      SetFilter(ak4490);
      #ifdef DUALMONO
        SetFilter(ak4490r);
      #endif DUALMONO
      break;
    case 'M':
      soundMode = 1;
      SetSoundMode(ak4490);      
      #ifdef DUALMONO
        SetSoundMode(ak4490r);
      #endif DUALMONO
      break;
    case 'N':
      soundMode = 2;
      SetSoundMode(ak4490);      
      #ifdef DUALMONO
        SetSoundMode(ak4490r);
      #endif DUALMONO
      break;
    case 'S':
      soundMode = 3;
      SetSoundMode(ak4490);      
      #ifdef DUALMONO
        SetSoundMode(ak4490r);
      #endif DUALMONO
      break;
    case '<':
      InvertPhase = 0;
      SetInvertPhase(ak4490);      
      #ifdef DUALMONO
        SetInvertPhase(ak4490r);
      #endif DUALMONO
      break;      
    case '>':
      InvertPhase = 1;
      SetInvertPhase(ak4490);      
      #ifdef DUALMONO
        SetInvertPhase(ak4490r);
      #endif DUALMONO
      break;
    default:
      break;
  }

// ------------------------  Rotary encoder code ----------------------------------------------
  static int pos = 0;
  int dir = 0;

  if (pos != newPos) 
  {
    if (pos < newPos)
    {
      dir = 1;
      pos = newPos;
    }
    else if (pos > newPos)
      {
        dir = 2;
        pos = newPos;
      }
  }
  
  if ((dir==1 || dir==2 || irdir==1 || irdir==2) && select1Mode==false && poweron == true)
  {
    //delay(INTERVAL_BOUNCE);  // debounce by waiting INTERVAL_BOUNCE time
    if (dir==1 || irdir==1)  // CCW
      {
        if (volume < 255)
        {
          volume=volume+2;
          Serial.print("Volume: "); Serial.println(volume);
          SetVol(volume);
        }
        irdir=0;
      }
      else                     // If not CCW, then it is CW
      {
        if (volume > 56)
        {
          volume=volume-2;
          Serial.print("Volume: "); Serial.println(volume);
          SetVol(volume);
        }
        irdir=0;
      }
  }

  if((dir==1 || dir==2 || irdir==1 || irdir==2) && select1Mode==true && poweron == true)     // When in select1Mode
  {
    switch(select1%(MAXPARAM))
      {  
      case FILT:
      if (dir==1 || irdir==1)  // CW
        {
          if (dacFilter < 5)
            {
              dacFilter++;
            }
          else dacFilter = 1;
        }
      else                                                      // If not CW, then it is CCW
        {
          if (dacFilter > 1)
            {
              dacFilter--;
            }
          else dacFilter = 5;
        }
      param=FILT;
      SetFilter(ak4490);
      #ifdef DUALMONO
        SetFilter(ak4490r);
      #endif DUALMONO
      menu1_disp(false);
      writeSettings();
      select1Millis=millis();
      irdir=0;
      break;
      
      case DIRDSD:
      if (dir==1 || irdir==1)  // CW
        {
          if (directDsd == true)
            {
              directDsd=false;
            }
          else if (directDsd == false)
                 {
                   directDsd=true;
                 }
        }
      else                     // If not CW, then it is CCW
        {
          if (directDsd == false)
            {
              directDsd = true;
            }
          else if (directDsd == true)
                 {
                   directDsd = false;
                 }            
        }
      param=DIRDSD;
      EnableDirectDSD(ak4490);
      #ifdef DUALMONO
        EnableDirectDSD(ak4490r);
      #endif DUALMONO
      menu1_disp(false);
      writeSettings();
      select1Millis=millis();
      irdir=0;
      break;

      case DSDFILT:
      if (dir==1 || irdir==1)  // CW
        {
          if (dsdFilter == 50)
            {
              dsdFilter = 150;
            }
          else if (dsdFilter == 150)
                 {
                   dsdFilter = 50;
                 }
        }
      else                     // If not CW, then it is CCW
        {
          if (dsdFilter == 150)
            {
              dsdFilter = 50;
            }
          else if (dsdFilter == 50)
                 {
                   dsdFilter = 150;
                 }            
        }
      param=DSDFILT;
      SetDSDFilter(ak4490);
      #ifdef DUALMONO
        SetDSDFilter(ak4490r);
      #endif DUALMONO
      menu1_disp(false);
      writeSettings();
      select1Millis=millis();
      irdir=0;
      break;

      case SOUND:
      if (dir==1 || irdir==1)  // CW
        {
          if (soundMode < 3)
            {
              soundMode++;
            }
          else soundMode = 1;
        }
      else                     // If not CW, then it is CCW
        {
          if (soundMode > 1)
            {
              soundMode--;
            }
          else soundMode = 3;          
        }
      param=SOUND;
      SetSoundMode(ak4490);      
      #ifdef DUALMONO
        SetSoundMode(ak4490r);
      #endif DUALMONO
      menu1_disp(false);
      writeSettings();
      select1Millis=millis();
      irdir=0;
      break;

      case INVRT:
      if (dir==1 || irdir==1)  // CW
        {
          if (InvertPhase == 0)
            {
              InvertPhase = 1;
            }
          else if (InvertPhase == 1)
                {
                  InvertPhase = 0;
                }
        }
      else                     // If not CW, then it is CCW
        {
          if (InvertPhase == 1)
            {
              InvertPhase = 0;
            }
          else if (InvertPhase == 0)
                {
                  InvertPhase = 1;
                }            
        }
      param=INVRT;
      SetInvertPhase(ak4490);      
      #ifdef DUALMONO
        SetInvertPhase(ak4490r);
      #endif DUALMONO
      menu1_disp(false);
      writeSettings();
      select1Millis=millis();
      irdir=0;
      break;
      
      case CLK:
      if (dir==1 || irdir==1)  // CW
        {
          if (MCLK_SET == 0)
            {
              MCLK_SET = 1;
            }
          else if (MCLK_SET == 1)
            {
              MCLK_SET = 0;
            }
        }
      else                     // If not CW, then it is CCW
        {
          if (MCLK_SET == 0)
            {
              MCLK_SET = 1;
            }
          else if (MCLK_SET == 1)
            {
              MCLK_SET = 0;
            }            
        }
      if (MCLK_SET == 0)
        {
          MCLK_L = 6;          // MCLK frequency for 44.1KHz family = 22.5792 MHz
          MCLK_H = 7;          // MCLK frequency for 48KHz family = 24.576 MHz
        }
      else if (MCLK_SET == 1)
        {
          MCLK_L = 10;          // MCLK frequency for 44.1KHz family = 45.1584 MHz
          MCLK_H = 11;          // MCLK frequency for 48KHz family = 49.152 MHz
        }
      writeSettings();
      param=CLK;
      menu1_disp(false);
      select1Millis=millis();
      irdir=0;
      break;

    }
    dir=0;
  }  // End of "in select1Mode"
    

// ------------------------  Rotary encoder button code ----------------------------------------------

  buttonPressed();                                                  // Get status of button
  if (key==1)                                                       // Momentary press of the button
    {
      if (select1Mode == false)                                     // If not in Select mode
        {
          if (poweron == false)                                     // If not powered on, do power on.
          {
            poweron = true;
            digitalWrite(POWERPIN, HIGH);
            Serial.println(F("Powering on"));
            Si570Init();
            AK4490Init();
            I2S_Input = 1;
            //dacMode = 1;                                              // Set default AK4490 signal type to PCM.
            //SetDACMode();
            DACAutoMode = false;                                      // Set initial DAC mode to Auto or Manual
            SetDACAutoMode();                                         // Set initial DAC mode to Auto or Manual
            USB_SR();
            SetDACMode();
            SetDAC_SR();
            SetVol(volume);
                        
            displayDACHelp();
            dispMain(true);
            delay(100);
            key=0;
          }
          else if (poweron == true)                                 // If powered on, do filter selection.
          {
            Serial.println(F("Go into menu mode."));
            select1Mode = true;
            select1Millis=millis();                                         // Start being-in-select-mode timer
            param=1;
            menu1_disp(true);
            key=0;  
          }
        }
      else if (select1Mode == true)                                 // If in Select mode
        {
          select1++;
          Serial.println(select1);
          switch(select1%(MAXPARAM))
            {  
              case FILT:
              param=FILT;
              menu1_disp(false);
              select1Millis=millis();
              key=0;
              break;
      
              case DIRDSD:
              param=DIRDSD;
              menu1_disp(false);
              select1Millis=millis();
              key=0;
              break;

              case DSDFILT:
              param=DSDFILT;
              menu1_disp(false);
              select1Millis=millis();
              key=0;
              break;              

              case SOUND:
              param=SOUND;
              menu1_disp(false);
              select1Millis=millis();
              key=0;
              break;

              case INVRT:
              param=INVRT;
              menu1_disp(false);
              select1Millis=millis();
              key=0;
              break;

              case CLK:
              param=CLK;
              menu1_disp(false);
              select1Millis=millis();
              key=0;
              break;
           }
         Serial.print("Param: "); Serial.println(param);
      }
   }
 
  else if (key==2)                                                        // Long press of the button
    {

    }
  
  else if (key==3)                                                        // Very long press of the button
    {

    }
// End of rotary encoder button code

// ------------------------  IR receiver code ----------------------------------------------
   
  if (irmp_get_data (&irmp_data))
    {
      if (irmp_data.command == POWER_CODE)                                      // What to do if the Power button is pressed
      {
        if (poweron == false)                                             // If not powered on, do power on.
          {
            poweron = true;
            digitalWrite(POWERPIN, HIGH);
            Serial.println(F("Powering on"));
            Si570Init();
            AK4490Init();
            I2S_Input = 1;
            DACAutoMode = false;                                      // Set initial DAC mode to Auto or Manual
            SetDACAutoMode();                                         // Set initial DAC mode to Auto or Manual
            SetVol(volume);
            displayDACHelp();
            dispMain(true);
            delay(500);
            prev_result = 0;
          }
        else if (poweron == true)                                         // If powered on, power off.
          {
            poweron = false;
            digitalWrite(POWERPIN, LOW);
            Serial.println(F("Powering off"));
            tft.fillScreen(BLACK);
          }
      } 

    else if (irmp_data.command == SELECT_CODE)                               // What to do if the Select button is pressed
      {
        if (poweron == true) 
          {
            key = 1;                                                     // Set the variable "key" to 1, so same as pressing the rot enc.
            prev_result = 0;
          }
      }  

    else if (irmp_data.command == LEFT_CODE)                                 // What to do if the Left button is pressed
      {
        //Serial.write("Left\n");
        if (poweron == true)
          {
            irdir = 2;    
            //prev_result = 0;
          }
      } 
    
    else if (irmp_data.command == RIGHT_CODE)                                 // What to do if the Right button is pressed
      {
        //Serial.write("Right\n");
        if (poweron == true)
          { 
            irdir = 1;  
            //prev_result = 0;
          }
      } 

    else if (irmp_data.command == MUTE_CODE)                                  // What to do if the Mute button is pressed
      {
      //Serial.write("Mute\n");
        if ((poweron == true) && (mute == false))                         // If powered on and not muted, do mute
          {
            mute = true;
            //setMute();
            //dispMain(false);
          }
        else if ((poweron == true) && (mute == true))                     // If powered on and mute, do unmute
          {
            mute = false;
            //setMute();
            //dispMain(false);
          }
        //prev_result = 0;
      }     
    
    else if ((irmp_data.command == VOLUP_CODE) && (poweron == true))          // What to do if the Volume Up button is pressed
      {  
        if (poweron)
          {
            if ((volume < 255) && (preamp == true))                         // If in preamp mode, change volume
              {
                volume=volume+2;
                Serial.print("Volume: "); Serial.println(volume);
                SetVol(volume);
                irdir=0;
              }      
            //prev_result = VOLUP_CODE;
          }
      }

    else if ((irmp_data.command == VOLDOWN_CODE) && (poweron == true))        // What to do if the Volume Down button is pressed
      {    
        if (poweron)
          {
            if ((volume > 56) && (preamp == true))                       // If in preamp mode, change volume
              {
                volume=volume-2;
                Serial.print("Volume: "); Serial.println(volume);
                SetVol(volume);
                irdir=0;       
              } 
            //prev_result = VOLDOWN_CODE;
          }
      }

    else if ((irmp_data.command == SETTINGS_CODE) && (poweron == true))       // What to do if the Settings button is pressed
      {    
        if (poweron)
          {
            key = 1;
            //prev_result = SETTINGS_CODE;
          }
      }

    else if ((irmp_data.command == GLOBAL_SET_CODE) && (poweron == true))       // What to do if the Settings button is pressed
      {    
        if (poweron)
          {
            Serial.println(F("Go into global settings menu mode."));
            //select1Mode = false;
            //select2Mode = true;
            //select2Millis=millis();                                         // Start being-in-select-mode timer
            //setting=1;
            //param=1;
            //settings_disp(true);
            //prev_result = GLOBAL_SET_CODE;
          }
      }
   
    else 
      {
        Serial.print(F("unexpected value: "));                               // If the IR code is not recognized, output it to Serial
        Serial.println(irmp_data.command);
        prev_result = 0;
      }
    }



// ----------------------------------------------- Come out of Select1Mode if it times out ---------------------------------------------------------------

  if((select1Mode==true) && (select1Mode&&millis()-select1Millis > INTERVAL_SELECT*1000))
    {
      select1Mode=false;                        // No longer in select mode
      param = 0;
      select1 = 1;
      dispMain(true);
      Serial.println(F("Came out of menu mode"));
    } 

} // loop

// ------------------------------------------------------------- Functions -------------------------------------------------------------------------------

// ----------------------------- Runs when interrupt happens on either of the rotary encoder's pins ------------------------------------------------------
void interruptFunction() 
  {
    encoder.tick();
    newPos = encoder.getPosition();
  }

// ----------------------------- Set MCLK by changing the output frequency of the Si570 clock generator ----------------------------------------------------
void SetMCLK (int MCLK) 
{ 
/* 1 : 11.2896 MHz
 * 2 : 12.288 MHz
 * 3 : 16.384 MHz
 * 4 : 16.9344 MHz
 * 5 : 18.432 MHz
 * 6 : 22.5792 MHz
 * 7 : 24.576 MHz
 * 8 : 33.8688 MHz
 * 9 : 36.864 MHz
 * 10 : 45.1584 MHz
 * 11 : 49.152 MHz
 */

  String frequency = "";
 
  switch (MCLK) 
    {
      case 1:
      vfo->setFrequency(11289600L);
      Serial.println("MCLK set to 11.2896 MHz.");
      break;

      case 2:
      vfo->setFrequency(12288000L);
      Serial.println("MCLK set to 12.288 MHz.");
      break;

      case 3:
      vfo->setFrequency(16384000L);
      Serial.println("MCLK set to 16.384 MHz.");
      break;

      case 4:
      vfo->setFrequency(16934400L);
      Serial.println("MCLK set to 16.9344 MHz.");
      break;

      case 5:
      vfo->setFrequency(18432000L);
      Serial.println("MCLK set to 18.432 MHz.");
      break;

      case 6:
      vfo->setFrequency(22579200L);
      Serial.println("MCLK set to 22.5792 MHz.");
      break;

      case 7:
      vfo->setFrequency(24576000L);
      Serial.println("MCLK set to 24.576 MHz.");
      break;

      case 8:
      vfo->setFrequency(33868800L);
      Serial.println("MCLK set to 33.8688 MHz.");
      break;

      case 9:
      vfo->setFrequency(36864000L);
      Serial.println("MCLK set to 36.864 MHz.");
      break;    
      
      case 10:
      vfo->setFrequency(45158400L);
      Serial.println("MCLK set to 45.1584 MHz.");
      break;

      case 11:
      vfo->setFrequency(49152000L);
      Serial.println("MCLK set to 49.152 MHz.");
      break; 
    }
} 

// ----------------------------- Set DAC chip parameters (including MCLK) according to incoming SR  ----------------------------------------------------
void SetDACParam()
  {
    if (SR_DAC == 15)                 // 44.1K
      {
        if (DACAutoMode == true)
          {
            MCLK = MCLK_L;                   // Set MCLK to 22.5792 MHz
            SetMCLK(MCLK);
          }
        //else SetMCLK(10);               // Set MCLK to 45.1584 MHz
        if (DACAutoMode == false)       // If DAC is set to Manual mode
          {
            SetDAC_SR();                // Set the AK4490 to the appropriate SR mode.  
          }
      }
    else if (SR_DAC == 14)            // 48K
      {
        if (DACAutoMode == true)
          {
            MCLK = MCLK_H;                   // Set MCLK to 24.576 MHz
            SetMCLK(MCLK);
          }
        //else SetMCLK(11);               // Set MCLK to 49.152 MHz
        if (DACAutoMode == false)       // If DAC is set to Manual mode
          {
            SetDAC_SR();                // Set the AK4490 to the appropriate SR mode.  
          }

      }
    else if (SR_DAC == 13)            // 88.2K
      {
        if (DACAutoMode == true)
          {
            MCLK = MCLK_L;                   // Set MCLK to 22.5792 MHz
            SetMCLK(MCLK);
          }
        //else SetMCLK(10);               // Set MCLK to 45.1584 MHz
        if (DACAutoMode == false)       // If DAC is set to Manual mode
          {
            SetDAC_SR();                // Set the AK4490 to the appropriate SR mode.  
          }

      }
    else if (SR_DAC == 12)            // 96K
      {
        if (DACAutoMode == true)
          {
            MCLK = MCLK_H;                   // Set MCLK to 24.576 MHz
            SetMCLK(MCLK);
          }
        //else SetMCLK(11);               // Set MCLK to 49.152 MHz
        if (DACAutoMode == false)       // If DAC is set to Manual mode
          {
            SetDAC_SR();                // Set the AK4490 to the appropriate SR mode.  
          }

      }    
    else if (SR_DAC == 11)            // 176.4K
      {
        if (DACAutoMode == true)
          {
            MCLK = MCLK_L;                   // Set MCLK to 22.5792 MHz
            SetMCLK(MCLK);
          }
        //else SetMCLK(10);               // Set MCLK to 45.1584 MHz
        if (DACAutoMode == false)       // If DAC is set to Manual mode
          {
            SetDAC_SR();                // Set the AK4490 to the appropriate SR mode.  
          }

      }
    else if (SR_DAC == 10)            // 192K
      {
        if (DACAutoMode == true)
          {
            MCLK = MCLK_H;                   // Set MCLK to 24.576 MHz
            SetMCLK(MCLK);
          }
        //else SetMCLK(11);               // Set MCLK to 49.152 MHz
        if (DACAutoMode == false)       // If DAC is set to Manual mode
          {
            SetDAC_SR();                // Set the AK4490 to the appropriate SR mode.  
          }
      }
  }

// ----------------------------- Set DAC chip volume  -------------------------------------------------------------------
void SetVol (byte RegVal) 
{ 
  WriteRegister (ak4490, 0x03, RegVal); // Set Up Volume In DAC-L ATT 
  WriteRegister (ak4490, 0x04, RegVal); // Set Up Volume In DAC-R ATT
  #ifdef DUALMONO
    WriteRegister (ak4490r, 0x03, RegVal); // Set Up Volume In DAC-L ATT 
    WriteRegister (ak4490r, 0x04, RegVal); // Set Up Volume In DAC-R ATT
  #endif DUALMONO

  if (select1Mode==false)
    {
      dispMain(false);
    }
} 

// ----------------------------- Set DAC chip to PCM or DSD operation ----------------------------------------------------
void SetDACMode() 
{
  if (dacMode == 1)                             // Set To PCM Mode
    {
      Serial.println("");
      Serial.println("Setting up for PCM.");
      ChangeBit(ak4490, 0x01, 0, true);         // Enable soft mute
      ChangeBit(ak4490, 0x02, 7, false);        // Set To PCM Mode
      WriteRegister(ak4490,0x00,B00000000);     // Reset
      WriteRegister(ak4490,0x00,B10001111);     // Set To Master Clock Frequency Auto / 32Bit I2S Mode
      ChangeBit(ak4490, 0x01, 0, false);        // Disable soft mute
      #ifdef DUALMONO
        ChangeBit(ak4490r, 0x01, 0, true);      // Enable soft mute
        ChangeBit(ak4490r, 0x02, 7, false);     // Set To PCM Mode
        WriteRegister(ak4490r,0x00,B00000000);  // Reset
        WriteRegister(ak4490r,0x00,B10001111);  // Set To Master Clock Frequency Auto / 32Bit I2S Mode
        ChangeBit(ak4490r, 0x01, 0, false);     // Disable soft mute
      #endif DUALMONO
      SetDACAutoMode();                         // Change mode to either Auto or Manual
    }
  else if (dacMode == 2)                        // Set To DSD Mode
    {
      Serial.println("");
      Serial.println("Setting up for DSD.");
      ChangeBit(ak4490, 0x01, 0, true);         // Enable soft mute
      ChangeBit(ak4490, 0x02, 7, true);         // Set To DSD Mode
      WriteRegister(ak4490,0x00,B00000000);     // Reset
      WriteRegister(ak4490,0x00,B00000001);     // Normal operation
      WriteRegister(ak4490,0x00,B10001111);     // Set To Master Clock Frequency Auto / 32Bit I2S Mode
      WriteRegister(ak4490,0x06,B10011001);     // Set To DSD Data Mute / DSD Mute Control / DSD Mute Release
      WriteRegister(ak4490,0x09,B00000001);     // Set To DSD Sampling Speed Control
      ChangeBit(ak4490, 0x01, 0, false);        // Disable soft mute
      #ifdef DUALMONO
        ChangeBit(ak4490r, 0x01, 0, true);      // Enable soft mute
        ChangeBit(ak4490r, 0x02, 7, true);      // Set To DSD Mode    
        WriteRegister(ak4490r,0x00,B00000000);  // Reset
        WriteRegister(ak4490r,0x00,B00000001);  // Normal operation
        WriteRegister(ak4490r,0x00,B10001111);  // Set To Master Clock Frequency Auto / 32Bit I2S Mode
        WriteRegister(ak4490r,0x06,B10011001);  // Set To DSD Data Mute / DSD Mute Control / DSD Mute Release
        WriteRegister(ak4490r,0x09,B00000001);  // Set To DSD Sampling Speed Control  
        ChangeBit(ak4490r, 0x01, 0, false);     // Disable soft mute
      #endif DUALMONO
      SetDACAutoMode();                         // Change mode to either Auto or Manual
    }
}

// ----------------------------- Set DAC chip filter  --------------------------------------------------------------------------
void SetFilter(byte dac)
{
  if (dacFilter == 1)
    {
      Serial.println("Sharp roll-off filter");              // SD=0, SLOW=0
      ChangeBit(dac, 0x05, 0, false);   // SuperSlow disabled
      ChangeBit(dac, 0x01, 5, false);
      ChangeBit(dac, 0x02, 0, false);
    }
  else if (dacFilter == 2)
    {
      Serial.println("Slow roll-off filter");               // SD=0, SLOW=1
      ChangeBit(dac, 0x05, 0, false);   // SuperSlow disabled
      ChangeBit(dac, 0x01, 5, false);
      ChangeBit(dac, 0x02, 0, true);
    }
  else if (dacFilter == 3)
    {
      Serial.println("Short delay sharp roll off filter");  // SD=1, SLOW=0
      ChangeBit(dac, 0x05, 0, false);   // SuperSlow disabled
      ChangeBit(dac, 0x01, 5, true);
      ChangeBit(dac, 0x02, 0, false);
    }
  else if (dacFilter == 4)
    {
      Serial.println("Short delay slow roll off filter");   // SD=1, SLOW=1
      ChangeBit(dac, 0x05, 0, false);   // SuperSlow disabled
      ChangeBit(dac, 0x01, 5, true);
      ChangeBit(dac, 0x02, 0, true);
    }
  else if (dacFilter == 5)
    {
      Serial.println("SuperSlow filter");
      ChangeBit(dac, 0x01, 5, false);  // SD = 0
      ChangeBit(dac, 0x02, 0, false);  // SLOW = 0 (default)
      ChangeBit(dac, 0x05, 0, true);   // SuperSlow enabled
    }
  if (select1Mode==false)
    {
      dispMain(false);
    }
}

// --------------------------------------------- Set DAC chip DSD Direct mode -------------------------------------------------------------------
void EnableDirectDSD(byte dac) 
{
  switch (directDsd) 
    {
      case 0:
      Serial.println("DSD Normal Path Set");
      ChangeBit(dac, 0x06, 1, false);
      break;

      case 1:
      Serial.println("DSD Direct Path Set");
      ChangeBit(dac, 0x06, 1, true);      
      break;
    }
//PrintDebugReg();
}

// --------------------------------------------- Set DAC chip DSD filter frequency -------------------------------------------------------------------
void SetDSDFilter(byte dac)
{
  switch (dsdFilter)
    {
      case 50:
      Serial.println("DSD Cut Off Filter at 50KHz");
      ChangeBit(dac, 0x09, 1, false);
      break;

      case 150:
      Serial.println("DSD Cut Off Filter at 150KHz");
      ChangeBit(dac, 0x09, 1, true);      
      break;
    }
}

// --------------------------------------------- Set DAC chip Sound Mode -------------------------------------------------------------------
void SetSoundMode(byte dac) 
{
  if (soundMode == 1)
    {
      Serial.println("Sound Setting 1");
      WriteRegister(dac,0x08,B00000000);      
    }
  if (soundMode == 2)
    {
      Serial.println("Sound Setting 2");
      WriteRegister(dac,0x08,B00000001);      
    }      
  if (soundMode == 3)
    {
      Serial.println("Sound Setting 3");
      WriteRegister(dac,0x08,B00000010);      
    } 
}

// --------------------------------------------- Set DAC chip normal or inverting output -------------------------------------------------------------------
void SetInvertPhase(byte dac) 
{
  if (InvertPhase == 0)
    {
      Serial.println("Output phase normal");
      ChangeBit(dac, 0x05, 7, false);
      ChangeBit(dac, 0x05, 6, false);
    }
  else if (InvertPhase == 1)
    {
      Serial.println("Output phase inverted");
      ChangeBit(dac, 0x05, 7, true);
      ChangeBit(dac, 0x05, 6, true);
    }      
}

// --------------------------------------------- Set DAC chip to Auto or Manual mode  -------------------------------------------------------------------
void SetDACAutoMode() 
{
  if (DACAutoMode == true) 
    {
      ChangeBit(ak4490, 0x00, 7, true);
      #ifdef DUALMONO
        ChangeBit(ak4490r, 0x00, 7, true);
      #endif DUALMONO
      Serial.println("DAC set to Auto mode.");
    }
  else if (DACAutoMode == false) 
    {
      ChangeBit(ak4490, 0x00, 7, false);
      #ifdef DUALMONO
        ChangeBit(ak4490r, 0x00, 7, false);
      #endif DUALMONO
      Serial.println("DAC set to Manual mode.");
    }
//PrintDebugReg();
}

// --------------------------------------------- Set DAC chip Sampling Rate -------------------------------------------------------------------
void SetDAC_SR() 
{
  if ((SR_DAC == 16) || (SR_DAC == 15) || (SR_DAC == 14))         // DAC SR set to 30K - 54K
    {
      ChangeBit(ak4490, 0x01, 3, false);
      ChangeBit(ak4490, 0x01, 4, false);
      ChangeBit(ak4490, 0x05, 1, false);
      #ifdef DUALMONO
        ChangeBit(ak4490r, 0x01, 3, false);
        ChangeBit(ak4490r, 0x01, 4, false);
        ChangeBit(ak4490r, 0x05, 1, false);
      #endif DUALMONO
      Serial.println("DAC SR set to 30K - 54K.");
    }
  else if ((SR_DAC == 13) || (SR_DAC == 12))                      // DAC SR set to 54K - 108K
    {
      ChangeBit(ak4490, 0x01, 3, true);
      ChangeBit(ak4490, 0x01, 4, false);
      ChangeBit(ak4490, 0x05, 1, false);
      #ifdef DUALMONO
        ChangeBit(ak4490r, 0x01, 3, true);
        ChangeBit(ak4490r, 0x01, 4, false);
        ChangeBit(ak4490r, 0x05, 1, false);
      #endif DUALMONO
      Serial.println("DAC SR set to 54K - 108K.");
    }
  else if ((SR_DAC == 11) || (SR_DAC == 10))                      // DAC SR set to 120K - 216K
    {
      ChangeBit(ak4490, 0x01, 3, false);
      ChangeBit(ak4490, 0x01, 4, true);
      ChangeBit(ak4490, 0x05, 1, false);
      #ifdef DUALMONO
        ChangeBit(ak4490r, 0x01, 3, false);
        ChangeBit(ak4490r, 0x01, 4, true);
        ChangeBit(ak4490r, 0x05, 1, false);
      #endif DUALMONO
      Serial.println("DAC SR set to 120K - 216K.");
    }
  else if ((SR_DAC == 9) || (SR_DAC == 8))                        // DAC SR set to 352K - 384K
    {
      ChangeBit(ak4490, 0x01, 3, false);
      ChangeBit(ak4490, 0x01, 4, false);
      ChangeBit(ak4490, 0x05, 1, true);
      #ifdef DUALMONO
        ChangeBit(ak4490r, 0x01, 3, false);
        ChangeBit(ak4490r, 0x01, 4, false);
        ChangeBit(ak4490r, 0x05, 1, true);
      #endif DUALMONO
      Serial.println("DAC SR set to 352K - 384K.");
    }
  else if ((SR_DAC == 7) || (SR_DAC == 6))                         // DAC SR set to 705K - 768K
    {
      ChangeBit(ak4490, 0x01, 3, true);
      ChangeBit(ak4490, 0x01, 4, false);
      ChangeBit(ak4490, 0x05, 1, true);
      #ifdef DUALMONO
        ChangeBit(ak4490r, 0x01, 3, true);
        ChangeBit(ak4490r, 0x01, 4, false);
        ChangeBit(ak4490r, 0x05, 1, true);
      #endif DUALMONO
      Serial.println("DAC SR set to 705K - 768K.");
    }
  else if (SR_DAC == 5)                                            // DAC SR set to DSD 64
    {
      ChangeBit(ak4490, 0x06, 0, false);
      ChangeBit(ak4490, 0x09, 0, false);
      #ifdef DUALMONO
        ChangeBit(ak4490r, 0x06, 0, false);
        ChangeBit(ak4490r, 0x09, 0, false);
      #endif DUALMONO
      Serial.println("DAC SR set to DSD 64");
    }
  else if (SR_DAC == 4)                                            // DAC SR set to DSD 128
    {
      ChangeBit(ak4490, 0x06, 0, true);
      ChangeBit(ak4490, 0x09, 0, false);
      #ifdef DUALMONO
        ChangeBit(ak4490r, 0x06, 0, true);
        ChangeBit(ak4490r, 0x09, 0, false);
      #endif DUALMONO
      Serial.println("DAC SR set to DSD 128");
    }
  else if (SR_DAC == 3)                                            // DAC SR set to DSD 256
    {
      ChangeBit(ak4490, 0x06, 0, false);
      ChangeBit(ak4490, 0x09, 0, true);
      #ifdef DUALMONO
        ChangeBit(ak4490r, 0x06, 0, false);
        ChangeBit(ak4490r, 0x09, 0, true);
      #endif DUALMONO
      Serial.println("DAC SR set to DSD 256");
    }
    
    ChangeBit(ak4490, 0x00, 0, false);              // Reset the AK4490
    ChangeBit(ak4490, 0x00, 0, true);               // Start the AK4490
    #ifdef DUALMONO
      ChangeBit(ak4490r, 0x00, 0, false);           // Reset the AK4490
      ChangeBit(ak4490r, 0x00, 0, true);            // Start the AK4490
    #endif DUALMONO
//PrintDebugReg();
}


// --------------------------------------------- Serial Port Stuff -------------------------------------------------------------------

char getCommand()                                            // read a character from the serial port
{
  char c = '\0';
  if (Serial.available())
  {
    c = Serial.read();
  }
  return c;
}

void displayMCLKHelp()                                            // displays available commands through the serial port
{
  Serial.println();
  Serial.println("AK4490 MCLK Selection");
  Serial.println();
  Serial.println("Press Q to 11.2896 MHz");
  Serial.println("Press W to 12.288 MHz");
  Serial.println("Press E to 16.384 MHz");
  Serial.println("Press R to 16.9344 MHz");
  Serial.println("Press T to 18.432 MHz");
  Serial.println("Press Y to 22.5792 MHz");
  Serial.println("Press U to 24.576 MHz");
  Serial.println("Press I to 33.8688 MHz");
  Serial.println("Press O to 36.864 MHz");
  Serial.println("Press P to 45.1584 MHz");
  Serial.println("Press { to 49.152 MHz");
  Serial.println("Press z to display main menu");
  Serial.println();
}

void displayDACHelp()                                            // displays available commands through the serial port
{
  Serial.println();
  Serial.println("AK4490 DAC Settings");
  Serial.println();
  Serial.println("Press v to go to DAC MCLK menu");  
  Serial.println("Press A to manually select PCM");
  Serial.println("Press B to manually select DSD");
  Serial.println("Press C to enable DirectDSD");
  Serial.println("Press D to disable DirectDSD");
  Serial.println("Press X to set DSD Filter at 50KHz");
  Serial.println("Press F to set DSD Filter at 150KHz");
  Serial.println("Press G to set the Sharp roll-off filter");
  Serial.println("Press H to set the Slow roll-off filter");
  Serial.println("Press V to set the Short delay sharp roll off filter");
  Serial.println("Press J to set the Short delay slow roll off filter");
  Serial.println("Press K to set the Superslow filter");
  Serial.println("Press M to set Sound Setting 1");
  Serial.println("Press N to set Sound Setting 2");
  Serial.println("Press S to set Sound Setting 3");  
  Serial.println("Press < to set output to non inverting");  
  Serial.println("Press > to set output to inverting");
  Serial.println("Press % to display all of AK4490's registers");
  Serial.println("Press z to display this menu");  
  Serial.println();
}

// --------------------------------------------- Si570 clock generator initialization routine -------------------------------------------------------------------
void Si570Init()                                            // resets and initializes the Si570
  {  
    // The library automatically reads the factory calibration settings of your Si570
    // but it needs to know for what frequency it was calibrated for (SI570_FACTORY_FREQUENCY defined at start of code).
    Serial.println("Initializing Si570");
    vfo = new Si570(SI570_I2C_ADDRESS, SI570_FACTORY_FREQUENCY);
    
    if (vfo->status == SI570_ERROR) 
      {
        // The Si570 is unreachable. Show an error for 2 seconds and continue.
        Serial.println("Si570 comm error or no Si570 installed");
        tft.setCursor(0, 160);
        tft.setFont(&Arimo_Bold_26);
        tft.println("Si570 comm error");
        delay(3000);
      }
  
    if (vfo->status == SI570_READY) 
      {
        // The Si570 is found.
        Serial.println("Si570 found");
        tft.setCursor(0, 160);
        tft.setFont(&Arimo_Bold_26);
        tft.println("Si570 found, DAC set-up as Clock Master");
        delay(1000);
      }
  }

// --------------------------------------------- DAC chip initialization routine -------------------------------------------------------------------
void AK4490Init()                                            // resets and initializes the DAC
{  
  Serial.println("Initializing AK4490..... ");

  tft.fillScreen(ILI9481_BLACK);        // Clear the screen
  
  tft.setFont(&Arimo_Bold_26);
  tft.setCursor(0,60);
  tft.println("Initializing AK4490.....");
  
  digitalWrite(PDN_4490, LOW);   // Reset the DAC
  delay(30);
  digitalWrite(PDN_4490, HIGH);  // Power up the DAC
  delay(100);
    
  Wire.beginTransmission(ak4490);                       // Look for AK4490
  error = Wire.endTransmission();

  tft.setCursor(0,80);
 
  if (error == 0)
  {
    Serial.println("AK4490 found!");
    tft.println("AK4490 found!");
  }
  else if (error==4) 
  {
    Serial.println("Unknown error at AK4490's address.");
    tft.println("Unknown error at AK4490's address.");
  }
  else
  {
    Serial.println("No response from AK4490!");
    tft.println("No response from AK4490!");
  }

  #ifdef DUALMONO                                     // Communicate with the second AK4490 (right channel when in dual mono mode).
  
    Wire.beginTransmission(ak4490r);
    error = Wire.endTransmission();

    tft.setCursor(0,100);
    
    if (error == 0)
    {
      Serial.println("AK4490r found!");
      tft.println("AK4490r found!");      
    }
    else if (error==4) 
    {
      Serial.println("Unknown error at AK4490r's address.");
      tft.println("Unknown error at AK4490r's address.");
    }
    else
    {
      Serial.println("No response from AK4490r!");
      tft.println("No response from AK4490r!");    
    }
  #endif DUALMONO
  
  WriteRegister(ak4490, 0x00, B10001111);    // Initialize the DAC. Sets Auto MCLK & SF, I2S 32bit and RSTN=1
  WriteRegister(ak4490, 0x01, B10100010);    // Enables Data Zero Detection.
  
  #ifdef DUALMONO
    WriteRegister(ak4490r, 0x00, B10001111);   // Initialize the right DAC. Sets Auto MCLK & SF, I2S 32bit and RSTN=1
    WriteRegister(ak4490r, 0x01, B10100010);   // Enables Data Zero Detection.
    // Set first DAC for Mono operation and Left Channel
    ChangeBit(ak4490, 0x02, 3, true);
    ChangeBit(ak4490, 0x02, 1, false);
    // Set second DAC for Mono operation and Right Channel
    ChangeBit(ak4490r, 0x02, 3, true);
    ChangeBit(ak4490r, 0x02, 1, true);
    // Enable first DAC synchronization feature
    ChangeBit(ak4490, 0x07, 0, true);
    // Enable second DAC synchronization feature
    ChangeBit(ak4490r, 0x07, 0, true);
  #endif DUALMONO

  // Assuming the settings have been read from EEPROM, apply the settings
  SetFilter(ak4490);
  EnableDirectDSD(ak4490);
  SetDSDFilter(ak4490);
  SetSoundMode(ak4490);
  SetInvertPhase(ak4490);

  #ifdef DUALMONO
    SetFilter(ak4490r);
    EnableDirectDSD(ak4490r);
    SetDSDFilter(ak4490r);
    SetSoundMode(ak4490r);
    SetInvertPhase(ak4490r);
  #endif DUALMONO

  Serial.println("Done with AK4490(s)!");
}


// ----------------------------- Function that determines the USB-to-I2S' SR --------------------------------------------------------------------

void USB_SR()
{
  // ---------------------------- Amanero ------------------------------------------------------------------------------
  //Serial.println("Running USB_SR");
  #ifdef Amanero
  /*
   * mcp_iso.GP0 = F0 (I1)
   * mcp_iso.GP1 = F1 (I2)
   * mcp_iso.GP2 = F2 (I3)
   * mcp_iso.GP3 = F3 (I4)
   * mcp_iso.GP4 = DSDOE (I5)
   */
  // Check for PCM or DSD
  if (mcp_iso.digitalRead(4) == false)    // PCM detected
  {  
    delay(200);
    if (mcp_iso.digitalRead(4) == false)  // PCM confirmed
      {
      dsdSignal = 0;
      //Serial.println("Amanero PCM");
      if (mcp_iso.digitalRead(0) == false && mcp_iso.digitalRead(1) == false && mcp_iso.digitalRead(2) == false && mcp_iso.digitalRead(3) == false)
        {
          SR_I2S = 16;      // 32KHz
          //Serial.println("Amanero 32KHz");
        }
        else if (mcp_iso.digitalRead(0) == true && mcp_iso.digitalRead(1) == false && mcp_iso.digitalRead(2) == false && mcp_iso.digitalRead(3) == false)
          {
            SR_I2S = 15;      // 44.1KHz
            //Serial.println("Amanero 44.1KHz");
          }
          else if (mcp_iso.digitalRead(0) == false && mcp_iso.digitalRead(1) == true && mcp_iso.digitalRead(2) == false && mcp_iso.digitalRead(3) == false)
            {
              SR_I2S = 14;      // 48KHz
              //Serial.println("Amanero 48KHz");
            }
            else if (mcp_iso.digitalRead(0) == true && mcp_iso.digitalRead(1) == true && mcp_iso.digitalRead(2) == false && mcp_iso.digitalRead(3) == false)
              {
                SR_I2S = 13;      // 88.2KHz
                //Serial.println("Amanero 88.2KHz");
              }
              else if (mcp_iso.digitalRead(0) == false && mcp_iso.digitalRead(1) == false && mcp_iso.digitalRead(2) == true && mcp_iso.digitalRead(3) == false)
                {
                  SR_I2S = 12;      // 96KHz
                  //Serial.println("Amanero 96KHz");
                }
                else if (mcp_iso.digitalRead(0) == true && mcp_iso.digitalRead(1) == false && mcp_iso.digitalRead(2) == true && mcp_iso.digitalRead(3) == false)
                  {
                    SR_I2S = 11;      // 176.4KHz
                    //Serial.println("Amanero 176.4KHz");
                  }
                  else if (mcp_iso.digitalRead(0) == false && mcp_iso.digitalRead(1) == true && mcp_iso.digitalRead(2) == true && mcp_iso.digitalRead(3) == false)
                    {
                      SR_I2S = 10;      // 192KHz
                      //Serial.println("Amanero 192KHz");
                    }
                    else if (mcp_iso.digitalRead(0) == true && mcp_iso.digitalRead(1) == true && mcp_iso.digitalRead(2) == true && mcp_iso.digitalRead(3) == false)
                      {
                        SR_I2S = 9;      // 352.8KHz
                        //Serial.println("Amanero 352.8KHz");
                      }
                      else if (mcp_iso.digitalRead(0) == false && mcp_iso.digitalRead(1) == false && mcp_iso.digitalRead(2) == false && mcp_iso.digitalRead(3) == true)
                        {
                          SR_I2S = 8;      // 384KHz
                          //Serial.println("Amanero 384KHz");
                        }
      }
  }
  else if (mcp_iso.digitalRead(4) == true)    // DSD detected
    {
      delay(200);
      if (mcp_iso.digitalRead(4) == true)     // DSD confirmed
        {      
          dsdSignal = 1;
          //Serial.println("Amanero DSD");
          if (mcp_iso.digitalRead(3) == true && mcp_iso.digitalRead(2) == false && mcp_iso.digitalRead(1) == false && mcp_iso.digitalRead(0) == true)
            {
              SR_I2S = 5;      // DSD64
              //Serial.println("Amanero DSD64");
            }
            else if (mcp_iso.digitalRead(3) == true && mcp_iso.digitalRead(2) == false && mcp_iso.digitalRead(1) == true && mcp_iso.digitalRead(0) == false)
              {
                SR_I2S = 4;     // DSD128
                //Serial.println("Amanero DSD128");
              }
              else if (mcp_iso.digitalRead(3) == true && mcp_iso.digitalRead(2) == false && mcp_iso.digitalRead(1) == true && mcp_iso.digitalRead(0) == true)
                {
                  SR_I2S = 3;     // DSD256
                  //Serial.println("Amanero DSD256");
                }
                  else if (mcp_iso.digitalRead(3) == true && mcp_iso.digitalRead(2) == true && mcp_iso.digitalRead(1) == false && mcp_iso.digitalRead(0) == false)
                  {
                    SR_I2S = 2;     // DSD512
                    //Serial.println("Amanero DSD512");
                  }     
        }
    }
  #endif Amanero
}


// ----------------------------- Function that reads a register's data value --------------------------------------------------------------------
byte ReadRegister(int devaddr, byte regaddr)                                // Read a data register value
  {                              
    Wire.beginTransmission(devaddr);
    Wire.write(regaddr);
    Wire.endTransmission();
    Wire.requestFrom(devaddr, 1);                 // only one byte
    if(Wire.available())                          // Wire.available indicates if data is available
      return Wire.read();                         // Wire.read() reads the data on the wire
    else
    return 99;                                     // If no data in the wire, then return 99 to indicate error
  }

// ----------------------------- Function that writes a data value to a register --------------------------------------------------------------------
void WriteRegister(int devaddr, byte regaddr, byte dataval)                // Write a data register value
  {
    Wire.beginTransmission(devaddr); // device
    Wire.write(regaddr); // register
    Wire.write(dataval); // data
    Wire.endTransmission();
  }

// ----------------------------- Function that changes a single bit in a register --------------------------------------------------------------------
void ChangeBit(int devaddr, byte regaddr, int data, boolean setting)
  {
    byte r = ReadRegister(devaddr, regaddr);
    if (setting == 1)
      {
        bitSet(r, data);
      } else
      if (setting == 0)
        {
          bitClear(r, data);
        }
      WriteRegister(devaddr, regaddr, r);
  }

// ----------------------------- Function that prints out registers for debugging --------------------------------------------------------

void PrintDebugReg(byte dac)
{
Serial.println("");
Serial.print("Register values for ");
Serial.println(dac, HEX);

byte r = 0;
Serial.print("Register 00: ");
r = ReadRegister(dac, 0x00);
Serial.println(r,BIN);
r = 0;

Serial.print("Register 01: ");
r = ReadRegister(dac, 0x01);
Serial.println(r,BIN);
r = 0;

Serial.print("Register 02: ");
r = ReadRegister(dac, 0x02);
Serial.println(r,BIN);
r = 0;

Serial.print("Register 03: ");
r = ReadRegister(dac, 0x03);
Serial.println(r,BIN);
r = 0;

Serial.print("Register 04: ");
r = ReadRegister(dac, 0x04);
Serial.println(r,BIN);
r = 0;

Serial.print("Register 05: ");
r = ReadRegister(dac, 0x05);
Serial.println(r,BIN);
r = 0;

Serial.print("Register 06: ");
r = ReadRegister(dac, 0x06);
Serial.println(r,BIN);
r = 0;

Serial.print("Register 07: ");
r = ReadRegister(dac, 0x07);
Serial.println(r,BIN);
r = 0;

Serial.print("Register 12H: ");
r = ReadRegister(dac, 0x12);
Serial.println(r,BIN);
r = 0;

Serial.print("Register 13H: ");
r = ReadRegister(dac, 0x13);
Serial.println(r,BIN);
r = 0;
}

// ----------------------------- Function that checks the pressing of the button --------------------------------------------------------------------
/* Returns key values:
 * 0 = momentarily pressed
 * 1 = sustained for 3sec
 * 2 = sustained for 6sec
*/
void buttonPressed()
  {
    if (millis() - keyPrevMillis >= keySampleIntervalMs) 
      {
        keyPrevMillis = millis();
        
        byte currKeyState = digitalRead(keyPin);
        
        if ((prevKeyState == HIGH) && (currKeyState == LOW)) 
          {
            KeyPressCount = 0;
          }
        else if ((prevKeyState == LOW) && (currKeyState == HIGH)) 
          {
            if (KeyPressCount < longKeyPressCountMax && KeyPressCount >= mediumKeyPressCountMin) 
              {
                Serial.println("Medium");
                key=2;
              }
            else 
              {
                if (KeyPressCount < mediumKeyPressCountMin) 
                  {
                    Serial.println("short");
                    key=1;
                  }
              }
          }
        else if (currKeyState == LOW) 
          {
            KeyPressCount++;
            if (KeyPressCount >= longKeyPressCountMax) 
              {
                Serial.println("long");                
                key=3;
              }
          }
        prevKeyState = currKeyState;
        }
  }

// ----------------------------- Functions used by the IR receiver --------------------------------------------------------------------

void timer2_init ()
  {
    timer.pause();
    timer.setPrescaleFactor( ((F_CPU / F_INTERRUPTS)/8) - 1);
    timer.setOverflow(7);
    timer.setChannel1Mode(TIMER_OUTPUT_COMPARE);
    timer.setCompare(TIMER_CH1, 1);  // Interrupt 1 count after each update
    timer.attachCompare1Interrupt(TIM2_IRQHandler);
        // Refresh the timer's count, prescale, and overflow
    timer.refresh();

    // Start the timer counting
    timer.resume();
  }

void TIM2_IRQHandler()                                                       // Timer2 Interrupt Handler
  {
    (void) irmp_ISR(); // call irmp ISR
     loopcount++;
   
  }

// ----------------------------- Functions used to read & write to the 24LC256 EEPROM chip --------------------------------------------------------------------

void writeData(int device, unsigned int add, byte data)               // writes a byte of data 'data' to the chip at I2C address 'device', in memory location 'add'
{
  Wire.beginTransmission(device);
  Wire.write((int)(add >> 8));                                       // left-part of pointer address
  Wire.write((int)(add & 0xFF));                                     // and the right
  Wire.write(data);
  Wire.endTransmission();
  delay(6);
  Serial.println("Data written to eeprom");
}

byte readData(int device, unsigned int add)                           // reads a byte of data from memory location 'add' in chip at I2C address 'device'
{
  byte result;                                                        // returned value
  Wire.beginTransmission(device);                                    // these three lines set the pointer position in the EEPROM
  Wire.write((int)(add >> 8));                                       // left-part of pointer address
  Wire.write((int)(add & 0xFF));                                     // and the right
  Wire.endTransmission();
  Wire.requestFrom(device,1);                                        // now get the byte of data...
  result = Wire.read();
  return result;                                                      // and return it as a result of the function readData
  Serial.print("Read from EEPROM: "); Serial.println(result);
}


// ----------------------------- Functions used to store & retrieve setting saved to the 24LC256 EEPROM chip --------------------------------------------------------------------

void writeSettings()
{
  if (readData(eeprom, 1) != dacFilter)                               // 1 = filter selection address
    {                                                                 // Check an see if there are any changes
      writeData(eeprom, 1, dacFilter);                                // If there are, write the changes in eeprom
      Serial.println("dacFilter written");
    }
  if (readData(eeprom, 2) != directDsd)                               // 2 = DSD Direct enabled
    {                                                                 // Check an see if there are any changes
      writeData(eeprom, 2, directDsd);                                // If there are, write the changes in eeprom
      Serial.println("directDsd written");
    }
  if (readData(eeprom, 3) != dsdFilter)                               // 3 = dsdFilter (50 or 150)
    {                                                                 // Check an see if there are any changes
      writeData(eeprom, 3, dsdFilter);                                // If there are, write the changes in eeprom
      Serial.println("dsdFilter written");      
    }
  if (readData(eeprom, 4) != soundMode)                               // 4 = soundMode
    {                                                                 // Check an see if there are any changes
      writeData(eeprom, 4, soundMode);                                // If there are, write the changes in eeprom
      Serial.println("soundMode written");            
    }
  if (readData(eeprom, 5) != InvertPhase)                             // 5 = Phase inversion
    {                                                                 // Check an see if there are any changes
      writeData(eeprom, 5, InvertPhase);                              // If there are, write the changes in eeprom
      Serial.println("InvertPhase written");            
    }
  if (readData(eeprom, 6) != MCLK_SET)                                // 6 = MCLK_SET
    {                                                                 // Check an see if there are any changes
      writeData(eeprom, 6, MCLK_SET);                                 // If there are, write the changes in eeprom
      Serial.println("MCLK_SET written");            
    }
  //Serial.println("Setting written");
}

void readSettings()
{
  dacFilter = readData(eeprom, 1);                                    // 1 = filter selection address
  directDsd = readData(eeprom, 2);                                    // 2 = DSD Direct enabled address
  dsdFilter = readData(eeprom, 3);                                    // 3 = dsdFilter (50 or 150) address
  soundMode = readData(eeprom, 4);                                    // 4 = soundMode address
  InvertPhase = readData(eeprom, 5);                                  // 5 = Phase inversion address
  MCLK_SET = readData(eeprom, 6);                                     // 6 = MCLK_SET address
  Serial.println("Settings read");
}

// ----------------------------- Function that initalizes the EEPROM to default values for all inputs --------------------------------------------------------------------
void initEEPROM()
  {
    Serial.println(F("Initializing EEPROM"));

    writeData(eeprom, 1, 1);
    writeData(eeprom, 2, 0);
    writeData(eeprom, 3, 50);
    writeData(eeprom, 4, 1);
    writeData(eeprom, 5, 1);
    writeData(eeprom, 6, 1);
    
    // To tell if EEPROM is initialized or not
    writeData(eeprom, 199, 199);
    writeData(eeprom, 200, 200);
    writeData(eeprom, 201, 201);
  }
