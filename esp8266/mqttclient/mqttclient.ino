//#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
//#include <Adafruit_INA219.h>
 
// Connect to the WiFi
const char* ssid = "";
const char* password = "";
const char* mqtt_server = "";
const char* cmd_mqtt_topic = "myhome/esp8266/1/cmd"; 
const char* status_mqtt_topic = "myhome/esp8266/1/status"; 

//wifi wqtt
WiFiClient espClient;
PubSubClient client(espClient);
char msg[50];

//status and looping
int pollNumber = 0;
int maxPoll = 180;  //3 minutes
 
#define HEATER_SWITCH  5 // Pin to turn on/off switch
#define ONE_WIRE_BUS 13 // Temperature
#define ON_OFF 4 // LOW when on, HIGH when off

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);
char temperatureFString[6];

void setup()
{
 Serial.begin(115200);
 
 client.setServer(mqtt_server, 1883);
 client.setCallback(callback);
 
 pinMode(HEATER_SWITCH, OUTPUT);
 pinMode(ON_OFF, INPUT);

 DS18B20.begin(); 
}
 
void loop()
{
 if (!client.connected()) 
 {
  reconnect();
 }
 client.loop();

 //delay, then send of poll ready
  delay(1000);
  pollNumber--;
  if(pollNumber <= 0)
  {
    pollNumber = maxPoll;

    //See if heater is on (LOW is ON, HIGH is OFF)
    int pinState = digitalRead(ON_OFF);  
    int onOff=0;
    if(pinState == 0)
      onOff=1;

    //Get temp
    DS18B20.requestTemperatures(); 
    float tempF = DS18B20.getTempFByIndex(0);
    dtostrf(tempF, 3, 2, temperatureFString);
    Serial.println(temperatureFString);    
  
    //Send to mqtt
    snprintf (msg, 75, "Heater:%ld,Temperature:%s", onOff,temperatureFString);
    Serial.print("Publish message: ");
    Serial.println(msg);
    client.publish(status_mqtt_topic, msg);    
  }
}
 
void callback(char* topic, byte* payload, unsigned int length) 
{
  Serial.print("Message arrived for topic: ");
  Serial.println(topic);

   for (int i=0;i<length;i++) 
   {
      char receivedChar = (char)payload[i];
      //Serial.print(receivedChar);
      if (receivedChar == 'T')
      {
         Serial.println("Turning ON pin");
         digitalWrite(HEATER_SWITCH, HIGH);
         delay(500);
         Serial.println("Turning OFF pin");
         digitalWrite(HEATER_SWITCH, LOW);

         //Send status
         pollNumber=0;
         delay(500);
      }
    }
}
 
 
void reconnect() 
{
 // Loop until we're reconnected
 while (!client.connected()) {
 Serial.print("Attempting MQTT connection...");
 // Attempt to connect
 if (client.connect("ESP8266 Client")) 
 {
  Serial.println("connected");
  // ... and subscribe to topic
  client.subscribe(cmd_mqtt_topic);
 } else 
 {
  Serial.print("failed, rc=");
  Serial.print(client.state());
  Serial.println(" try again in 5 seconds");
  // Wait 5 seconds before retrying
  delay(5000);
  }
 }
}

 

