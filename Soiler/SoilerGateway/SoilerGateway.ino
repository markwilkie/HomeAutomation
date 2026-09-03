#include "Arduino.h"
#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>      //https://github.com/sandeepmistry/arduino-LoRa
#include <ArduinoJson.h>

//#include "HT_SSD1306Wire.h"   //new(?) library shipping with heltec.  verified to work on v2 BUT requires heltec libary which conflicts
#include "SSD1306.h" //https://github.com/ThingPulse/esp8266-oled-ssd1306
#include "Debug.h"
#include "logger.h"
#include "version.h"
#include "MyWifi.h"
#include "MyMqtt.h"

#define BAND    915E6  //you can set band here directly,e.g. 868E6,915E6

//Globals
MyWifi wifi;
MyMqtt mqtt;

//Display
SSD1306 display(0x3c, 4, 15);
//static SSD1306Wire  display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

//OLED pins to ESP32 GPIOs via this connection:
//OLED_SDA -- GPIO4
//OLED_SCL -- GPIO15
//OLED_RST -- GPIO16
// WIFI_LoRa_32 ports
// GPIO5  -- SX1278's SCK
// GPIO19 -- SX1278's MISO
// GPIO27 -- SX1278's MOSI
// GPIO18 -- SX1278's CS
// GPIO14 -- SX1278's RESET
// GPIO26 -- SX1278's IRQ(Interrupt Request)

#define SS      18
#define RST     14
#define DI0     26
#define BAND    915E6

//Publish one soil sensor's reading to HA over MQTT (see
//HOME_ASSISTANT_MIGRATION_PLAN.md). Zone mapping, calibration, and Rachio
//updates all now live in Home Assistant automations, not firmware.
void publishSoilState(int id,int moistureReading,int voltage,int rssi)
{
  char topic[40];
  sprintf(topic,"soiler/%d/state",id);

  DynamicJsonDocument doc(512);
  doc["soil_moisture"] = moistureReading;
  doc["vcc_voltage"] = (float)voltage/10.0;
  doc["rssi"] = rssi;
  doc["firmware_version"] = SKETCH_VERSION;
  doc["heap_frag"] = round2((1.0-((double)ESP.getMinFreeHeap()/(double)ESP.getFreeHeap()))*100);

  char payload[512];
  serializeJson(doc,payload);

  if(mqtt.publish(topic,payload))
    myLogger.log(VERBOSE,"Published soil state for sensor %d",id);
  else
    myLogger.log(WARNING,"Failed to publish soil state for sensor %d (MQTT not connected?)",id);
}

void setup()
{
    #if defined(ERRORDEF) || defined(INFODEF) || defined(VERBOSEDEF)
    Serial.begin(115200);
    #endif

    //setup LED pin
    pinMode(LED_BUILTIN, OUTPUT);

    myLogger.log(INFO,"-------------------------- Booting");

    //Important setup stuff
    initialSetup();

    //Setup display
    pinMode(16,OUTPUT);
    digitalWrite(16, LOW);    // set GPIO16 low to reset OLED
    delay(50);
    digitalWrite(16, HIGH);
    display.init();
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);

    //Setuip LoRa
    Serial.println("LoRa Receiver");
    display.drawString(5,5,"LoRa Receiver");
    display.display();
    SPI.begin(5,19,27,18);
    LoRa.setPins(SS,RST,DI0);

    if (!LoRa.begin(BAND)) {
      Serial.println("Starting LoRa Receiver Failed!");
      display.drawString(5,25,"Starting LoRa failed!");
      while (1);
    }
    Serial.println("LoRa Initial OK!");
    display.drawString(5,25,"LoRa Initializing OK!");
    display.display();

    myLogger.sendLogs(wifi.isConnected());
}

void initialSetup()
{
    //blink led (1x2000,3x250)
    blinkLED(1,2000,500);
    blinkLED(3,250,250);

    //start wifi and MQTT
    wifi.startWifi();
    mqtt.begin();
}


void loop()
{
  //Lora packet?
  int packetSize = LoRa.parsePacket();
  if (packetSize)
      readPacket(packetSize);

  //Service OTA updates
  wifi.serviceOTA();

  //Keep the MQTT/HA connection alive (rate-limited reconnect if dropped)
  mqtt.loop();
}

void readPacket(int packetSize)
{
  // received a packets
  Serial.println("Received packet");
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(3, 0, "Received packet ");
  display.display();

  //make sure we're at the start of the transmission
  while(LoRa.available() && LoRa.read() != 01) {}
  int id=LoRa.read();
  int perc=LoRa.read();
  int voltage=LoRa.read();
  int crc=LoRa.read();
  while(LoRa.available() && LoRa.read() != 04) {}   //make sure we go to the EOT byte

  //Now let's clean up in case it's another message
  while(LoRa.available()) {LoRa.read();}

  // print RSSI of packet
  display.drawString(20,22, (String)perc);
  display.drawString(20, 45, "RSSI:  ");
  display.drawString(70, 45, (String)LoRa.packetRssi());
  display.display();

  //poor man's crc
  if(crc!=(id|perc|voltage))
  {
    myLogger.log(INFO,"CRC's don't match  (id: %d perc: %d)",id,perc);
    myLogger.sendLogs(wifi.isConnected());
    return;
  }

  //is this valid data?
  if(id<10 || id>128 || perc<0 || perc>100 || voltage<0 || voltage>100)
  {
    myLogger.log(INFO,"Did not recognize packet  (id: %d, perc: %d, volt: %d, crc: %d)",id,perc,voltage,crc);
    myLogger.sendLogs(wifi.isConnected());
    return;
  }

  //publish soil reading to HA over MQTT
  publishSoilState(id,perc,voltage,LoRa.packetRssi());
  myLogger.sendLogs(wifi.isConnected());
}

// rounds a number to 2 decimal places
// example: round(3.14159) -> 3.14
double round2(double value)
{
   return (int)(value * 100 + 0.5) / 100.0;
}

void blinkLED(int times,int onDuration,int offDuration)
{
  for(int i=0;i<times;i++)
  {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(onDuration);
    digitalWrite(LED_BUILTIN, LOW);
    delay(offDuration);

  }
}

uint16_t checksumCalculator(uint8_t * data, uint16_t length)
{
   uint16_t curr_crc = 0x0000;
   uint8_t sum1 = (uint8_t) curr_crc;
   uint8_t sum2 = (uint8_t) (curr_crc >> 8);
   int index;
   for(index = 0; index < length; index = index+1)
   {
      sum1 = (sum1 + data[index]) % 255;
      sum2 = (sum2 + sum1) % 255;
   }
   return (sum2 << 8) | sum1;
}
