#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <mcp2515_can.h>
#include <SD.h>
#include <FS.h>

#define CAN_CS_PIN 5

#define ESP_NOW_CHANNEL 7 // this was chosen randomly, if you experience instability you might have to tune this, also change it in the receiver code!

#define SD_MISO 13
#define SD_SCLK 14
#define SD_CS 15
#define SD_MOSI 27
#define HSPI_FRQ 32000000

// Replace with the MAC address of your receiver! (see serial output of the receiver)
uint8_t LCDreceiverAddress[] = {0x58, 0xBF, 0x25, 0x9D, 0xF5, 0x70};

typedef struct kcan_data {
  bool clutchPressed;
  bool brakePressed;
  unsigned char steeringWheelButtons; // each bit one button (use functions below): 2^0=VolumeUp, 2^1=VolumeDown, 2^2=UpButton, 2^3=DownButton, 2^4=TelephoneButton, 2^5=VoiceButton, 2^6=RotateButton, 2^7=DiskButton
  unsigned char gearAct; // meaning still unclear "gear actual"??
  unsigned char PDCsensors[8]; // in cm, order: rear-L, rear-L2, rear-R2, rear-R, front-L, front-L2, front-R2, front-R
  signed short engineTemp; // in celcius
  signed short oilTemp; // in celcius
  signed short wheelSpeeds[4]; // in km/h (might depend on car settings), order: front-L, front-R, rear-L, rear-R
  unsigned short speed; // in km/h (might depend on car settings)
  unsigned short engineRpm;
  unsigned short range; // in km
  unsigned short airPressEngine; // in hPa
  float fuelLevel1; // in liter
  float fuelLevel2; // in liter
  float engineTorque; // in Nm, can be negative!
  float batteryVoltage; // in volts
  float avgConsumption; // in l/100km (dependent on the car settings)
  float avgSpeed; // in km/h (dependent on the car settings)
  float throttlePercentage; // throttle from 0 (foot off paddle) to 1 (flat)
  float steeringPosition; // -1 -> fully (600°) to the left, 0 -> centered, 1 -> fully (600°) to the right
  float accelerationLong; // in m/s²
  float accelerationCross; // in m/s²
} kcan_data;

kcan_data data;
esp_now_peer_info_t receiverInfo;
esp_now_send_status_t lastSendStatus = (esp_now_send_status_t)0;

mcp2515_can CAN(CAN_CS_PIN);

SPIClass hspi(HSPI);
bool loggingModule = false;

TaskHandle_t DataTask;
TaskHandle_t SenderTask;
TaskHandle_t LoggerTask;

// called when a data package is sent
void OnDataSent(const uint8_t * mac_addr, esp_now_send_status_t status) {
  lastSendStatus = status;
  if(status != 0) {
    Serial.println("Error sending message!");
  }
}


void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESP_NOW_CHANNEL);
  while (!WiFi.STA.started()) {
    Serial.println("\nStarting Wifi...");
    delay(100);
  }

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW! Restarting...");
    ESP.restart();
  }

  // Register callback message
  esp_now_register_send_cb(OnDataSent);
  
  // Register peer
  memcpy(receiverInfo.peer_addr, LCDreceiverAddress, 6);
  receiverInfo.channel = ESP_NOW_CHANNEL;  
  receiverInfo.encrypt = false;
  
  // Add peer        
  if (esp_now_add_peer(&receiverInfo) != ESP_OK){
    Serial.println("Failed to add peer! Restarting...");
    ESP.restart();
  }
  Serial.println("ESP-NOW init successful!");

  Serial.print("Sending ESP-NOW messages at a size of: ");
  Serial.println(sizeof(data));
  Serial.println();


  /* ATTENTION: We are interfacing a 100KBPS CAN bus but need to use the 200KBPS variable
     This is needed because the library expects a MCP module with a 16MHz crystal on it
     Check if your crystal (usually shiny, oval) has an 8 or 16 written on it
     if 8 --> use CAN_200KBPS          if 16 --> use CAN_100KBPS
     This also applies to other CAN bus speeds of course, always double the speed if you have an 8MHz crystal */
  while (CAN_OK != CAN.begin(CAN_200KBPS)) {
    Serial.println("CAN bus init failed! Retrying in 250...");
    delay(250);
  }
  do {
    Serial.println("Setting CAN mode to MODE_LISTENONLY");
    //  if we don't set this mode i assume that the MCP sends an acknowledge message when messages are received and the car does NOT like this AT ALL
    CAN.setMode(MODE_LISTENONLY); // this is very important!!!
  } while(CAN.getMode() != MODE_LISTENONLY);
  Serial.println("CAN bus initialized successfully\n");


  // Init SD card ( or not :) )
  hspi.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS); // use other SPI bus for the SD card to not interrupt the MCP
  hspi.setFrequency(HSPI_FRQ);
  pinMode(SD_CS, OUTPUT);
  if(!SD.begin(SD_CS, hspi, HSPI_FRQ) || SD.cardType() == CARD_NONE) {
    Serial.println("SD card module not connected or no card inserted");
  } else {
    loggingModule = true;
    Serial.println("SD card module initialized successfully");
    Serial.printf("SD storage: %lluMiB / %lluMiB\n", SD.usedBytes() / (1024 * 1024), SD.totalBytes() / (1024 * 1024));
  }
  Serial.println();


  xTaskCreatePinnedToCore(
                    senderTaskCode, // Task function.
                    "senderTask",   // name of task.
                    5000,           // Stack size of task
                    NULL,           // parameter of the task
                    2,              // priority of the task
                    &SenderTask,    // Task handle to keep track of created task
                    0);             // pin task to core 0
  
  if(loggingModule) {
    xTaskCreatePinnedToCore(
                    loggerTaskCode, // Task function.
                    "loggerTask",   // name of task.
                    5000,           // Stack size of task
                    NULL,           // parameter of the task
                    1,              // priority of the task
                    &LoggerTask,    // Task handle to keep track of created task
                    0);             // pin task to core 0
  }

  xTaskCreatePinnedToCore(
                    dataTaskCode,   // Task function.
                    "dataTask",     // name of task.
                    10000,          // Stack size of task
                    NULL,           // parameter of the task
                    2,              // priority of the task
                    &DataTask,      // Task handle to keep track of created task
                    1);             // pin task to core 1                  
}


void loop() {
}


void dataTaskCode(void * params) {
  Serial.print("Data task running on core ");
  Serial.println(xPortGetCoreID());

  unsigned char len = 0;
  unsigned char buf[8];

  while(1) {
    if (CAN_MSGAVAIL == CAN.checkReceive()) { // Message received
      CAN.readMsgBuf(&len, buf);
      unsigned long canId = CAN.getCanId();

      // If interesting ID received, set corresponding values
      switch(canId) {
        case 0xA8:
          setEngineTorque(buf[1], buf[2]);
          setClutchPressed(buf[5]);
          setBrakePressed(buf[7]);
          break;

        case 0xAA:
          setThrottlePercentage(buf[3]);
          setEngineRpm(buf[4], buf[5]);
          break;

        case 0xC8:
          setSteeringPosition(buf[0], buf[1]);
          break;

        case 0xCE:
          setWheelSpeeds(buf, len);
          break;

        case 0x1A0:
          setSpeed(buf[0], buf[1]);
          setAcceleration(buf[2], buf[3], buf[4]);
          break;

        case 0x1C2:
          setPDCsensors(buf, len);
          break;

        case 0x1D0:
          setEngineTemp(buf[0]);
          setOilTemp(buf[1]);
          setAirPressEngine(buf[3]);
          break;

        case 0x1D2:
          setGearAct(buf[1]);
          break;

        case 0x1D6:
          setSteeringWheelButtons(buf[0], buf[1]);
          break;

        case 0x330:
          setRange(buf[6], buf[7]);
          break;

        case 0x349:
          setFuelLevels(buf[0], buf[1], buf[2], buf[3]);
          break;

        case 0x362:
          setAvgConsumption(buf[1], buf[2]);
          setAvgSpeed(buf[0], buf[1]);
          break;

        case 0x3B4:
          setBatteryVoltage(buf[0], buf[1]);
          break;
      }
    }
  }
}


void senderTaskCode(void * params) {
  Serial.print("Sender task running on core ");
  Serial.println(xPortGetCoreID());

  esp_err_t sendErr;

  while(1) {
    esp_now_send(LCDreceiverAddress, (uint8_t *) &data, sizeof(data));
    delay(lastSendStatus != 0 ? 1000 : 60); // longer delay between unsuccessful sends to avoid many unnecessary sends when receiver isn't ready yet
  }
}


void loggerTaskCode(void * params) {
  Serial.print("Logger task running on core ");
  Serial.println(xPortGetCoreID());

  File logFile;
  getLogFile(&logFile);

  if(!logFile.print("time(s);clutchPressed;brakePressed;steeringWheelButtons;gearAct;engineTemp;oilTemp;wheel1;wheel2;wheel3;wheel4;speed;engineRpm;range;airPressEngine;fuelLevel1;fuelLevel2;engineTorque;batteryVoltage;avgCons;avgSpeed;throttlePercent;steeringPos;accelLong;accelCross;\n")) {
    Serial.println("Initial write to file failed. Aborting...");
    vTaskDelete(LoggerTask);
  }

  char printString[1024];
  unsigned char flushCounter = 0;

  Serial.print("Writing log to: ");
  Serial.println(logFile.name());
  while(1) {
    // use hex where possible to save space
    sprintf(printString, "%.2f;%X;%X;%X;%X;%hX;%hX;%hX;%hX;%hX;%hX;%X;%X;%X;%X;%.1f;%.1f;%.1f;%.1f;%.1f;%.1f;%.4f;%.4f;%.4f;%.4f;\n", millis() / 1000.0f, data.clutchPressed, data.brakePressed, data.steeringWheelButtons, data.gearAct,
        data.engineTemp, data.oilTemp, data.wheelSpeeds[0], data.wheelSpeeds[1], data.wheelSpeeds[2], data.wheelSpeeds[3], data.speed, data.engineRpm, data.range, data.airPressEngine, data.fuelLevel1, data.fuelLevel2,
        data.engineTorque, data.batteryVoltage, data.avgConsumption, data.avgSpeed, data.throttlePercentage, data.steeringPosition, data.accelerationLong, data.accelerationCross);
    logFile.print(printString);

    flushCounter = (flushCounter + 1) % 20; // flush after every 20th log to not lose too much data when power is shut off, but also not overdo it
    if(flushCounter == 0) {
      logFile.flush();
    }

    delay(250); // better would be a precise timer but this is fine for now
  }
}


// ### CAN conversion methods ordered by ID ###
// from 0x0A8
void setEngineTorque(unsigned char byte1, unsigned char byte2) {
  // from loopbunny.co.uk: "This reports the real-time torque value the engine is currently producing. This value is twos compliment and can also be negative"
  signed short combined = (signed short)(((unsigned short)byte2 << 8) + (unsigned short)byte1) >> 4; // rightmost 4 bits are a status message
  data.engineTorque = (float)combined / 2.0f;
}

// from 0x0A8
void setClutchPressed(unsigned char byte5) {
  // to check first bit we only need to check if the number is odd or even
  data.clutchPressed = byte5 % 2;
}

// from 0x0A8
void setBrakePressed(unsigned char byte7) {
  data.brakePressed = byte7 > 20;
}

// from 0x0AA
void setThrottlePercentage(unsigned char byte3) {
  data.throttlePercentage = ((float)byte3) / 255.0f;
}

// from 0x0AA
void setEngineRpm(unsigned char byte4, unsigned char byte5) {
  data.engineRpm = round((float)(((unsigned short)byte5 << 8) + (unsigned short)byte4) / 4.0f);
}

// from 0x0C8
void setSteeringPosition(unsigned char byte0, unsigned char byte1) {
  // value is in 2s compliment (can be negative), to get the angle in degrees devide by 23 and the max steering angle is 600°
  // negative means to the left and positive to the right --> value between -13800 and +13800 (-600° and 600°)
  signed short combined = ((unsigned short)byte1 << 8) + (unsigned short)byte0;

  data.steeringPosition = (float)combined / 13800.0f;
}

// from 0x0CE
void setWheelSpeeds(unsigned char* values, unsigned char len) { // len should always be 8, just to be safe
  for(int i = 0; i < len / 2; i++) {
    data.wheelSpeeds[i] = (signed short)(((unsigned short)values[(2 * i) + 1] << 8) + (unsigned short)values[2 * i]) / 16;
  }
}

// from 0x1A0
void setSpeed(unsigned char byte0, unsigned char byte1) {
  data.speed = (((unsigned short)((unsigned char)(byte1 << 4)) << 4) + (unsigned short)byte0) / 10;
}

// from 0x1A0
void setAcceleration(unsigned char byte2, unsigned char byte3, unsigned char byte4) {
  data.accelerationLong = (((signed short)((signed char)(byte3 << 4)) << 4) + (signed short)byte2) / 40.0f;
  data.accelerationCross = (((signed short)byte4 << 4) + ((signed short)byte3 >> 4)) / 40.0f;
}

// from 0x1C2
void setPDCsensors(unsigned char* values, unsigned char len) { // len should always be 8, just to be safe
  for(int i = 0; i < len; i++) {
    data.PDCsensors[i] = values[i];
  }
}

// from 0x1D0
void setEngineTemp(unsigned char byte0) {
  data.engineTemp = (signed short)byte0 - 48;
}

// from 0x1D0
void setOilTemp(unsigned char byte1) {
  data.oilTemp = (signed short)byte1 - 48;
}

// from 0x1D0
void setAirPressEngine(unsigned char byte3) {
  data.airPressEngine = (((unsigned short)byte3) * 2) + 598;
}

// from 0x1D2
void setGearAct(unsigned char byte1) {
  data.gearAct = (unsigned char)(byte1 >> 4); // shift out first 4 bits
}

// from 0x1D6
void setSteeringWheelButtons(unsigned char byte0, unsigned char byte1) {
  // the steering wheel has 8 buttons --> to be space efficient store them in one unsigned char
  unsigned char tempButtons = 0;

  // Volume up at bit 4
  if((byte0 >> 3) % 2) {
    tempButtons += 1;
  }
  // Volume down at bit 3
  if((byte0 >> 2) % 2) {
    tempButtons += 2;
  }
  // Up at bit 6
  if((byte0 >> 5) % 2) {
    tempButtons += 4;
  }
  // Down at bit 5
  if((byte0 >> 4) % 2) {
    tempButtons += 8;
  }
  // Telephone at bit 1
  if(byte0 % 2) {
    tempButtons += 16;
  }
  // Voice at bit 1
  if(byte1 % 2) {
    tempButtons += 32;
  }
  // Rotate at bit 5
  if((byte1 >> 4) % 2) {
    tempButtons += 64;
  }
  // Disk at bit 6
  if((byte1 >> 5) % 2) {
    tempButtons += 128;
  }

  data.steeringWheelButtons = tempButtons;
}

// from 0x330
void setRange(unsigned char byte6, unsigned char byte7) {
  data.range = (((unsigned short)byte7 << 8) + (unsigned short)byte6) / (unsigned short)16;
}

// from 0x349
void setFuelLevels(unsigned char byte0, unsigned char byte1, unsigned char byte2, unsigned char byte3) {
  data.fuelLevel1 = (float)(((unsigned short)byte1 << 8) + (unsigned short)byte0) / 160.0f;
  data.fuelLevel2 = (float)(((unsigned short)byte3 << 8) + (unsigned short)byte2) / 160.0f;
}

// from 0x362
void setAvgConsumption(unsigned char byte1, unsigned char byte2) {
  data.avgConsumption = (float)(((unsigned short)byte2 << 4) + ((unsigned short)byte1 >> 4)) / 10.0f;
}

// from 0x362
void setAvgSpeed(unsigned char byte0, unsigned char byte1) {
  // yes, shifting in 2 steps is intentional here, to get rid of the upper half
  data.avgSpeed = (float)(((unsigned short)((unsigned char)(byte1 << 4)) << 4) + (unsigned short)byte0) / 10.0f;
}

// from 0x3B4
void setBatteryVoltage(unsigned char byte0, unsigned char byte1) {
  // (((Byte[1]-240 )*256)+Byte[0])/68 
  data.batteryVoltage = (float)(((unsigned short)(byte1 - (unsigned char)0xF0) << 8) + (unsigned char)byte0) / 68.0f;
}


// ### Helper functions for logging ###
void getLogFile(File* outputFile) {
  File logRoot = SD.open("/BMW_KCAN_telemetryLog");
  if(!logRoot) {
    Serial.println("Creating log directory");

    if (!SD.mkdir("/BMW_KCAN_telemetryLog")) {
      Serial.println("Could not create log directory. Aborting...");
      vTaskDelete(LoggerTask);
    }

    logRoot = SD.open("/BMW_KCAN_telemetryLog");
    if(!logRoot) {
      Serial.println("Still could not access log directory. Aborting...");
      vTaskDelete(LoggerTask);
    }
  } else if(!logRoot.isDirectory()) {
    Serial.println("Logging directory name taken. Aborting...");
    vTaskDelete(LoggerTask);
  }

  unsigned int fileCounter = 0;
  File logFile = logRoot.openNextFile();
  while (logFile) {
    fileCounter++;
    logFile = logRoot.openNextFile();
  }

  char printString[64];

  sprintf(printString, "/BMW_KCAN_telemetryLog/log_%d.csv", fileCounter);
  logFile = SD.open(printString, FILE_WRITE);
  if(!logFile) {
    Serial.println("Could not open log file for writing. Aborting...");
    vTaskDelete(LoggerTask);
  }

  *outputFile = logFile;
}