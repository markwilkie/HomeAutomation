#include "Arduino.h" 
#include <Wire.h>  
#include <SPI.h>
#include <LoRa.h>      //https://github.com/sandeepmistry/arduino-LoRa
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Dictionary.h>

//#include "HT_SSD1306Wire.h"   //new(?) library shipping with heltec.  verified to work on v2 BUT requires heltec libary which conflicts
#include "SSD1306.h" //https://github.com/ThingPulse/esp8266-oled-ssd1306
#include "Debug.h"
#include "logger.h"
#include "version.h"
#include "properties.h"
#include "MyWifi.h"

#define HTTPSERVERTIME        0                    // time blocking in server listen for handshaking while in loop  (zero is immediate)

#define BAND    915E6  //you can set band here directly,e.g. 868E6,915E6

//Globals
Preferences preferences;
MyWifi wifi;
unsigned long millisAtEpoch = 0;
char hubAddress[30]="";
int hubSoilSWPort=0;
unsigned long epoch=0;  //Epoch from hub
Dictionary moistureSensorPorts;

//Rachio
const char* api=RACHIO_API_KEY;
Dictionary sensorZones;

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

//Send a GET message to the admin driver to get epoch
void syncEpoch()
{
  if(!hubSoilSWPort)
    return;

  //Sets handshakRequired to true if fails
  DynamicJsonDocument doc = wifi.sendGetMessage("/epoch",hubSoilSWPort);
  epoch = doc["epoch"].as<unsigned long>();
  millisAtEpoch = millis();

  myLogger.log(INFO,"Setting Epoch to %s (%ld)",getTimeString(epoch),epoch);
}

//Update soil moisture % with rachio
void putSoilMoisture(const char*id,const char*token,double percentage) 
{
  //Manually create payload so that racio likes it
  //{"id":"eb2067a4-503d-4c96-b3c5-7f1d36836fef","percent":1.0}

  //format percentage
  char percStr[30];
  int percInt=percentage*100.0;
  if(percentage<1)
    sprintf(percStr,"0.%d",percInt);
  else
    sprintf(percStr,"1.0");

  //Create payload
  char buf[500];
  sprintf(buf,"{\"id\":\"%s\",\"percent\":%s}",id,percStr);

  //send admin data back
  if(wifi.sendPutMessage("https://api.rach.io/1/public/zone/setMoisturePercent",token,buf))
    myLogger.log(VERBOSE,"Updated Rachio with soil moisture data...");
}

//refresh soil gateway data
void postSoilGW() 
{
  if(!hubSoilSWPort)
    return;

  //admin
  DynamicJsonDocument doc(512);

  if(hubSoilSWPort>0)
    doc["hubSoilSWPort"] = hubSoilSWPort;

  doc["current_time"] = currentTime(); //send back the last epoch sent in + elapsed time since

  //send admin data back
  if(!wifi.sendPostMessage("/soilgw",doc,hubSoilSWPort))
    hubSoilSWPort=0;

  myLogger.log(VERBOSE,"Posted soil gateway data...");
}

//refresh soil sensor data
void postSoil(int id,int moistureReading,int voltage,int rssi) 
{
  //Make sure gateway is online
  if(!hubSoilSWPort)
    return;

  //Get port for the specific sensor
  int sensorPort=moistureSensorPorts.search(String(id)).toInt();
  if(!sensorPort)
  {
    bool retval=registerSensor(id);
    if(!retval)
      return;
    sensorPort=moistureSensorPorts.search(String(id)).toInt();
  }

  //soil moisture sensor
  DynamicJsonDocument doc(512);
  doc["network_id"] = id;
  doc["soil_moisture"] = moistureReading;
  doc["vcc_voltage"] = (float)voltage/10.0;
  doc["wifi_strength"] = rssi;
  doc["firmware_version"] = SKETCH_VERSION;
  doc["heap_frag"] = round2((1.0-((double)ESP.getMinFreeHeap()/(double)ESP.getFreeHeap()))*100);
  doc["current_time"] = currentTime(); //send back the last epoch sent in + elapsed time since

  //send soil data back
  if(!wifi.sendPostMessage("/soil",doc,sensorPort))
  {
    //error, so let's take this sensor out
    myLogger.log(WARNING,"Error posting to ensor id %d, so removing it from our list",id);
    sensorPort=0;
    moistureSensorPorts.remove(String(id));
    return;
  }

  myLogger.log(VERBOSE,"Posted soil sensor data...");
}

bool registerSensor(int id)
{
  char buf[100];
  sprintf(buf,"/registerDevice?id=%d",id);

  //registers (or re-registers) sensor and gets current port
  DynamicJsonDocument doc = wifi.sendGetMessage(buf,hubSoilSWPort);
  int port = doc["port"].as<int>();
  moistureSensorPorts.insert(String(id),String(port));  //adding to the dictionary

  //set if unable to get valid port
  if(port<=0)
    myLogger.log(WARNING,"Unable to get sensor port");
  else
    myLogger.log(INFO,"Setting Port to %d for device id %d  (%s)",port,id,moistureSensorPorts.search(String(id)));

  return port>0;
}

//The hub driver will POST ip, port, and epoch when handshaking
void syncWithHub() 
{
  DynamicJsonDocument doc=wifi.readContent();

  //if (wifi.isPost())   // !! isPost stack dumps on the heltec esp32 proc
  if(1)
  {
      String ip = doc["hubAddress"].as<String>();
      ip.toCharArray(hubAddress, ip.length()+1);
      epoch = doc["epoch"].as<unsigned long>();
      millisAtEpoch=millis();

      String deviceID = doc["network_id"];
      String deviceName = doc["deviceName"];
      if(deviceName.equals("soil"))
        hubSoilSWPort = doc["hubPort"].as<int>();

      myLogger.log(VERBOSE,"Device info from hub - Device Name/ID: %s/%s, IP: %s, Port: %d, Epoch: %ld  (%s)",deviceName,doc["network_id"].as<String>().c_str(),ip.c_str(),doc["hubPort"].as<int>(),epoch,getTimeString(epoch));
    
      // Create the response
      // To get the status of the result you can get the http status so
      // this part can be unusefully
      DynamicJsonDocument doc(50);
      doc["status"] = "OK";   
      wifi.sendResponse(doc);

      //Let's see if we still need a handshake 
      if(hubSoilSWPort>0)
      {
        //Save address and ports to flash in case we reboot
        putHubInfoIntoPreference();
      }
  }

  myLogger.sendLogs(wifi.isConnected());
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

    // Setup mapping of Rachio zones to sensors
    //
    // Start by looking up sensor id, then see if there are linked zones by looking up using current zone guid  (e.g. walkway is linked to brick)
    //

    // Zones w/ no sensors and are not linked
    //   Underdeck="8afb92cc-ff42-4755-bb4d-486ef39bd59f" or zone 2
    //
    DynamicJsonDocument doc = wifi.readJsonFile(CONFIG_URL);
    JsonArray array = doc.as<JsonArray>();
    for(JsonVariant v : array) 
    {
      sensorZones.insert(v["sensorid"].as<String>(),v["zoneid"].as<String>());
      myLogger.log(INFO,"Mapping sensor %s to zone %s",v["sensorid"].as<String>().c_str(),v["zoneid"].as<String>().c_str());
    } 

    //sensorZones.insert("15","457fe405-7582-4cdc-8672-b28f3bec4bde");  //Candy tuffs zone 1
    //sensorZones.insert("16","aff59ede-1d30-4321-bba6-a92e6c23484f");  //Flowers - by brick zone 5
    //sensorZones.insert("aff59ede-1d30-4321-bba6-a92e6c23484f","2bf30e06-c054-4791-a4ea-37ea81716ad5");  //Flowers - walkway zone 3  (linked to brick zone 5)
    //sensorZones.insert("17","5ad042e5-46e5-4c28-ac01-91107612a77f");  //Flowers - side of house zone 7
    //sensorZones.insert("19","eb2067a4-503d-4c96-b3c5-7f1d36836fef");  //Garden zone 6
    //sensorZones.insert("20","8190e592-eff7-4463-a4b1-453f579edbed");  //Deckpots

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

    //Initial sync
    syncEpoch();
    postSoilGW();
    myLogger.sendLogs(wifi.isConnected());       
}

void initialSetup()
{
    //blink led (1x2000,3x250)
    blinkLED(1,2000,500);
    blinkLED(3,250,250);
     
    //Try and load address and ports from flash
    getHubInfoFromPreference();   

    //start wifi and http server
    wifi.startWifi();
    wifi.startServer();   //No need for server because this is now a LoRa link  
}


void loop()
{
  //Lora packet?
  int packetSize = LoRa.parsePacket();
  if (packetSize) 
      readPacket(packetSize);

  //Check in if the hub is trying to get me
  wifi.listen(HTTPSERVERTIME);

  //Let's ping server around every 650 seconds to let them know we're alive
  if((currentTime()-epoch) > 600)
  {
    syncEpoch();
    postSoilGW();
    myLogger.sendLogs(wifi.isConnected()); 
  }
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

  //post soil info we just got to hub
  postSoil(id,perc,voltage,LoRa.packetRssi());
  myLogger.sendLogs(wifi.isConnected());

  //post soil percentage we just got to Rachio
  updateSoilMoisture(id,perc);
  myLogger.sendLogs(wifi.isConnected());
}

void updateSoilMoisture(int sensorId,int perc)
{
  //First, convert perc to soil moisture  (TODO: add structure w/ min/max so this is setup-able)
  //For now, we'll use 20/60 as min max
  double soilPerc=(perc-20)/(60.0-20.0);  
  if(soilPerc<0)
    soilPerc=0.0;
  if(soilPerc>1) 
    soilPerc=1.0;

  String zoneId=sensorZones.search(String(sensorId));
  myLogger.log(INFO,"Updating Zone %s for Sensor %d with %f",zoneId.c_str(),sensorId,soilPerc); 
  if(zoneId.length()>0)
  {
    //update zone just returned
    putSoilMoisture(zoneId.c_str(),api,soilPerc);

    //Any linked zones?
    zoneId=sensorZones.search(zoneId);
    while(zoneId.length()>0)
    {
      myLogger.log(INFO,"Linked Zone %s found",zoneId.c_str());
      putSoilMoisture(zoneId.c_str(),api,soilPerc);
      zoneId=sensorZones.search(zoneId);
    }
  }
}

//put bool named pair into flash
void putHubInfoIntoPreference()
{
  preferences.begin("soil", false);
  preferences.putBytes("hubAddress", hubAddress,30);
  preferences.putInt("hubSoilSWPort", hubSoilSWPort);
  preferences.end();
}

//put bool named pair into flash
void getHubInfoFromPreference()
{
  preferences.begin("soil", true);
  preferences.getBytes("hubAddress", hubAddress, 30);
  hubSoilSWPort=preferences.getInt("hubSoilSWPort",0); 
  preferences.end();
}

// Print the UTC time from time_t, using C library functions.
char *getTimeString(time_t now) 
{
  static char timeString[13];
  
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  int year = timeinfo.tm_year + 1900; // tm_year starts in year 1900 (!)
  int month = timeinfo.tm_mon + 1; // tm_mon starts at 0 (!)
  int day = timeinfo.tm_mday; // tm_mday starts at 1 though (!)
  int hour = timeinfo.tm_hour;
  int mins = timeinfo.tm_min;
  int sec = timeinfo.tm_sec;
  int day_of_week = timeinfo.tm_wday; // tm_wday starts with Sunday=0

  sprintf(timeString,"%02d:%02d %02d/%02d ",hour, mins, month, day);
  return timeString;
}

// rounds a number to 2 decimal places
// example: round(3.14159) -> 3.14
double round2(double value) 
{
   return (int)(value * 100 + 0.5) / 100.0;
}

unsigned long currentTime()
{
  return epoch+((millis()-millisAtEpoch)/1000);
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
