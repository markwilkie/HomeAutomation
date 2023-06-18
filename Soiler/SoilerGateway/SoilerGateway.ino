#include "Arduino.h" 
#include <Wire.h>          
#include <Preferences.h>
#include <ArduinoJson.h>
//#include <LoRa.h>

#include "LoRaWan_APP.h" 
#include "HT_SSD1306Wire.h"
#include "Debug.h"
#include "logger.h"
#include "version.h"
#include "Wifi.h"

#define HTTPSERVERTIME        30                    // time blocking in server listen for handshaking while in loop

SSD1306Wire ledDisplay(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED); // addr , freq , i2c group , resolution , rst


#define RF_FREQUENCY                                915000000 // Hz

#define TX_OUTPUT_POWER                             14        // dBm

#define LORA_BANDWIDTH                              0         // [0: 125 kHz,
                                                              //  1: 250 kHz,
                                                              //  2: 500 kHz,
                                                              //  3: Reserved]
#define LORA_SPREADING_FACTOR                       7         // [SF7..SF12]
#define LORA_CODINGRATE                             1         // [1: 4/5,
                                                              //  2: 4/6,
                                                              //  3: 4/7,
                                                              //  4: 4/8]
#define LORA_PREAMBLE_LENGTH                        8         // Same for Tx and Rx
#define LORA_SYMBOL_TIMEOUT                         0         // Symbols
#define LORA_FIX_LENGTH_PAYLOAD_ON                  false
#define LORA_IQ_INVERSION_ON                        false


#define RX_TIMEOUT_VALUE                            1000
#define BUFFER_SIZE                                 30 // Define the payload size here

char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];

static RadioEvents_t RadioEvents;
int16_t txNumber;
int16_t rssi,rxSize;
bool lora_idle = true;

//Globals
Preferences preferences;
Wifi wifi;
unsigned long millisAtEpoch = 0;
bool handshakeRequired = true;
char hubAddress[30]="";
int hubSoilPort=0;
unsigned long epoch=0;  //Epoch from hub

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
  
    // Initialising the UI will init the display too.
    ledDisplay.init();
    ledDisplay.setFont(ArialMT_Plain_16);
    ledDisplay.drawString(0, 10, "Waiting for packet");
    ledDisplay.display();

    //Init lora mcu      
    Mcu.begin();  
    
    txNumber=0;
    rssi=0;
  
    RadioEvents.RxDone = OnRxDone;
    Radio.Init( &RadioEvents );
    Radio.SetChannel( RF_FREQUENCY );
    Radio.SetRxConfig( MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                               LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                               LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                               0, true, 0, 0, LORA_IQ_INVERSION_ON, true );
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
    if(lora_idle)
    {
      lora_idle = false;
      Serial.println("into RX mode");
      Radio.Rx(0);
    }
    Radio.IrqProcess( );
  }
}

void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{
    if(rssi>0)  //there's a bug?? that sometimes gives positive numbers for rssi  
      rssi=0;
    rxSize=size;
    memcpy(rxpacket, payload, size );
    rxpacket[size]='\0';
    Radio.Sleep( );
    lora_idle = true;
    Serial.printf("\r\nreceived packet \"%s\" with rssi %d , length %d\r\n",rxpacket,rssi,rxSize);

    ledDisplay.clear();
    ledDisplay.drawString(0, 10, "Received Packet");
    ledDisplay.drawString(0, 30, rxpacket);
    ledDisplay.display();

    //post soil info
    postSoil(atoi(rxpacket),rssi);
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
