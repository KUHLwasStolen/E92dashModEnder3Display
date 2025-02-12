#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <U8g2lib.h>

#define EN2_PIN 23
#define EN1_PIN 22
#define ENC_PIN 21
#define LCD_POWER_PIN 27
#define LCD_CS_PIN 14
#define LCD_SCK_PIN 13
#define LCD_MOSI_PIN 12

#define ESP_NOW_CHANNEL 7 // this was chosen randomly, if you experience instability you might have to tune this, also change it in the sender code!

U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, LCD_SCK_PIN, LCD_MOSI_PIN, LCD_CS_PIN);
unsigned char lcdState = 1; // 0 = off, 1 = mixedDash, 2 = fuelInfo, 3 = PDCsensors, 4 = speeds, 5 = testing
#define LCDSTATE_COUNT 6 // number of available states of the lcd (which actually display info, another one for selecting things is automatically added)
unsigned char selectedLine = 0; // for screens that use user selection

TaskHandle_t RenderingTask;
TaskHandle_t UserInputTask;

typedef struct kcan_data {
  bool clutchPressed;
  bool brakePressed;
  unsigned char steeringWheelButtons; // each bit one button (use functions below): 2^0=VolumeUp, 2^1=VolumeDown, 2^2=UpButton, 2^3=DownButton, 2^4=TelephoneButton, 2^5=VoiceButton, 2^6=RotateButton, 2^7=DiskButton
  unsigned char shiftLeverPos; // on a manual car meaning: ?; on an automatic car: 0 "Off" 1 "P" 2 "R" 4 "N" 8 "D"
  unsigned char PDCsensors[8]; // in cm, order: rear-L, rear-L2, rear-R2, rear-R, front-L, front-L2, front-R2, front-R
  signed short engineTemp; // in celcius
  signed short wheelSpeeds[4]; // in km/h (might depend on car settings), order: front-L, front-R, rear-L, rear-R
  unsigned short speed; // in km/h (might depend on car settings)
  unsigned short engineRpm;
  unsigned short range; // in km
  float fuelLevel1; // in liter
  float fuelLevel2; // in liter
  float engineTorque; // in Nm, can be negative!
  float batteryVoltage; // in volts
  float avgConsumption; // in l/100km (dependent on the car settings)
  float avgSpeed; // in km/h (dependent on the car settings)
  float throttlePercentage; // throttle from 0 (foot off paddle) to 1 (flat)
  float steeringPosition; // -1 -> fully (600°) to the left, 0 -> centered, 1 -> fully (600°) to the right
} kcan_data;

kcan_data data;

// executed when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t * incomingData, int len) {
  memcpy(&data, incomingData, sizeof(data));
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
  Serial.println(sizeof(data));

  showStartupLogo(2500);

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
      selectedLine = 0;
      Serial.println("Encoder button pressed");
      
      if(lcdState == 0) {
        // Turn on lcd
        Serial.println("Turning on LCD");
        digitalWrite(LCD_POWER_PIN, HIGH);
        delay(30); // give it a bit of time to turn on again
        u8g2.clear(); // clear display as sometimes there are artifacts when turning back on
      }

      lcdState = (lcdState + 1) % (LCDSTATE_COUNT + 1);

      if(lcdState == 0) {
        if(selectedLine == 1) {
          // Turn off lcd
          Serial.println("Turning off LCD");
          digitalWrite(LCD_POWER_PIN, LOW);
        } else {
          lcdState = 1; // go back to the first page
        }
      }
    }

    // Encoder rotate (no direction only rotate)
    if(digitalRead(EN1_PIN) == 0) {
      pressed = true;
      Serial.println("Rotated");
      selectedLine = (selectedLine + 1) % 2;
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

  while(1) {
    updateDisplay();
    delay(60); // ~16 refreshes/s
  }
}


// displays the current vehicle information
void updateDisplay() {
  switch(lcdState) {
    // lcd off case
    case 0: break;

    // displays engineTemp, enginePower, engineTorque, batteryVoltage, clutchPressed, brakePressed, throttlePercentage, steeringPosition 
    case 1:
      drawMixedDash();
    break;
    
    // displays fuel levels
    case 2:
      drawFuelInfo();
    break;

    // displays PDC sensor distances
    case 3:
      drawPDCsensors();
    break;

    // displays wheel speeds and overall speed
    case 4:
      drawSpeeds();
    break;

    // temporary page for decoding stuff
    case 5:
      drawTestingScreen();
    break;

    case LCDSTATE_COUNT:
      drawSelectorScreen();
    break;
  }
}


// ### Rendering helper methods ###
void getEngineTempStr(char* tempStr) {
  sprintf(tempStr, "%+d C", data.engineTemp);
}

void getEnginePowerStr(char* powStr) {
  float enginePower = ((float)data.engineRpm * data.engineTorque * ((2.0f * PI) / 60.0f)) / 1000.0f;
  sprintf(powStr, "%d kW", (int)round(enginePower));
}

void getEngineTorqueStr(char* torqueStr) {
  sprintf(torqueStr, "%d Nm", (int)round(data.engineTorque));
}

void getBatteryVoltageStr(char* voltStr) {
  sprintf(voltStr, "%.2f V", data.batteryVoltage);
}

void getFuelLevelStr(char* fuelStr, int fuelIndex) {
  switch(fuelIndex) {
    case 1:
      sprintf(fuelStr, "%.1f l", data.fuelLevel1);
    break;

    case 2:
      sprintf(fuelStr, "%4.1f l", data.fuelLevel2);
    break;

    default: sprintf(fuelStr, "N/A");
  }
}

void getFuelPercentageStr(char* fuelStr) {
  sprintf(fuelStr, "%4.1f%%", ((data.fuelLevel1 + data.fuelLevel2) * 100.0f) / (2.0f * 62.0f));
}

void getRangeStr(char* rangeStr) {
  sprintf(rangeStr, "%d km", data.range);
}

void getAvgConsumptionStr(char* consStr) {
  sprintf(consStr, "%.1f l/100km", data.avgConsumption);
}

void getAvgSpeedStr(char* speedStr) {
  sprintf(speedStr, "%.1f km/h", data.avgSpeed);
}

void getPDCstr(char* pdcStr, unsigned char index, bool displayedLeft) {
  sprintf(pdcStr, displayedLeft ? "%d cm": "%3d cm", data.PDCsensors[index]);
}

void getSpeedStr(char* speedStr, unsigned char index, bool displayedLeft) {
  sprintf(speedStr, displayedLeft ? "%d km/h" : "%3d km/h", index < 4 ? data.wheelSpeeds[index] : data.speed);
}


// ### Logic helper methods ###
//  ## Convert steeringWheelButtons into bools
bool volumeUpPressed() {
  return data.steeringWheelButtons % 2;
}

bool volumeDownPressed() {
  return (data.steeringWheelButtons >> 1) % 2;
}

bool upPressed() {
  return (data.steeringWheelButtons >> 2) % 2;
}

bool downPressed() {
  return (data.steeringWheelButtons >> 3) % 2;
}

bool telephonePressed() {
  return (data.steeringWheelButtons >> 4) % 2;
}

bool voicePressed() {
  return (data.steeringWheelButtons >> 5) % 2;
}

bool rotatePressed() {
  return (data.steeringWheelButtons >> 6) % 2;
}

bool diskPressed() {
  return (data.steeringWheelButtons >> 7) % 2;
}


// ### General visual helper methods
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
    u8g2.drawButtonUTF8(32, 50, U8G2_BTN_HCENTER | U8G2_BTN_BW1 | (data.clutchPressed ? U8G2_BTN_INV : 0), 62,  0,  1, "Clutch" );

    // Brake status
    u8g2.drawButtonUTF8(96, 50, U8G2_BTN_HCENTER | U8G2_BTN_BW1 | (data.brakePressed ? U8G2_BTN_INV : 0), 62,  0,  1, "Brake" );

    // Throttle position
    u8g2.drawBox(0, 55, round(data.throttlePercentage * 128.0f), 5);

    // Steering position
    int barWidth = round(data.steeringPosition * 64.0f);
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
  int level1Height = 63 - (int)round(data.fuelLevel1 * (32.0f/62.0f));
  int level2Height = 63 - (int)round(data.fuelLevel2 * (32.0f/62.0f));
  int textHeight = 52; // this puts text above fuel level but may collide with new text on top: level1Height < level2Height ? level1Height - 1 : level2Height - 1;
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
  char outputStr[11];

  u8g2.firstPage();
  do {
    getSpeedStr(outputStr, 0, true); // front-L
    u8g2.drawStr(1, 8, "100 km/h");

    getSpeedStr(outputStr, 1, false); // front-R
    u8g2.drawStr(80, 8, "100 km/h");

    getSpeedStr(outputStr, 2, true); // rear-L
    u8g2.drawStr(1, 63, "100 km/h");

    getSpeedStr(outputStr, 3, false); // rear-R
    u8g2.drawStr(80, 63, "100 km/h");

    getSpeedStr(outputStr, 4, false); // overall
    u8g2.drawStr(41, 34, "100 km/h");
  } while( u8g2.nextPage() );
}

// always the last page
void drawSelectorScreen() {
  u8g2.firstPage();
  do {
    u8g2.setDrawColor(2); // XOR mode

    u8g2.drawBox(0, 10 * selectedLine, u8g2.getDisplayWidth(), 10);

    u8g2.drawStr(1, 8, "Next");
    u8g2.drawStr(1, 18, "LCD off");

    u8g2.setDrawColor(1); // normal mode
  } while( u8g2.nextPage() );
}

// for testing stuff
void drawTestingScreen() {
  char outputStr[21];

  u8g2.firstPage();
  do {
    sprintf(outputStr, "shiftLeverPos: %d", data.shiftLeverPos);
    u8g2.drawStr(1, 8, outputStr);
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
void showStartupLogo(int duration) {
  // 'bmw-2-logo-png-transparent', 128x64px
  const unsigned char bmwLogo [] PROGMEM = {
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