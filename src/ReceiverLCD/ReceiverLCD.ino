#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>

#define EN2_PIN 23
#define EN1_PIN 22
#define ENC_PIN 21

#define LCD_POWER_PIN 13
#define LCD_CS_PIN 15
#define LCD_SCK_PIN 14
#define LCD_MOSI_PIN 27

#define LED_RING_PIN 26
#define PIXEL_COUNT 8

// ## LCD related
#define LCD_OFF_STATE 0
#define MIXED_DASH_STATE 1
#define SPEED_ACCEL_STATE 2
#define FUEL_INFO_STATE 3
#define PDC_SENSOR_STATE 4

U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, LCD_SCK_PIN, LCD_MOSI_PIN, LCD_CS_PIN);
uint8_t lcdState = 1; // see state numbers and meanings above
uint8_t standardLcdState = lcdState; // used for displaying/setting standard state
#define LCDSTATE_COUNT 5 // number of available states of the lcd (another one is added automatically, selector screen)
#define MAX_LINES 6 // max number of selectable line
uint8_t selectedLine = 0; // for screens that use user selection

// ## LED ring related
Adafruit_NeoPixel ledRing(PIXEL_COUNT, LED_RING_PIN, NEO_GRB + NEO_KHZ800);
const uint32_t standardColor = ledRing.Color(0xFE, 0x81, 0x06);
uint8_t ledRingState = 1; // 0 = off, 1 = static standard color, ... (refer to updateLedRing() function)
#define LEDSTATE_COUNT 3

uint8_t ledBrightness = 70; // brightness of led ring from 0 (off) to 255 (full), going too high is not recommended because of power draw and danger of being blinded
const uint8_t maxLedBrightness = 140; // limits the brightness of the LED ring to a reasonable level (used for adjusting via LCD)
const uint8_t ledBrightnessStep = 7; // steps for increasing ledBrightness

#define MAXPOW_RPM 4000 // used for shift indicator, adjust this for your own car, refer to spec sheets online or use the data logger of the sender module to find this point
#define SHIFTINDICATOR_START 2000 // this is the rpm where the shift indicator will illuminate the first light
// individual color steps of the led ring gear shift indicator
const uint32_t gearShiftColors[PIXEL_COUNT] = {ledRing.Color(0x2E, 0xFF, 0x11), ledRing.Color(0xB6, 0xFF, 0x00), ledRing.Color(0xFF, 0xF6, 0x00), ledRing.Color(0xFF, 0xE0, 0x30), ledRing.Color(0xFF, 0xE0, 0x30), ledRing.Color(0xFF, 0xD5, 0x00), ledRing.Color(0xFF, 0x60, 0x21), ledRing.Color(0xFF, 0x23, 0x23)};
bool rpmBlinkOn = false;
uint64_t lastBlinkChange = 0;

// ## Preferences/settings related
Preferences preferences;
/* saved settings:
standardValues {
  lcdState: uint8_t
  ledRingState: uint8_t
  ledBrightness: uint8_t
}                         */

// ## Task handles
TaskHandle_t RenderingTask;
TaskHandle_t UserInputTask;

// ## ESP-NOW RELATED
#define ESP_NOW_CHANNEL 7 // this was chosen randomly, if you experience instability you might have to tune this, !!!also change it in the sender code!!!

typedef struct kcan_data_t {
  bool clutchPressed;
  bool brakePressed;
  bool reversed;
  uint8_t steeringWheelButtons; // each bit one button (use functions below): 2^0=VolumeUp, 2^1=VolumeDown, 2^2=UpButton, 2^3=DownButton, 2^4=TelephoneButton, 2^5=VoiceButton, 2^6=RotateButton, 2^7=DiskButton
  uint8_t PDCsensors[8]; // in cm, order: rear-L, rear-L2, rear-R2, rear-R, front-L, front-L2, front-R2, front-R
  int16_t engineTemp; // in celcius
  int16_t wheelSpeeds[4]; // in km/h (might depend on car settings), order: front-L, front-R, rear-L, rear-R
  uint16_t speed; // in km/h (might depend on car settings)
  uint16_t engineRpm;
  uint16_t range; // in km
  uint16_t airPressEngine; // in hPa
  float fuelLevel1; // in liter
  float fuelLevel2; // in liter
  float engineTorque; // in Nm, can be negative!
  float batteryVoltage; // in volts
  float avgConsumption; // in l/100km (dependent on the car settings)
  float avgSpeed; // in km/h (dependent on the car settings)
  float throttlePercentage; // throttle from 0 (foot off paddle) to 1 (flat), also includes throttle input from cruise control
  float steeringPosition; // +1 -> fully (600°) to the left, 0 -> centered, -1 -> fully (600°) to the right
  float accelerationLong; // in m/s²
  float accelerationCross; // in m/s²
} kcan_data_t;

typedef struct sd_data_t {
  uint16_t usedMiB;
  uint16_t totalMiB;
} sd_data_t;

kcan_data_t kcan_data;
sd_data_t sd_data = {0, 0};

// executed when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t * incomingData, int32_t len) {
  if(len == sizeof(kcan_data)) {
    memcpy(&kcan_data, incomingData, sizeof(kcan_data));
  } else {
    memcpy(&sd_data, incomingData, sizeof(sd_data));
  }
}


void setup() {
  Serial.begin(115200);

  pinMode(LCD_POWER_PIN, OUTPUT);
  pinMode(ENC_PIN, INPUT);
  pinMode(EN1_PIN, INPUT);

  digitalWrite(LCD_POWER_PIN, HIGH); // turn on lcd
  

  u8g2.begin(); // initialize lcd
  u8g2.setFont(u8g2_font_6x10_mr);


  // Setup WIFI mode and print MAC address
  WiFi.mode(WIFI_MODE_STA);
  WiFi.setChannel(ESP_NOW_CHANNEL);
  while (!WiFi.STA.started()) {
    delay(100);
  }
  Serial.print("Receiving data at: ");
  Serial.println(WiFi.macAddress());

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW! Restarting in 250");
    displayErrorMessage("ESP-NOW init failed!");
    delay(250);
    ESP.restart();
  }

  // register callback function
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
  Serial.println("Succesfully initialized ESP-NOW");
  
  Serial.print("Expecting ESP-NOW messages at a size of: ");
  Serial.println(sizeof(kcan_data));

  ledRing.begin(); // initialize LEDs
  ledRing.show(); // initialize pixels to off

  // get last saved preferences
  preferences.begin("standardValues", true);
  lcdState = preferences.getUChar("lcdState", lcdState);
  standardLcdState = lcdState;
  ledRingState = preferences.getUChar("ledRingState", ledRingState);
  ledBrightness = preferences.getUChar("ledBrightness", ledBrightness);
  preferences.end();

  // display BMW logo and 1 ms later start ledRing animation
  showStartupLogo(1);

  for(uint8_t i = 0; i < 3 * PIXEL_COUNT; i++) {
    ledRing.clear();
    ledRing.setBrightness(ledBrightness);
    ledRing.setPixelColor(i % PIXEL_COUNT, ledRing.gamma32(standardColor));
    ledRing.show();

    delay(83);
  }

  xTaskCreatePinnedToCore(
                    renderingTaskCode,    // Task function
                    "renderingTask",      // name of task
                    10000,                // Stack size of task
                    NULL,                 // parameter of the task
                    2,                    // priority of the task
                    &RenderingTask,       // Task handle to keep track of created task
                    1);                   // pin task to core 1

  xTaskCreatePinnedToCore(
                    userInputTaskCode,    // Task function
                    "userInputTask",      // name of task
                    10000,                // Stack size of task
                    NULL,                 // parameter of the task
                    1,                    // priority of the task
                    &UserInputTask,       // Task handle to keep track of created task
                    1);                   // pin task to core 1
}

 
void loop() {
}


void userInputTaskCode(void * params) {
  Serial.print("User input task running on core ");
  Serial.println(xPortGetCoreID());

  bool pressed = false;

  while(1) {
    // Encoder button
    if(digitalRead(ENC_PIN) == 0) {
      pressed = true;
      Serial.println("Encoder button pressed");
      
      // only need to handle states with special cases
      switch(lcdState) {
        case LCDSTATE_COUNT: // selector screen
          switch(selectedLine) {
            case 0: // "Next"
              lcdState = 1; // go back to the first page
            break;

            case 1: // "LCD off"
              Serial.println("Turning off LCD");
              lcdState = 0;
              digitalWrite(LCD_POWER_PIN, LOW);
            break;

            case 2: // "LED mode"
              Serial.println("Switching LED ring state");
              ledRingState = (ledRingState + 1) % LEDSTATE_COUNT;
            break;

            case 3: // "LED brightness"
              Serial.println("Changing LED ring brightness");
              ledBrightness = (ledBrightness + ledBrightnessStep) % (maxLedBrightness + ledBrightnessStep);
            break;

            case 4: // standard LCD state
              Serial.println("Changing standard LCD state");
              standardLcdState = (standardLcdState + 1) % LCDSTATE_COUNT;
            break;

            case 5: // save settings
              Serial.println("Saving settings");
              preferences.begin("standardValues", false);
              preferences.putUChar("lcdState", standardLcdState);
              preferences.putUChar("ledRingState", ledRingState);
              preferences.putUChar("ledBrightness", ledBrightness);
              preferences.end();
            break;
          }
        break;
        
        case 0: // turn on LCD and switch to next screen (!no break here!)
          Serial.println("Turning on LCD");
          digitalWrite(LCD_POWER_PIN, HIGH);
          delay(30); // give it a bit of time to turn on again
          u8g2.clear(); // clear display as sometimes there are artifacts when turning back on
        
        default: // show next screen
          lcdState = (lcdState + 1) % (LCDSTATE_COUNT + 1);
      }
    }


    // Encoder rotate (no direction detection only rotate, because direction is VERY inconsistent because of low-quality encoder on the LCD)
    if(digitalRead(EN1_PIN) == 0) {
      pressed = true;
      Serial.println("Rotated");
      selectedLine = (selectedLine + 1) % MAX_LINES;
    }


    // Steering wheel buttons
    if(volumeUpPressed()) {
      pressed = true;
      Serial.println("Volume up (steering wheel) pressed");
    }
    if(volumeDownPressed()) {
      pressed = true;
      Serial.println("Volume down (steering wheel) pressed");
    }
    if(upPressed()) {
      pressed = true;
      Serial.println("Up button (steering wheel) pressed");
    }
    if(downPressed()) {
      pressed = true;
      Serial.println("Down button (steering wheel) pressed");
    }
    if(telephonePressed()) {
      pressed = true;
      Serial.println("Telephone button (steering wheel) pressed");
    }
    if(voicePressed()) {
      pressed = true;
      Serial.println("Voice button (steering wheel) pressed");
    }
    if(rotatePressed()) {
      pressed = true;
      Serial.println("Rotate button (steering wheel) pressed");
    }
    if(diskPressed()) {
      pressed = true;
      Serial.println("Disk button (steering wheel) pressed");
    }

    delay(pressed ? 450 : 1); // if pressed avoid multiple triggers, if not yield
    pressed = false;
  }
}


void renderingTaskCode(void * params) {
  Serial.print("Rendering task running on core ");
  Serial.println(xPortGetCoreID());

  // Variables used for automatically switching to PDC screen when parking
  uint8_t minPDC = 255, newMinPDC = 255, lastLcdState = lcdState;
  bool lastReversed = kcan_data.reversed;

  while(1) {
    for(uint8_t i = 0; i < 8; i++)
      if(kcan_data.PDCsensors[i] < newMinPDC) newMinPDC = kcan_data.PDCsensors[i];

    if(lcdState != PDC_SENSOR_STATE
        && ((newMinPDC < 200 && minPDC > 200)
            || (kcan_data.reversed == true && lastReversed == false))) {

      lastLcdState = lcdState;
      lcdState = PDC_SENSOR_STATE;
      
    } else if(lcdState == PDC_SENSOR_STATE
              && (newMinPDC > 200 && minPDC < 200
                  || (kcan_data.reversed == false && lastReversed == true))) {

      lcdState = lastLcdState;
    }

    minPDC = newMinPDC;
    lastReversed = kcan_data.reversed;

    updateDisplay();
    updateLedRing();
    delay(55); // ~18 refreshes/s
  }
}


// displays the current vehicle information
void updateDisplay() {
  switch(lcdState) {
    case LCD_OFF_STATE: break;
 
    case MIXED_DASH_STATE:
      drawMixedDash();
    break;
    
    case SPEED_ACCEL_STATE:
      drawSpeeds();
    break;

    case FUEL_INFO_STATE:
      drawFuelInfo();
    break;

    case PDC_SENSOR_STATE:
      drawPDCsensors();
    break;

    case LCDSTATE_COUNT:
      drawSelectorScreen();
    break;
  }
}

// updated the LED ring according to current wish
void updateLedRing() {
  ledRing.clear(); // turn off everything
  ledRing.setBrightness(ledBrightness); // set current brightness

  switch(ledRingState) {
    // ring off, no need to do anything
    case 0: break;

    // static standard color
    case 1:
      ledRing.fill(ledRing.gamma32(standardColor), 0, PIXEL_COUNT);
    break;

    // rpm reactive shift indicator
    case 2:
      // complete red if rpm >= optimal shift rpm
      if(kcan_data.engineRpm > MAXPOW_RPM) {
        if(!rpmBlinkOn && millis() - lastBlinkChange >= 200) {
          rpmBlinkOn = true;
          lastBlinkChange = millis();
        } else if(rpmBlinkOn && millis() - lastBlinkChange >= 400) {
          rpmBlinkOn = false;
          lastBlinkChange = millis();
        }

        if(rpmBlinkOn) ledRing.fill(ledRing.gamma32(gearShiftColors[7]), 0, PIXEL_COUNT);
      } else {    // fill ring according to steps specified
        uint16_t rpmSteps = (uint16_t)(abs((MAXPOW_RPM - SHIFTINDICATOR_START) / PIXEL_COUNT));
        rpmBlinkOn = false;

        for(uint8_t i = 0; i < PIXEL_COUNT; i++) 
          if(kcan_data.engineRpm >= SHIFTINDICATOR_START + (rpmSteps * i)) 
            ledRing.setPixelColor(i, ledRing.gamma32(gearShiftColors[i]));
      }

    break;
  }

  ledRing.show(); // update changes
}


// ### Display rendering helper functions ###
void getEngineTempStr(char* tempStr) {
  sprintf(tempStr, "%+d C", kcan_data.engineTemp);
}

void getEnginePowerStr(char* powStr) {
  float enginePower = ((float)kcan_data.engineRpm * kcan_data.engineTorque * ((2.0f * PI) / 60.0f)) / 1000.0f;
  sprintf(powStr, "%d kW", (int)round(enginePower));
}

void getEngineTorqueStr(char* torqueStr) {
  sprintf(torqueStr, "%d Nm", (int)round(kcan_data.engineTorque));
}

void getBatteryVoltageStr(char* voltStr) {
  sprintf(voltStr, "%.2f V", kcan_data.batteryVoltage);
}

void getFuelLevelStr(char* fuelStr, uint8_t fuelIndex) {
  switch(fuelIndex) {
    case 1:
      sprintf(fuelStr, "%.1f l", kcan_data.fuelLevel1);
    break;

    case 2:
      sprintf(fuelStr, "%4.1f l", kcan_data.fuelLevel2);
    break;

    default: sprintf(fuelStr, "N/A");
  }
}

void getFuelPercentageStr(char* fuelStr) {
  sprintf(fuelStr, "%4.1f%%", ((kcan_data.fuelLevel1 + kcan_data.fuelLevel2) * 100.0f) / (2.0f * 62.0f));
}

void getRangeStr(char* rangeStr) {
  sprintf(rangeStr, "%d km", kcan_data.range);
}

void getAvgConsumptionStr(char* consStr) {
  sprintf(consStr, "%.1f l/100km", kcan_data.avgConsumption);
}

void getAvgSpeedStr(char* speedStr) {
  sprintf(speedStr, "%.1f km/h", kcan_data.avgSpeed);
}

void getPDCstr(char* pdcStr, uint8_t index, bool displayedLeft) {
  sprintf(pdcStr, displayedLeft ? "%d cm": "%3d cm", kcan_data.PDCsensors[index]);
}

void getSpeedStr(char* speedStr, uint8_t index, bool displayedLeft) {
  sprintf(speedStr, displayedLeft ? "%d" : "%3d", index < 4 ? kcan_data.wheelSpeeds[index] : kcan_data.speed);
}

void getLedRingStateStr(char* stateStr) {
  switch(ledRingState) {
    case 0:
      sprintf(stateStr, "        off");
    break;

    case 1:
      sprintf(stateStr, "     static");
    break;

    case 2:
      sprintf(stateStr, "shift light");
    break;
  }
}

void getLedRingBrightnessPercentageStr(char* percentageStr) {
  sprintf(percentageStr, "%3d%%", (int)(float(ledBrightness * 100) / float(maxLedBrightness)));
}

void getSdCardStr(char* sdStr) {
  sprintf(sdStr, "%5u/%5u MiB", sd_data.usedMiB, sd_data.totalMiB);
}

void getLcdStateStr(char* stateStr, uint8_t index) {
  switch(index) {
    case LCD_OFF_STATE:
      sprintf(stateStr, "      off");
    break;
 
    case MIXED_DASH_STATE:
      sprintf(stateStr, "    mixed");
    break;
    
    case SPEED_ACCEL_STATE:
      sprintf(stateStr, "accel/sp.");
    break;

    case FUEL_INFO_STATE:
      sprintf(stateStr, "     fuel");
    break;

    case PDC_SENSOR_STATE:
      sprintf(stateStr, "      PDC");
    break;

    default: sprintf(stateStr, "      N/A");
  }
}


// ### Logic helper functions ###
//  ## Convert steeringWheelButtons into bools
bool volumeUpPressed() {
  return kcan_data.steeringWheelButtons % 2;
}

bool volumeDownPressed() {
  return (kcan_data.steeringWheelButtons >> 1) % 2;
}

bool upPressed() {
  return (kcan_data.steeringWheelButtons >> 2) % 2;
}

bool downPressed() {
  return (kcan_data.steeringWheelButtons >> 3) % 2;
}

bool telephonePressed() {
  return (kcan_data.steeringWheelButtons >> 4) % 2;
}

bool voicePressed() {
  return (kcan_data.steeringWheelButtons >> 5) % 2;
}

bool rotatePressed() {
  return (kcan_data.steeringWheelButtons >> 6) % 2;
}

bool diskPressed() {
  return (kcan_data.steeringWheelButtons >> 7) % 2;
}


// ### LCD screen functions
// displays engineTemp, enginePower, engineTorque, batteryVoltage, clutchPressed, brakePressed, throttlePercentage, steeringPosition
void drawMixedDash() {
  char outputStr[12];

  u8g2.firstPage();
  do {
    u8g2.drawStr(1, 8, "Enginetemp.:");
    getEngineTempStr(outputStr);
    u8g2.drawUTF8(81, 8, outputStr);

    // Calculated engine power from torque and rpm
    u8g2.drawStr(1, 18, "Enginepower:");
    getEnginePowerStr(outputStr);
    u8g2.drawStr(81, 18, outputStr);

    u8g2.drawStr(1, 28, "Torque     :");
    getEngineTorqueStr(outputStr);
    u8g2.drawStr(81, 28, outputStr);

    u8g2.drawStr(1, 38, "Battvoltage:");
    getBatteryVoltageStr(outputStr);
    u8g2.drawStr(81, 38, outputStr);

    // Clutch status
    u8g2.drawButtonUTF8(32, 50, U8G2_BTN_HCENTER | U8G2_BTN_BW1 | (kcan_data.clutchPressed ? U8G2_BTN_INV : 0), 62,  0,  1, "Clutch" );

    // Brake status
    u8g2.drawButtonUTF8(96, 50, U8G2_BTN_HCENTER | U8G2_BTN_BW1 | (kcan_data.brakePressed ? U8G2_BTN_INV : 0), 62,  0,  1, "Brake" );

    // Throttle position
    u8g2.drawBox(0, 55, round(kcan_data.throttlePercentage * 128.0f), 5);

    // Steering position
    int16_t barWidth = -round(kcan_data.steeringPosition * 64.0f);
    if(barWidth >= 0) {
      u8g2.drawBox(64, 61, barWidth, 3);
    } else {
      u8g2.drawBox(64 + barWidth, 61, abs(barWidth), 3);
    }
    
  } while( u8g2.nextPage() );
}

// displays fuel levels
void drawFuelInfo() {
  // max fuel is about 62 liters which we will map to a height of 32 pixels (about half of the display)
  int16_t level1Height = 63 - (int16_t)round(kcan_data.fuelLevel1 * (32.0f/62.0f));
  int16_t level2Height = 63 - (int16_t)round(kcan_data.fuelLevel2 * (32.0f/62.0f));
  int16_t textHeight = 52; // alternatively: this puts text above fuel level but may collide with new text on top: level1Height < level2Height ? level1Height - 1 : level2Height - 1;
  char outputStr[15];

  u8g2.firstPage();
  do {
    u8g2.drawStr(1, 8, "Range:");
    getRangeStr(outputStr);
    u8g2.drawStr(43, 8, outputStr);
    
    u8g2.drawStr(1, 18, " Avg.:");
    getAvgConsumptionStr(outputStr);
    u8g2.drawStr(43, 18, outputStr);

    getAvgSpeedStr(outputStr);
    u8g2.drawStr(43, 28, outputStr);

    u8g2.setDrawColor(2); // XOR draw mode
    getFuelLevelStr(outputStr, 1);
    u8g2.drawStr(1, textHeight, outputStr);

    getFuelPercentageStr(outputStr);
    u8g2.drawStr(48, textHeight, outputStr);

    getFuelLevelStr(outputStr, 2);
    u8g2.drawStr(93, textHeight, outputStr);

    // fuel level 1
    u8g2.drawTriangle(0,level1Height, 0,64, 128,64);
    // fuel level 2
    u8g2.drawTriangle(0,level1Height, 128,level2Height, 128,64);
    u8g2.setDrawColor(1); // normal draw mode
  } while( u8g2.nextPage() );
}

// displays information from the PDC
void drawPDCsensors() {
  char outputStr[8];

  u8g2.firstPage();
  do {
    getPDCstr(outputStr, 5, true); // front-L2
    u8g2.drawStr(15, 8, outputStr);

    getPDCstr(outputStr, 4, true); // front-L
    u8g2.drawStr(1, 19, outputStr);

    getPDCstr(outputStr, 6, false); // front-R2
    u8g2.drawStr(76, 8, outputStr);

    getPDCstr(outputStr, 7, false); // front-R
    u8g2.drawStr(92, 19, outputStr);

    getPDCstr(outputStr, 0, true); // rear-L
    u8g2.drawStr(1, 52, outputStr);

    getPDCstr(outputStr, 1, true); // rear-L2
    u8g2.drawStr(15, 63, outputStr);

    getPDCstr(outputStr, 3, false); // rear-R
    u8g2.drawStr(92, 52, outputStr);

    getPDCstr(outputStr, 2, false); // rear-R2
    u8g2.drawStr(76, 63, outputStr);

    // center radar-thingy
    u8g2.drawDisc(63, 31, 2);
    // inner
    u8g2.drawArc(63, 31, 6, 11, 53);
    u8g2.drawArc(63, 31, 6, 75, 117);
    u8g2.drawArc(63, 31, 6, 139, 181);
    u8g2.drawArc(63, 31, 6, 203, 245);
    // middle
    u8g2.drawArc(63, 31, 12, 11, 53);
    u8g2.drawArc(63, 31, 12, 75, 117);
    u8g2.drawArc(63, 31, 12, 139, 181);
    u8g2.drawArc(63, 31, 12, 203, 245);
    // outer
    u8g2.drawArc(63, 31, 18, 11, 53);
    u8g2.drawArc(63, 31, 18, 75, 117);
    u8g2.drawArc(63, 31, 18, 139, 181);
    u8g2.drawArc(63, 31, 18, 203, 245);
  } while( u8g2.nextPage() );
}

// displays wheel speeds and overall speed
void drawSpeeds() {
  char outputStr[6];

  // 1 g translates to 40 pixels
  int16_t xOffset = (int16_t)round((kcan_data.accelerationCross / 9.81f) * 40.0f);
  int16_t yOffset = (int16_t)round((kcan_data.accelerationLong / 9.81f) * 40.0f);

  u8g2.firstPage();
  do {
    u8g2.setDrawColor(2); // XOR mode

    getSpeedStr(outputStr, 0, true); // front-L
    u8g2.drawStr(1, 8, outputStr);
    u8g2.drawStr(1, 18, "km/h");

    getSpeedStr(outputStr, 1, false); // front-R
    u8g2.drawStr(110, 8, outputStr);
    u8g2.drawStr(104, 18, "km/h");

    getSpeedStr(outputStr, 2, true); // rear-L
    u8g2.drawStr(1, 53, outputStr);
    u8g2.drawStr(1, 63, "km/h");

    getSpeedStr(outputStr, 3, false); // rear-R
    u8g2.drawStr(110, 53, outputStr);
    u8g2.drawStr(104, 63, "km/h");

    // g-force square
    //  inner "0.5 g"
    u8g2.drawLine(42, 31, 63, 10);
    u8g2.drawLine(64, 10, 85, 31);
    u8g2.drawLine(64, 53, 85, 32);
    u8g2.drawLine(42, 32, 63, 53);
    //  outer "1 g"
    u8g2.drawLine(64, 73, 105, 32);
    u8g2.drawLine(74, 0, 105, 31);
    u8g2.drawLine(22, 31, 53, 0);
    u8g2.drawLine(22, 32, 63, 73);
    u8g2.drawStr(108, 35, "1g"); // "axis label"

    // g-force indicator, center position: 61, 29
    u8g2.drawBox(61 + xOffset, 29 + yOffset, 6, 6);

    u8g2.setDrawColor(1); // normal mode
  } while( u8g2.nextPage() );
}

// always the last page
void drawSelectorScreen() {
  char outputStr[17];

  u8g2.firstPage();
  do {
    u8g2.setDrawColor(2); // XOR mode

    u8g2.drawBox(0, 10 * selectedLine, u8g2.getDisplayWidth(), 10);

    u8g2.drawStr(1, 8, "Next");
    u8g2.drawStr(1, 18, "LCD off");

    u8g2.drawStr(1, 28, "LED mode");
    getLedRingStateStr(outputStr);
    u8g2.drawStr(62, 28, outputStr);

    u8g2.drawStr(1, 38, "LED brightness");
    getLedRingBrightnessPercentageStr(outputStr);
    u8g2.drawStr(104, 38, outputStr);

    u8g2.drawStr(1, 48, "Stand. LCD");
    getLcdStateStr(outputStr, standardLcdState);
    u8g2.drawStr(74, 48, outputStr);

    u8g2.drawStr(1, 58, "Save settings");

    u8g2.drawStr(1, 68, "SD");
    getSdCardStr(outputStr);
    u8g2.drawStr(38, 68, outputStr);

    u8g2.setDrawColor(1); // normal mode
  } while( u8g2.nextPage() );
}

// displays a simple one line string on the lcd
void displayErrorMessage(char * message) {
  u8g2.firstPage();
  do {
    u8g2.setCursor(1,8);
    u8g2.print(message);
  } while ( u8g2.nextPage() );
}

// displays a simple BMW logo on the lcd for the given amount of time
void showStartupLogo(uint32_t duration) {
  // 'bmw-2-logo-png-transparent', 128x64px
  const uint8_t bmwLogo [] PROGMEM = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x00, 0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x1c, 0x1c, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x14, 0x1c, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x14, 0x1e, 0x00, 0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x24, 0x1a, 0x00, 0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0x07, 0x00, 0x24, 0x1a, 0x00, 0xe0, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0x03, 0x00, 0x64, 0x19, 0x00, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x44, 0x19, 0x00, 0x88, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0xc4, 0x18, 0x00, 0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x04, 0xfe, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x36, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfe, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x3f, 0xa1, 0x00, 0x50, 0x0f, 0x00, 0x63, 0xfc, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x9f, 0xf1, 0x01, 0xaa, 0x7f, 0x00, 0x59, 0xf8, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xdf, 0x10, 0x03, 0x55, 0xff, 0x81, 0x27, 0xf8, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x8f, 0x09, 0xa3, 0xaa, 0xff, 0x87, 0x30, 0xf0, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x07, 0x0f, 0x51, 0x55, 0xff, 0x0f, 0x10, 0xe3, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x07, 0x86, 0xa9, 0xaa, 0xff, 0x1f, 0xc8, 0xe0, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x07, 0xcc, 0x54, 0x55, 0xff, 0x3f, 0x34, 0xe0, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x03, 0x78, 0xaa, 0xaa, 0xff, 0x7f, 0x0c, 0xc0, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x03, 0x30, 0x55, 0x55, 0xff, 0xff, 0x00, 0xc0, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0xaa, 0xaa, 0xff, 0xff, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x55, 0x55, 0xff, 0xff, 0x01, 0x80, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x01, 0x80, 0xaa, 0xaa, 0xff, 0xff, 0x01, 0x80, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x01, 0x40, 0x55, 0x55, 0xff, 0xff, 0x03, 0x80, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x00, 0x80, 0xaa, 0xaa, 0xff, 0xff, 0x03, 0x00, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x00, 0x40, 0x55, 0x55, 0xff, 0xff, 0x03, 0x00, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x00, 0xa0, 0xaa, 0xaa, 0xff, 0xff, 0x07, 0x00, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x00, 0x40, 0x55, 0x55, 0xff, 0xff, 0x07, 0x00, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x00, 0xa0, 0xaa, 0xaa, 0xff, 0xff, 0x07, 0x00, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x00, 0x40, 0x55, 0x55, 0xff, 0xff, 0x07, 0x00, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x00, 0xe0, 0xff, 0xff, 0xaa, 0xaa, 0x02, 0x00, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x00, 0xe0, 0xff, 0xff, 0x55, 0x55, 0x05, 0x00, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x00, 0xe0, 0xff, 0xff, 0xaa, 0xaa, 0x02, 0x00, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x00, 0xe0, 0xff, 0xff, 0x55, 0x55, 0x05, 0x00, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x00, 0xc0, 0xff, 0xff, 0xaa, 0xaa, 0x02, 0x00, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x00, 0xc0, 0xff, 0xff, 0x55, 0x55, 0x01, 0x00, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x01, 0xc0, 0xff, 0xff, 0xaa, 0xaa, 0x02, 0x80, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x01, 0x80, 0xff, 0xff, 0x55, 0x55, 0x01, 0x80, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x01, 0x80, 0xff, 0xff, 0xaa, 0xaa, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0xff, 0xff, 0x55, 0x55, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x03, 0x00, 0xff, 0xff, 0xaa, 0xaa, 0x00, 0xc0, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x03, 0x00, 0xfe, 0xff, 0x55, 0x55, 0x00, 0xc0, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x07, 0x00, 0xfc, 0xff, 0xaa, 0x2a, 0x00, 0xe0, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x07, 0x00, 0xf8, 0xff, 0x55, 0x15, 0x00, 0xe0, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x0f, 0x00, 0xf0, 0xff, 0xaa, 0x0a, 0x00, 0xe0, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x0f, 0x00, 0xe0, 0xff, 0x55, 0x05, 0x00, 0xf0, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x80, 0xff, 0xaa, 0x00, 0x00, 0xf8, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x00, 0xfe, 0x55, 0x00, 0x00, 0xf8, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x00, 0xf0, 0x0a, 0x00, 0x00, 0xfc, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0x03, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x00, 0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
  };

  u8g2.clearBuffer();
  u8g2.drawXBMP(0,0,128,64,bmwLogo);
  u8g2.sendBuffer();

  delay(duration);
}