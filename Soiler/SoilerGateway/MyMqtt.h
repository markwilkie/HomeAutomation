#include "logger.h"

extern MyLogger myLogger;

#ifndef mymqtt_h
#define mymqtt_h

#include <WiFi.h>
#include <PubSubClient.h>   //https://github.com/knolleary/pubsubclient

// See HOME_ASSISTANT_MIGRATION_PLAN.md - this is what replaced the
// SmartThings hub's HTTP handshake/registration protocol.
#define MQTT_SERVER "192.168.15.24"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "soiler-gateway"
#define MQTT_STATUS_TOPIC "soiler/gateway/status"
#define MQTT_RECONNECT_INTERVAL_MS 5000UL   //don't hammer the broker, and don't block LoRa receive

class MyMqtt
{
  public:
    void begin();
    void loop();
    bool publish(const char*topic,const char*payload,bool retained=false);
    bool isConnected();

  private:
    void reconnect();

    unsigned long lastAttempt=0;
};

#endif
