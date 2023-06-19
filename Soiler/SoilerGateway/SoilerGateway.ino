#include "Arduino.h" 
#include <SPI.h>
#include <LoRa.h>    
#include <Preferences.h>
#include <ArduinoJson.h>

#include "SSD1306.h"    
#include "Debug.h"
#include "logger.h"
#include "version.h"
#include "Wifi.h"

#define HTTPSERVERTIME        30                    // time blocking in server listen for handshaking while in loop

#define BAND    915E6  //you can set band here directly,e.g. 868E6,915E6
String packet;

//Globals
Preferences preferences;
Wifi wifi;
unsigned long millisAtEpoch = 0;
bool handshakeRequired = true;
char hubAddress[30]="";
int hubSoilPort=0;
unsigned long epoch=0;  //Epoch from hub

SSD1306  display(0x3c, 4, 15);

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

//Send a GET message to the admin driver to get epoch, wifi-only flag and so on.
void syncSettings()
{
  if(!hubSoilPort)
    return;

  DynamicJsonDocument doc = wifi.sendGetMessage("/settings",hubSoilPort);
  epoch = doc["epoch"].as<unsigned long>();
  millisAtEpoch = millis();

  logger.log(INFO,"Setting Epoch to %s (%ld)",getTimeString(epoch),epoch);

  if(handshakeRequired)
    logger.log(WARNING,"Handshake mode active");
  else
    logger.log(VERBOSE,"Handshake mode NOT active");
}

//refresh admin data
void postSoil(int moistureReading,int rssi) 
{
  if(!hubSoilPort)
    return;

  //admin
  DynamicJsonDocument doc(512);

  if(hubSoilPort>0)
    doc["hubSoilPort"] = hubSoilPort;

  doc["soil_moisture"] = moistureReading;
  doc["wifi_strength"] = rssi;
  doc["firmware_version"] = SKETCH_VERSION;
  doc["heap_frag"] = round2((1.0-((double)ESP.getMinFreeHeap()/(double)ESP.getFreeHeap()))*100);
  doc["current_time"] = currentTime(); //send back the last epoch sent in + elapsed time since

  //send admin data back
  if(!wifi.sendPostMessage("/soil",doc,hubSoilPort))
    hubSoilPort=0;

  logger.log(VERBOSE,"Posted soil data...");
}

//The hub driver will POST ip, port, and epoch when handshaking
void syncWithHub() 
{
  DynamicJsonDocument doc=wifi.readContent();
  Serial.println("in sync w/ hub");
  serializeJsonPretty(doc, Serial);

  //if (wifi.isPost())   // !! isPost stack dumps on the heltec esp32 proc
  if(1)
  {
      Serial.println("before trying to read json doc");
      String ip = doc["hubAddress"].as<String>();
      Serial.println(ip);
      ip.toCharArray(hubAddress, ip.length()+1);
      epoch = doc["epoch"].as<unsigned long>();
      Serial.println(epoch);
      millisAtEpoch=millis();

      String deviceName = doc["deviceName"];
      Serial.println(deviceName);
      if(deviceName.equals("soil"))
        hubSoilPort = doc["hubPort"].as<int>();

      Serial.println("before logger");
      logger.log(VERBOSE,"Hub info - Device: %s, IP: %s, Port: %d, Epoch: %ld  (%s)",deviceName,ip.c_str(),doc["hubPort"].as<int>(),epoch,getTimeString(epoch));
    
      // Create the response
      // To get the status of the result you can get the http status so
      // this part can be unusefully
      DynamicJsonDocument doc(50);
      doc["status"] = "OK";   
      Serial.println("before send");
      wifi.sendResponse(doc);

      //Let's see if we still need a handshake 
      if(hubSoilPort>0)
      {
        //Save address and ports to flash in case we reboot
        Serial.println("before save to flash");
        putHubInfoIntoPreference();
        handshakeRequired = false;
      }
  }
  Serial.println("done");
}

void setup() 
{
    #if defined(ERRORDEF) || defined(INFODEF) || defined(VERBOSEDEF)
    Serial.begin(115200);
    #endif

    //setup LED pin
    pinMode(LED_BUILTIN, OUTPUT);

    logger.log(INFO,"-------------------------- Booting"); 

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
      display.drawString(5,25,"Starting LoRa failed!");
      while (1);
    }
    Serial.println("LoRa Initial OK!");
    display.drawString(5,25,"LoRa Initializing OK!");
    display.display();    
}

void initialSetup()
{
    //blink led (1x2000,3x250)
    blinkLED(1,2000,500);
    blinkLED(3,250,250);
     
    //Try and load address and ports from flash
    getHubInfoFromPreference();

    //Now let's see if we still need a handshake 
    if(hubSoilPort>0)
    {
      handshakeRequired = false;
    }     

    //start wifi and http server
    wifi.startWifi();
    wifi.startServer();   //No need for server because this is now a LoRa link

}


void loop()
{

  //Check in w/ web server if we're in handshake mode
  if(handshakeRequired)
  {
    Serial.println("handshake mode");
    logger.log(INFO,"In Handshake Mode...  (not listening for LoRa)"); 

    //Blink because we're handshaking (2x500)
    blinkLED(2,500,250);

    //start wifi if not already on
    if(!wifi.isConnected())
      wifi.startWifi();

    //start server and listen on it if not already started
    if(!wifi.isServerOn())
      wifi.startServer();

    //listen for a bit
    wifi.listen(HTTPSERVERTIME*1000);  
  }
  else
  {
    //Serial.print("listening for LoRa ");
    int packetSize = LoRa.parsePacket();
    //Serial.println(packetSize);
    if (packetSize) { cbk(packetSize);  }
    delay(10);
  }
}

void cbk(int packetSize) 
{
    // received a packets
    Serial.print("Received packet. ");
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.drawString(3, 0, "Received packet ");
    display.display();
    
    // read packet
    while (LoRa.available()) 
    {
      packet = LoRa.readString();
      Serial.print(packet);
      display.drawString(20,22, packet);
      display.display();
    }

    // print RSSI of packet
    Serial.print(" with RSSI ");
    Serial.println(LoRa.packetRssi());
    display.drawString(20, 45, "RSSI:  ");
    display.drawString(70, 45, (String)LoRa.packetRssi());
    display.display();

  //post soil info
  postSoil(packet.toInt(),LoRa.packetRssi());
}

//put bool named pair into flash
void putHubInfoIntoPreference()
{
  preferences.begin("soil", false);
  preferences.putBytes("hubAddress", hubAddress,30);
  preferences.putInt("hubSoilPort", hubSoilPort);
  preferences.end();
}

//put bool named pair into flash
void getHubInfoFromPreference()
{
  preferences.begin("soil", true);
  preferences.getBytes("hubAddress", hubAddress, 30);
  hubSoilPort=preferences.getInt("hubSoilPort",0); 
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
