/*
 * 
 *    GCU firmware rev1
 *    by Tim Eastwood
 * 
 */

#include <EEPROM.h>

//128x128 freetronics LCD library
#include <FTOLED.h>
#include <FTOLED_Colours.h>
#include <progmem_compat.h>
#include <fonts/SystemFont5x7.h>
#include <fonts/Droid_Sans_12.h>
#include <fonts/Droid_Sans_24.h>
//pin defines for LCD (suit PCB rev1)
const byte pin_cs = 7;
const byte pin_dc = 2;
const byte pin_reset = 3;
OLED oled(pin_cs, pin_dc, pin_reset);

//general pin defines (suit PCB rev1)
#define glowPin 18 //controls the glow plug relay
#define lampPin 17 //controls the dash light
#define buzzPin 10 //the alarm / alert buzzer output pin
#define buttonPin 4 //the button for changing settings
#define lcdButtonPin 5 //the button for changing settings
#define tempPin A0 //reads the temperature of the engine
#define voltPin A1 //reads the input voltage

//voltage sensor properties
#define voltR1 30000.0
#define voltR2 7500.0
#define voltNumSamples 8 //how many temp samples to average out
#define voltSampleInterval 20 //how often to sample the temp (ms)

//thermistor properties
#define thNominalOhms 10000 //resistance at nominal temp
#define thNominalTemp 25 //temp. for nominal resistance (almost always 25 C)
#define thBCoeff 3950 //The beta coefficient of the thermistor (usually 3000-4000)
#define thPullupOhms 10000 //the value of the pullup resistor
#define tempNumSamples 8 //how many temp samples to average out
#define tempSampleInterval 20 //how often to sample the temp (ms)

//general settings
#define timeBeforeAlarm 60 //seconds to wait after boot up before activating alarms
#define alarmDelay 2 //seconds before an error state becomes an alarm
#define shortPressDuration 10 //button short press duration (ms)
#define longPressDuration 1000 //button long press duration (ms)
#define longBeepDuration 100 //long beep duration
#define longBeepSilence 50 //silence between long beeps
#define shortBeepDuration 80 //short beep duration
#define shortBeepSilence 50 //silence between long beeps
#define menuInterval 3000 //how much time the user gets to select a menu option before moving on
//#define lcdRefreshRate 10 //lcd update frequency (ms)

//settings variables
int preGlowDuration; //how long to pre glow
int glowDuration; //how long to glow
int afterGlowDuration; //how long to keep 'warm'
int afterGlowInterval; //x seconds on, x seconds off
int glowTempThreshold; //above this engine temp, don't glow
int tempAlertThreshold; //quiet temp alert
int tempCriticalThreshold; //LOUD temp alarm
int voltAlarmMode; //mode for voltage warning

//serial diagnostics
#define serialDiag true
#define serialBaud 115200
#define beepDiag false

unsigned long curTime; //stores the current time index each frame

//selectable menu options
static int preGlowDurationOptions[5] = { 4, 6, 8, 10, 12 };
static int glowDurationOptions[9] = { 6, 10, 15, 20, 25, 30, 40, 60, 120 };
static int afterGlowDurationOptions[7] = { 0, 180, 240, 300, 360, 480, 600 };
static int afterGlowIntervalOptions[3] = { 1, 2, 3 };
static int glowTempThresholdOptions[8] = { 0, 50, 55, 60, 65, 70, 75, 80 };
static int tempAlertThresholdOptions[10] = { 0, 95, 98, 100, 105, 110, 115, 120, 125, 130 };
static int tempCriticalThresholdOptions[10] = { 0, 95, 100, 105, 110, 115, 120, 125, 130, 135 };
static int voltAlarmModeOptions[5] = { 0, 1, 2, 3, 4 };
//option 9 = reset to default settings

/*
 * END DECLARATIONS
 * START FUNCTIONS
 */

//all this code... just to schedule some beeps so delays aren't used. far out.
boolean beeping = false;
unsigned int beepCount = 0;
boolean beepLong = false;
boolean beepInterrupt = false;
unsigned long beepingStartTime;
unsigned long nextBeepStartTime;
unsigned int beepCountTotal = 0;
unsigned int beepsCounted = 0;
boolean nextBeepSilent = false;
void beep() {

  //check if beeping should be cancelled
  if (beepInterrupt) {
    beepLong = false;
    beeping = false;
    beepsCounted = 0;
    beepCount = 0;
    nextBeepSilent = false;
    beepInterrupt = false;
    if (serialDiag && beepDiag) {
      Serial.println("A beep was interrupted");
    }
    return;
  }

  //check if doing a longer beep
  unsigned int beepDuration = shortBeepDuration;
  unsigned int beepSilence = shortBeepSilence;
  if (beepLong) {
    beepDuration = longBeepDuration;
    beepSilence = longBeepSilence;
  }

  //do the first beep and set the counters and timers
  if (!beeping) {
    if (serialDiag && beepDiag) {
      Serial.println("Beeping started with " + (String)beepCount + " beeps");
    }
    beeping = true;
    beepingStartTime = curTime;
    beepCount = beepCount * 2 - 1; //add beep count for silence between beeps
    beepsCounted = 1;
    tone(buzzPin, 400, beepDuration);
    nextBeepSilent = true;
    nextBeepStartTime = beepingStartTime + beepDuration;
  }

  //perform additional beeps if any remain and if the start time has either arrived or elapsed
  else if (beepCount > beepsCounted && curTime >= nextBeepStartTime){
    beepsCounted++;
    if (nextBeepSilent) {
      //tone(buzzPin, 31, beepSilence);
      nextBeepStartTime = curTime + beepDuration;
    }
    else {
      tone(buzzPin, 400, beepDuration);
      nextBeepStartTime = curTime + beepSilence;
    }
    nextBeepSilent = !nextBeepSilent;
    if (serialDiag && beepDiag) {
      Serial.println("Have done " + (String)beepsCounted + " beeps");
    }
  }

  //reset all the counters
  else if (beepCount <= beepsCounted) {
    beepLong = false;
    beeping = false;
    beepsCounted = 0;
    beepCount = 0;
    nextBeepSilent = false;   
    if (serialDiag && beepDiag) {
      Serial.println("Finished beeps");
    } 
  }

}

//the menu - uses beeps and button presses to change settings
unsigned int menuState = 0; //whether we're in the menu (1), changing an option(2), or nothing (0)
unsigned long menuTime = 0;
unsigned int menuOption = 1; //which menu context item we're at
unsigned int menuAtOption = 1; //stores which setting we've selected
void menu() {

  if (menuState == 0) { return; } //skip if not in menu
  if (curTime < menuTime) { return; } //skip if waiting for a delay

  //the maximum 'selection' for each menu item, ie number of beeps
  static int menuMaxOption = 5; //setting 1
  if (menuAtOption == 2) { menuMaxOption = 9; } //setting 2
  else if (menuAtOption == 3) { menuMaxOption = 7; } //etc
  else if (menuAtOption == 4) { menuMaxOption = 3; }
  else if (menuAtOption == 5) { menuMaxOption = 8; }
  else if (menuAtOption == 6) { menuMaxOption = 10; }
  else if (menuAtOption == 7) { menuMaxOption = 10; }
  else if (menuAtOption == 8) { menuMaxOption = 5; }
  else if (menuAtOption == 9) { menuMaxOption = 1; }
  if (menuState == 1) { menuMaxOption = 9; } //top level

  //menu top level
  if (curTime - menuTime > menuInterval * menuOption) {
    menuOption++; //go to next menu option
    if (menuOption > menuMaxOption) {
      menuTime = curTime;
      menuOption = 1;  //reset to start
    }
    beepLong = true;
    beepCount = menuOption; //beep the current menuOption
    beep();
    if (serialDiag) {
      Serial.println("At menu option: " + (String)menuOption);
    }
  }
  
}

//actions on short press of button
void buttonShortPress() {

  if (serialDiag) { Serial.println("Short press"); }
  if (beeping) { beepInterrupt = true; }

  //action if not in menu
  if (menuState == 0) {
    return;
    /*
     * FUTURE: next LCD display mode / gauge
     */
  }

  //action if at menu top level
  else if (menuState == 1) {
    menuTime = curTime + menuInterval; //add a delay (menuInterval) before continuing with beeps
    menuState = 2;
    menuAtOption = menuOption;
    menuOption = 0;
  }

  //action if changing an option in the menu
  else if (menuState == 2) {

    //set the newly selected option
    if (menuAtOption < 9) {
      int eepromAddr = menuAtOption - 1;
      int eepromVal = menuOption - 1;
      if (serialDiag) {
        Serial.println("Writing value " + (String)eepromVal + " to address " + eepromAddr);
      }
      EEPROM.write(eepromAddr, eepromVal);
      eepromInit(false); //apply the option now
      menuState = 0;
      menuOption = 0;
    }

    //if selected 'restore to default'
    else {
      eepromInit(true);
      menuState = 0;
      menuOption = 0;
      tone(buzzPin, 600, 2000);
    }
  }
  
}

//actions on long press of button
void buttonLongPress() {

  if (serialDiag) { Serial.println("Long press"); }
  if (beeping) { beepInterrupt = true; }

  if (menuState == 0) {
    menuState = 1; //enter the menu
    menuOption = 0;
    menuTime = curTime + menuInterval; //add a delay (menuInterval) before continuing with beeps
  }
  else if (menuState > 0) {
    menuState = 0; //exit the menu
    menuOption = 0;
  }
  
}

//setup the configuration
void eepromInit(boolean initialWrite) {

  //populate with default values
  if (initialWrite) {
    EEPROM.write(0, 1); //pre glow duration - 6 sec
    EEPROM.write(1, 2); //glow duration - 15 sec
    EEPROM.write(2, 0); //after glow duration - disabled
    EEPROM.write(3, 1); //after glow interval - 2 sec
    EEPROM.write(4, 0); //glow temp threshold - 65 deg
    EEPROM.write(5, 3); //temp alert threshold - 100 deg
    EEPROM.write(6, 4); //temp critical threshold - 110 deg
    EEPROM.write(7, 4); //voltage alarm mode - both
    if (serialDiag) {
      Serial.println("EEPROM values set to defaults!");
    }
  }

  //pre glow duration
  int value = EEPROM.read(0);
  preGlowDuration = preGlowDurationOptions[value];

  //glow duration
  value = EEPROM.read(1);
  glowDuration = glowDurationOptions[value];

  //after glow duration
  value = EEPROM.read(2);
  afterGlowDuration = afterGlowDurationOptions[value];

  //after glow interval
  value = EEPROM.read(3);
  afterGlowInterval = afterGlowIntervalOptions[value];

  //glow temp threshold
  value = EEPROM.read(4);
  glowTempThreshold = glowTempThresholdOptions[value];

  //temp alert threshold
  value = EEPROM.read(5);
  tempAlertThreshold = tempAlertThresholdOptions[value];

  //temp critical threshold
  value = EEPROM.read(6);
  tempCriticalThreshold = tempCriticalThresholdOptions[value];

  //voltage alarm mode
  value = EEPROM.read(7);
  voltAlarmMode = voltAlarmModeOptions[value];

  if (serialDiag) {
      Serial.println("EEPROM values read:");
      Serial.println("  preGlowDuration: " + (String)preGlowDuration + " sec");
      Serial.println("  glowDuration: " + (String)glowDuration + " sec");
      Serial.println("  afterGlowDuration: " + (String)afterGlowDuration + " sec");
      Serial.println("  afterGlowInterval: " + (String)afterGlowInterval + " sec");
      Serial.println("  glowTempThreshold: " + (String)glowTempThreshold + " degC");
      Serial.println("  tempAlertThreshold: " + (String)tempAlertThreshold + " degC");
      Serial.println("  tempCriticalThreshold: " + (String)tempCriticalThreshold + " degC");
      Serial.println("  voltAlarmMode: " + (String)voltAlarmMode);
  }
  
}

//control the glow plugs
int glowState = 0; //stores the state of the glow sequence
unsigned long glowTimer = 0; //stores the time index for use during the glow sequence
unsigned long afterGlowTimer = 0; //stores the time index for use during the glow sequence
int lampFlashes = 0; //how many times we've flashed the lamps if not glowing
void glow() {

  //make sure there are temperature samples before intiating glow
  if (curTime < 160) {
    return;
  }

  //pre glow
  if (glowState == 0) {
    
    if (glowTempThreshold > 0 && getTemp() > glowTempThreshold) { //skip glow sequence above certain temp threshold
      glowState = 5; //set to 5 to flash the dash lamp
    }
    else {
      glowState = 1;
      glowTimer = curTime;
      digitalWrite(glowPin, LOW); //power the glow plugs
      digitalWrite(lampPin, LOW); //turn on the dash light / turn on lamp A
    }
  }

  //actual glow
  else if (glowState == 1 && curTime - glowTimer > preGlowDuration * 1000){ //if after the preglow and before the end of actual glow
    glowState = 2;
    glowTimer = curTime;
    digitalWrite(glowPin, LOW); //power the glow plugs
    digitalWrite(lampPin, HIGH); //turn off the dash light / change to lamp B
  }

  //after glow (x seconds on, x seconds off)
  else if (glowState == 2 && curTime - glowTimer > glowDuration * 1000) {

    //if disabled, go straight to OFF
    if (afterGlowDuration == 0) {
      digitalWrite(glowPin, HIGH);
      digitalWrite(lampPin, HIGH);
      glowState = 4;
    }

    //if we should be afterglowing
    else if (curTime - glowTimer < afterGlowDuration * 1000) {
  
      //toggle state every x seconds
      if (curTime - afterGlowTimer > afterGlowInterval * 1000) {
        digitalWrite(glowPin, !digitalRead(glowPin)); //toggle the powered state of the glow plugs
        afterGlowTimer = curTime;
      }
    }
    
    //finish up after glow
    else if (curTime - glowTimer > afterGlowDuration * 1000) {
      glowState = 4; //set this to 4 so the glow system won't run again when millis() gets reset to 0
      digitalWrite(glowPin, HIGH);
      digitalWrite(lampPin, HIGH);
    }
  }

  //flash lamps if we're not going to use glow
  else if (glowState == 5) {    
    if (curTime - glowTimer > 500 && lampFlashes < 8) {
      digitalWrite(lampPin, !digitalRead(lampPin)); //toggle the powered state of the glow plugs
      glowTimer = curTime;
      lampFlashes++;
    }
    else if (lampFlashes == 8){
      digitalWrite(lampPin, HIGH);
      glowState = 4;
    }
  }
  
}

//get voltage samples
int voltSamples[voltNumSamples]; //stores temp samples for averaging
unsigned long voltTimeLast = 0; //stores the time interval for temp sampling
int voltCurSample = 0; //stores the next array index for averaging temp values
void sampleVolts() {

  if (curTime - voltTimeLast > voltSampleInterval) {
    //take a sample of the analog value
    voltSamples[voltCurSample] = analogRead(voltPin);
    voltCurSample++;
    if (voltCurSample == voltNumSamples) {
      voltCurSample = 0;
    }
  }
}

//returns the current voltage to 1 decimal place
float getVolts(){

  // average all the samples
  float average = 0;
  for (int i = 0; i < voltNumSamples; i++) {
     average += voltSamples[i];
  }
  average /= voltNumSamples; 

  average = (average * 5.0) / 1024.0; 
  average = average / (voltR2/(voltR1+voltR2));
  average -= 0.45; //calibration for voltage sensor
 
  int result = average * 10.0; //multiply by 10 and convert to into to drop off decimal places
  return (float)result / 10.0; //divide by 10 and convert back to float to get the single decimal place back
}   

//get temperature samples
int tempSamples[tempNumSamples]; //stores temp samples for averaging
unsigned long tempTimeLast = 0; //stores the time interval for temp sampling
int tempCurSample = 0; //stores the next array index for averaging temp values
void sampleTemp() {

  if (curTime - tempTimeLast > tempSampleInterval) {
    //take a sample of the analog value
    tempSamples[tempCurSample] = analogRead(tempPin);
    tempCurSample++;
    if (tempCurSample == tempNumSamples) {
      tempCurSample = 0;
    }
  }
}

//returns the current temp to 1 decimal place
float getTemp(){

  // average all the samples
  float average = 0;
  for (int i = 0; i < tempNumSamples; i++) {
     average += tempSamples[i];
  }
  average /= tempNumSamples;

  // convert the value to resistance
  average = 1023 / average - 1;
  average = thPullupOhms / average;
 
  float steinhart;
  steinhart = average / thNominalOhms;          // (R/Ro)
  steinhart = log(steinhart);                   // ln(R/Ro)
  steinhart /= thBCoeff;                        // 1/B * ln(R/Ro)
  steinhart += 1.0 / (thNominalTemp + 273.15);  // + (1/To)
  steinhart = 1.0 / steinhart;                  // Invert
  steinhart -= 273.15;                          // convert to deg C

  int result = steinhart * 10.0; //multiply by 10 and convert to into to drop off decimal places
  return (float)result / 10.0; //divide by 10 and convert back to float to get the single decimal place back
}

//sound alarms
boolean tempAlert = false;
unsigned int long tempTime;
boolean voltsAboveThreshold = false;
unsigned int long voltsTime;
void alarms() {

  //alarm frequency: 4000hertz

  //skip alarms if we've only just booted up
  if (menuState != 0 || curTime < timeBeforeAlarm * 1000) { return; } 

  float curVolts = getVolts();

  //if temp alarm is enabled
  if (tempAlertThreshold > 0){

    //get values
    float curTemp = getTemp();

    //if temp above alarm threshold
    if (curTemp > tempAlertThreshold) {

      //track how long it's been above the alarm threshold
      if (!tempAlert) {
        tempAlert = true;
        tempTime = curTime;
      }

      //if alarm should sound
      else if (curTime - tempTime > alarmDelay) {

        //if temp is critical and critical alert is enabled
        if (tempCriticalThreshold > 0 || curTemp > tempCriticalThreshold) {
          
        }
        //if temp is just alert
        else {
          
        }
      }
    }
    else {
      tempAlert = false; //reset
    }
  }
  
}

//track button presses
unsigned long buttonTime = 0; //when the button was first pressed
boolean lastButtonState = false;
boolean shortPressed = false;
boolean longPressed = false;
void sampleButtons() {

  //get button state
  boolean curButtonState = false;
  if (!digitalRead(lcdButtonPin) || !digitalRead(buttonPin)) { //invert boolean because buttons are LOW when pressed
    curButtonState = true; 
  }

  //if the button state has changed
  if (curButtonState != lastButtonState){

    //the button has been pressed
    if (curButtonState) {
      buttonTime = curTime;
      lastButtonState = true;
    }

    //the button was let go
    else {
      if (shortPressed && !longPressed) {
        buttonShortPress();
      }
      else if (longPressed) {
        buttonLongPress();
      }
      lastButtonState = false;
      shortPressed = false;
      longPressed = false;
    }
  }

  //the button is still held down
  else if (curButtonState) {
    int buttonTimeOn = curTime - buttonTime; //how long the button has been held for
    if (!shortPressed && buttonTimeOn > shortPressDuration) {
      shortPressed = true;
      tone(buzzPin, 480, 50);
    }
    if (!longPressed && buttonTimeOn > longPressDuration) {
      longPressed = true;
      tone(buzzPin, 480, 150);
    }
  }

}

//output diagnostic information to Serial
unsigned int long diagTime = 0;
unsigned int fps = 0;
void diagnostics() {
  if (curTime - diagTime > 1000) {
    String serString = "F:";
    serString += (String)fps; //print framerate
    serString += ",T:";
    serString += (String)getTemp(); //print temp
    serString += ",V:";
    serString += (String)getVolts(); //print volts
    serString += ",MS:";
    serString += (String)menuState;
    serString += ",MO:";
    serString += (String)menuOption;
    serString += ",MaO:";
    serString += (String)menuAtOption;
    fps = 0;
    diagTime = curTime;
    Serial.println(serString);
  }
}

unsigned int startupCounter = 0;
static int startInterval = 150;
unsigned int long startupTime = 0;
void startup() {
  if (startupCounter == 0) {
    startupCounter = 1;
    startupTime = curTime;
    tone(buzzPin, 400, startInterval);
  }
  else if (startupCounter == 1 && curTime - startupTime > startInterval) {
    startupCounter = 2;
    startupTime = curTime;
    tone(buzzPin, 600, startInterval);
  }
  else if (startupCounter == 2 && curTime - startupTime > startInterval) {
    startupCounter = 3;
    startupTime = curTime;
    tone(buzzPin, 800, startInterval);
  }
  else if (startupCounter == 3 && curTime - startupTime > startInterval) {
    startupCounter = 4;
    startupTime = curTime;
    tone(buzzPin, 1000, startInterval);
  }
  else if (startupCounter == 4 && curTime - startupTime > startInterval) {
    startupCounter = 5;
    startupTime = curTime;
    tone(buzzPin, 1200, startInterval);
  }
  else if (startupCounter == 5) { return; }
}

//update the LCD
#define lcdDemo true
int static lcdRefreshRate = 10;
unsigned int long lcdTimer;
int lcdMode = 0; //mode of the LCD:
/*
 * 0 = glow status, then switch to volts and temp bar graph
 * 1 = always show glow status
 * 2 = never show glow status
 */
 int lcdLastMode = 1;
 int lcdLastReading1 = 0;
void lcdDraw() {

  //limit frame rate and wait until startup complete before drawing
  if (startupCounter < 5 || curTime - lcdTimer < lcdRefreshRate) { return; }
  
  lcdTimer = curTime;

  if (lcdDemo) {

    //glow plugs
    if (lcdMode == 0) {
      
      if (lcdMode != lcdLastMode) {
  
        oled.clearScreen();
        oled.selectFont(Droid_Sans_24);
        lcdLastMode = lcdMode;
        oled.drawString(10, 4, "Glow Plugs", PALETURQUOISE, BLACK);
        oled.selectFont(Droid_Sans_12);
        oled.drawString(4, 110, "Heating...", ORANGERED, BLACK);
        oled.drawBox(0,0,128,128,1,ORANGE);
        
      }

      
      
    }
    
  }
  
}

void setup() {
  if (serialDiag) {
    Serial.begin(serialBaud);
    delay(500);
  }
  oled.begin();
  pinMode(glowPin, OUTPUT); digitalWrite(glowPin, HIGH);
  pinMode(lampPin, OUTPUT); digitalWrite(lampPin, HIGH);
  pinMode(buzzPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(lcdButtonPin, INPUT_PULLUP);
  pinMode(tempPin, INPUT);
  pinMode(voltPin, INPUT);
  eepromInit(false);
  oled.selectFont(SystemFont5x7);
  oled.drawString(0, 0, "...", WHITE, BLACK);
  //tone(buzzPin, 4000, 2000);
}

void loop() {

  //capture current time index this frame (milliseconds)
  curTime = millis();

  //menu
  menu();

  //startup sound
  startup();

  //sample button state
  sampleButtons();

  //call the button beeps
  if (beeping) {
    beep();
  }

  //sample temperature
  sampleTemp();

  //sample voltage
  sampleVolts();

  //operate glow plugs
  if (glowState < 4 || glowState == 5) {
    glow();
  }

  //output diagnostics
  if (serialDiag) {
    fps++;
    diagnostics();
  }

  //alarms();

  lcdDraw();

}

//draws a thick line (3x3 pixels for each dot, with coord in the centre)
void drawCircularGaugeLine(OLED_Color color, int needleAngle) {

  //calculate coords for gauge line
  float angle = (180 - needleAngle) + 270;
  if (angle > 360) {
    angle -= 360;
  }
  angle *= 0.0174533;
  float calc = sin(angle);
  int x1 = 64 - calc * 61.0;
  int x2 = 64 - calc * 32.0;
  calc = cos(angle);
  int y1 = 64 + calc * 61.0;
  int y2 = 64 + calc * 32.0;
  
  oled.drawLine(x1, y1, x2, y2, color);
  oled.drawLine(x1 + 1, y1 + 1, x2 + 1, y2 + 1, color);
  oled.drawLine(x1 - 1, y1 - 1, x2 - 1, y2 - 1, color);
  oled.drawLine(x1 - 1, y1 + 1, x2 - 1, y2 + 1, color);
  oled.drawLine(x1 + 1, y1 - 1, x2 + 1, y2 - 1, color);
  oled.drawLine(x1, y1 - 1, x2, y2 - 1, color);
  oled.drawLine(x1, y1 + 1, x2, y2 + 1, color);
  oled.drawLine(x1 - 1, y1, x2 - 1, y2, color);
  oled.drawLine(x1 + 1, y1, x2 + 1, y2, color);

  oled.drawCircle(63,63,63,GREEN);
  oled.drawCircle(63,63,62,GREEN);
  
}

