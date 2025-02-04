#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <mcp2515_can.h>

#define CAN_CS_PIN 5

#define ESP_NOW_CHANNEL 7 // this was chosen randomly, if you experience instability you might have to tune this, also change it in the receiver code!

// Replace with the MAC address of your receiver! (see serial output of the receiver)
uint8_t LCDreceiverAddress[] = {0x58, 0xBF, 0x25, 0x9D, 0xF5, 0x70};

typedef struct kcan_data {
  bool clutchPressed;
  bool brakePressed;
  unsigned char steeringWheelButtons; // each bit one button (use functions below): 2^0=VolumeUp, 2^1=VolumeDown, 2^2=UpButton, 2^3=DownButton, 2^4=TelephoneButton, 2^5=VoiceButton, 2^6=RotateButton, 2^7=DiskButton
  short engineTemp; // in celcius
  unsigned short engineRpm;
  float fuelLevel1; // in liter
  float fuelLevel2; // in liter
  float engineTorque; // in Nm, can be negative!
  float batteryVoltage; // in volts
  double throttlePercentage; // throttle from 0 (foot off paddle) to 1 (flat)
  double steeringPosition; // -1 -> fully (600°) to the left, 0 -> centered, 1 -> fully (600°) to the right
} kcan_data;

// initialize data differently from receiver to "dry test" without connection to car
kcan_data data = {true, false, 0, 1, 1, 58.2f, 45.0f, 1.0f, 1.0f, 0.75d, -0.5d};
esp_now_peer_info_t receiverInfo;
esp_now_send_status_t lastSendStatus = (esp_now_send_status_t)0;

mcp2515_can CAN(CAN_CS_PIN);

TaskHandle_t DataTask;
TaskHandle_t SenderTask;

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

  /* ATTENTION: We are interfacing a 100KBPS CAN bus but need to use the 200KBPS variable
     This is needed because the library expects a MCP module with a 16MHz crystal on it
     Check if your crystal (usually shiny, oval) has an 8 or 16 written on it
     if 8 --> use CAN_200KBPS          if 16 --> use CAN_100KBPS
     This also applies to other CAN bus speeds of course, always double the speed if you have an 8MHz crystal */
  while (CAN_OK != CAN.begin(CAN_200KBPS)) {
    Serial.println("CAN bus init failed! Retrying in 250...");
    delay(250);
  }
  Serial.println("CAN bus initialized successfully");

  xTaskCreatePinnedToCore(
                    dataTaskCode,   // Task function.
                    "dataTask",     // name of task.
                    10000,          // Stack size of task
                    NULL,           // parameter of the task
                    1,              // priority of the task
                    &DataTask,      // Task handle to keep track of created task
                    1);             // pin task to core 1

  xTaskCreatePinnedToCore(
                    senderTaskCode, // Task function.
                    "senderTask",   // name of task.
                    10000,          // Stack size of task
                    NULL,           // parameter of the task
                    1,              // priority of the task
                    &SenderTask,    // Task handle to keep track of created task
                    0);             // pin task to core 0
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
          setThrottlePercentage(buf[2], buf[3]);
          setEngineRpm(buf[4], buf[5]);
          break;

        case 0xC8:
          setSteeringPosition(buf[0], buf[1]);
          break;

        case 0x1D0:
          setEngineTemp(buf[0]);
          break;

        case 0x1D6:
          setSteeringWheelButtons(buf[0], buf[1]);
          break;

        case 0x349:
          setFuelLevels(buf[0], buf[1], buf[2], buf[3]);
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
    delay(lastSendStatus != 0 ? 750 : 100); // longer delay between unsuccessful sends to avoid many sends when receiver isn't ready yet
  }
}


// ### CAN conversion methods ordered by ID ###
// from 0x0A8
void setEngineTorque(unsigned char byte1, unsigned char byte2) {
  // from loopbunny.co.uk: "This reports the real-time torque value the engine is currently producing. This value is twos compliment and can also be negative"
  signed short combined = ((unsigned short)byte2 << 8) + (unsigned short)byte1;
  data.engineTorque = (float)combined / 32.0f;
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
void setThrottlePercentage(unsigned char byte2, unsigned char byte3) {
  // value between 255 and 65064 (don't ask me why)
  unsigned short combined = ((unsigned short)byte3 << 8) + (unsigned short)byte2;

  data.throttlePercentage = (double)(combined - 255) / (double)(65064 - 255);
}

// from 0x0AA
void setEngineRpm(unsigned char byte4, unsigned char byte5) {
  data.engineRpm = round((float)(((unsigned short)byte5 << 8) + (unsigned short)byte4) / 4.0f);
}

// from 0x0C8
void setSteeringPosition(unsigned char byte0, unsigned char byte1) {
  // value is in 2s compliment (can be negative), to get the angle in degrees devide by 23 and the max steering angle is 600°
  // negative means to the left and positive to the right --> value between -12800 and +12800 (-600° and 600°)
  signed short combined = ((unsigned short)byte1 << 8) + (unsigned short)byte0;

  data.steeringPosition = (double)combined / 12800.0d;
}

// from 0x1D0
void setEngineTemp(unsigned char byte0) {
  data.engineTemp = (signed short)byte0 - 48;
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

// from 0x349
void setFuelLevels(unsigned char byte0, unsigned char byte1, unsigned char byte2, unsigned char byte3) {
  data.fuelLevel1 = (float)(((unsigned short)byte1 << 8) + (unsigned short)byte0) / 160.0f;
  data.fuelLevel2 = (float)(((unsigned short)byte3 << 8) + (unsigned short)byte2) / 160.0f;
}

// from 0x3B4
void setBatteryVoltage(unsigned char byte0, unsigned char byte1) {
  // (((Byte[1]-240 )*256)+Byte[0])/68 
  data.batteryVoltage = (float)(((unsigned short)(byte1 - (unsigned char)0xF0) << 8) + (unsigned char)byte0) / 68.0f;
}