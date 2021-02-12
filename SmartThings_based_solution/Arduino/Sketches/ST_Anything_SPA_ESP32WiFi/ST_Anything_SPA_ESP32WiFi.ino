//******************************************************************************************

//
//  Summary:  This Arduino Sketch, along with the ST_Anything library and the revised SmartThings 
//            library, demonstrates the ability of one NodeMCU ESP8266 to 
//            implement a multi input/output custom device for integration into SmartThings.
//            The ST_Anything library takes care of all of the work to schedule device updates
//            as well as all communications with the NodeMCU ESP32's WiFi.
//
//    
//
//******************************************************************************************
//******************************************************************************************
// SmartThings Library for ESP32WiFi
//******************************************************************************************
#include <SmartThingsESP32WiFi.h>

//******************************************************************************************
// ST_Anything Library 
//******************************************************************************************
#include <Constants.h>       //Constants.h is designed to be modified by the end user to adjust behavior of the ST_Anything library
#include <Device.h>          //Generic Device Class, inherited by Sensor and Executor classes
#include <Sensor.h>          //Generic Sensor Class, typically provides data to ST Cloud (e.g. Temperature, Motion, etc...)
#include <Executor.h>        //Generic Executor Class, typically receives data from ST Cloud (e.g. Switch)
#include <InterruptSensor.h> //Generic Interrupt "Sensor" Class, waits for change of state on digital input 
#include <PollingSensor.h>   //Generic Polling "Sensor" Class, polls Arduino pins periodically
#include <Everything.h>      //Master Brain of ST_Anything library that ties everything together and performs ST Shield communications

#include <PS_ORP_Probe.h>  //Uses the AtlasScientific libraries to read their probes (consumer analog version)
#include <PS_Ph_Probe.h>  //Uses the AtlasScientific libraries to read their probes (consumer analog version)
#include <PS_DS18B20_Temperature.h>  //Implements a Polling Sesnor (PS) to measure Temperature via DS18B20 libraries

//******************************************************************************************
//Define which Arduino Pins will be used for each device
//******************************************************************************************

#define PIN_PH_PROBE    34
#define PIN_ORP_PROBE   35
#define PIN_TEMPERATURE 4

//******************************************************************************************
//ESP32 WiFi Information
//******************************************************************************************
String str_ssid     = "WILKIE-LFP";                           //  <---You must edit this line!
String str_password = "4777ne178";                   //  <---You must edit this line!
IPAddress ip(192, 168, 15, 75);       //Device IP Address       //  <---You must edit this line!
IPAddress gateway(192, 168, 15, 1);    //Router gateway          //  <---You must edit this line!
IPAddress subnet(255, 255, 255, 0);   //LAN subnet mask         //  <---You must edit this line!
IPAddress dnsserver(192, 168, 15, 1);  //DNS server              //  <---You must edit this line!
const unsigned int serverPort = 8090; // port to run the http server on

// Smarthings Hub Information
IPAddress hubIp(192, 168, 15, 63);  // smartthings hub ip       //  <---You must edit this line!
const unsigned int hubPort = 39500; // smartthings hub port

// Hubitat Hub Information
//IPAddress hubIp(192, 168, 1, 145);    // hubitat hub ip         //  <---You must edit this line!
//const unsigned int hubPort = 39501;   // hubitat hub port

//******************************************************************************************
//st::Everything::callOnMsgSend() optional callback routine.  This is a sniffer to monitor 
//    data being sent to ST.  This allows a user to act on data changes locally within the 
//    Arduino sktech.
//******************************************************************************************
void callback(const String &msg)
{
//  Serial.print(F("ST_Anything Callback: Sniffed data = "));
//  Serial.println(msg);
  
  //TODO:  Add local logic here to take action when a device's value/state is changed
  
  //Masquerade as the ThingShield to send data to the Arduino, as if from the ST Cloud (uncomment and edit following line)
  //st::receiveSmartString("Put your command here!");  //use same strings that the Device Handler would send
}


  //******************************************************************************************
  //Declare each Device that is attached to the Arduino
  //  Notes: - For each device, there is typically a corresponding "tile" defined in your 
  //           SmartThings Device Hanlder Groovy code, except when using new COMPOSITE Device Handler
  //         - For details on each device's constructor arguments below, please refer to the 
  //           corresponding header (.h) and program (.cpp) files.
  //         - The name assigned to each device (1st argument below) must match the Groovy
  //           Device Handler names.  (Note: "temphumid" below is the exception to this rule
  //           as the DHT sensors produce both "temperature" and "humidity".  Data from that
  //           particular sensor is sent to the ST Hub in two separate updates, one for 
  //           "temperature" and one for "humidity")
  //         - The new Composite Device Handler is comprised of a Parent DH and various Child
  //           DH's.  The names used below MUST not be changed for the Automatic Creation of
  //           child devices to work properly.  Simply increment the number by +1 for each duplicate
  //           device (e.g. contact1, contact2, contact3, etc...)  You can rename the Child Devices
  //           to match your specific use case in the ST Phone Application.
  //******************************************************************************************
  //Polling Sensors
  static st::PS_ORP_Probe orpSensor(F("probe1"), 120, 0, PIN_ORP_PROBE);
  static st::PS_Ph_Probe phSensor(F("probe2"), 120, 0, PIN_PH_PROBE);
  static st::PS_DS18B20_Temperature temperatureSensor(F("temperature1"), 60, 55, PIN_TEMPERATURE, false, 10, 1); 
  
  //Interrupt Sensors 

  //Special sensors/executors (uses portions of both polling and executor classes)
  
  //Executors

//******************************************************************************************
//Arduino Setup() routine
//******************************************************************************************
void setup()
{
  //*****************************************************************************
  //  Configure debug print output from each main class 
  //  -Note: Set these to "false" if using Hardware Serial on pins 0 & 1
  //         to prevent communication conflicts with the ST Shield communications
  //*****************************************************************************
  st::Everything::debug=true;
  st::Executor::debug=true;
  st::Device::debug=true;
  st::PollingSensor::debug=true;
  st::InterruptSensor::debug=true;

  //*****************************************************************************
  //Initialize the "Everything" Class
  //*****************************************************************************

  //Initialize the optional local callback routine (safe to comment out if not desired)
  st::Everything::callOnMsgSend = callback;
  
  //Create the SmartThings ESP8266WiFi Communications Object
    //STATIC IP Assignment - Recommended
    st::Everything::SmartThing = new st::SmartThingsESP32WiFi(str_ssid, str_password, ip, gateway, subnet, dnsserver, serverPort, hubIp, hubPort, st::receiveSmartString);
 
    //DHCP IP Assigment - Must set your router's DHCP server to provice a static IP address for this device's MAC address
    //st::Everything::SmartThing = new st::SmartThingsESP8266WiFi(str_ssid, str_password, serverPort, hubIp, hubPort, st::receiveSmartString);

  //Run the Everything class' init() routine which establishes WiFi communications with SmartThings Hub
  st::Everything::init();
  
  //*****************************************************************************
  //Add each sensor to the "Everything" Class
  //*****************************************************************************
  st::Everything::addSensor(&orpSensor);
  st::Everything::addSensor(&phSensor);
  st::Everything::addSensor(&temperatureSensor);
      
  //*****************************************************************************
  //Add each executor to the "Everything" Class
  //*****************************************************************************
    
  //*****************************************************************************
  //Initialize each of the devices which were added to the Everything Class
  //*****************************************************************************
  st::Everything::initDevices();

  //*****************************************************************************
  //Print calibration instructions
  //*****************************************************************************
  Serial.println(F("Use commands \"PHCAL,7\", \"PHCAL,4\", and \"PHCAL,10\" to calibrate the pH circuit to those respective values"));
  Serial.println(F("Use command \"PHCAL,CLEAR\" to clear the pH calibration"));
  Serial.println(F("Use command \"ORPCAL,xxx\" to calibrate the ORP circuit to the value xxx \n\"ORPCAL,CLEAR\" clears the calibration"));
}

//******************************************************************************************
//Arduino Loop() routine
//******************************************************************************************
void loop()
{
  //*****************************************************************************
  //Execute the Everything run method which takes care of "Everything"
  //*****************************************************************************
  st::Everything::run();

  //See if there's any incoming serial commands
  checkAndCalibrate();
}

//******************************
// Calibration section
//******************************
uint8_t user_bytes_received = 0;
const uint8_t bufferlen = 32;
char user_data[bufferlen];

void checkAndCalibrate()
{
  //anything coming in?
  if (Serial.available() > 0) {
    user_bytes_received = Serial.readBytesUntil(13, user_data, sizeof(user_data));
  }

  //if so, parse and save to EPROM
  if (user_bytes_received) 
  {
    Serial.print("Got Something: ");
    Serial.println(user_data);
    
    parse_cmd(user_data);
    user_bytes_received = 0;
    memset(user_data, 0, sizeof(user_data));
  }
}

void parse_cmd(char* string) {
  strupr(string);
  String cmd = String(string);

  //ORP?
  if(cmd.startsWith("ORPCAL")){
    int index = cmd.indexOf(',');
    if(index != -1){
      String param = cmd.substring(index+1, cmd.length()-1);
      if(param.equals("CLEAR")){
        orpSensor.calClear();
        Serial.println("ORP CALIBRATION CLEARED");
      }
      else{
        int cal_param = param.toInt();
        orpSensor.cal(cal_param);
        Serial.println("ORP CALIBRATED");
      }
    }
  }

  //pH?
  if(cmd.startsWith("PHCAL")){
      Serial.println(string);    
    if(cmd.startsWith("PHCAL,7")) {       
      phSensor.calMid();                                
      Serial.println("PH MID CALIBRATED");
    }
    else if(cmd.startsWith("PHCAL,4")) {            
      phSensor.calLow();                                
      Serial.println("PH LOW CALIBRATED");
    }
    else if(cmd.startsWith("PHCAL,10")) {      
      phSensor.calHigh();                               
      Serial.println("PH HIGH CALIBRATED");
    }
    else if(cmd.startsWith("PHCAL,CLEAR")) { 
      phSensor.calClear();                              
      Serial.println("PH CALIBRATION CLEARED");
    }
  }
}
