#include "MyMqtt.h"

WiFiClient mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);

void MyMqtt::begin()
{
  mqttClient.setServer(MQTT_SERVER,MQTT_PORT);
  reconnect();
}

// Connects with a Last Will (soiler/gateway/status -> "offline", retained) so
// HA sees availability immediately even on an ungraceful disconnect, then
// publishes "online". Called from begin() and re-tried from loop() if the
// connection drops.
void MyMqtt::reconnect()
{
  if(WiFi.status()!=WL_CONNECTED)
    return;

  myLogger.log(VERBOSE,"Connecting to MQTT broker %s...",MQTT_SERVER);

  if(mqttClient.connect(MQTT_CLIENT_ID,MQTT_STATUS_TOPIC,0,true,"offline"))
  {
    mqttClient.publish(MQTT_STATUS_TOPIC,"online",true);
    myLogger.log(INFO,"Connected to MQTT broker, published online status");
  }
  else
  {
    myLogger.log(WARNING,"MQTT connect failed, rc=%d",mqttClient.state());
  }
}

// Call every loop() iteration. Rate-limited so a down broker doesn't block
// LoRa packet reception while retrying.
void MyMqtt::loop()
{
  if(mqttClient.connected())
  {
    mqttClient.loop();
    return;
  }

  unsigned long now=millis();
  if((now-lastAttempt)<MQTT_RECONNECT_INTERVAL_MS)
    return;

  lastAttempt=now;
  reconnect();
}

bool MyMqtt::publish(const char*topic,const char*payload,bool retained)
{
  if(!mqttClient.connected())
    return false;

  return mqttClient.publish(topic,payload,retained);
}

bool MyMqtt::isConnected()
{
  return mqttClient.connected();
}
