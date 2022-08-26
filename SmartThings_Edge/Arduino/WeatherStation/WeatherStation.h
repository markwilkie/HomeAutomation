#ifndef weatherstation_h
#define weatherstation_h

#include "ULP.h"

#include <ESPmDNS.h>

//defines
#define WIFITIME              600       // interval in seconds wifi will attempt to POST data to the hub
#define HTTPSERVERTIME        30        // time blocking in server listen for handshaking while in loop
#define TIMEDEEPSLEEP         WIFITIME  // amount in seconds the ESP32 will deep sleep at a time.  
#define SENSORTIME            WIFITIME      //default time most sensors will get a reading
#define AIRTIME               3600          // interval in seconds we'll wake up the power hungry air sensor

//globals in ULP which survive deep sleep
extern RTC_DATA_ATTR ULP ulp;  //low power processor
extern RTC_DATA_ATTR bool handshakeRequired;  //will put the sketch into server mode  (power hungry)
extern RTC_DATA_ATTR long epoch;  //Epoch from hub
extern RTC_DATA_ATTR char hubAddress[30];

#endif
