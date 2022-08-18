#ifndef weatherstation_h
#define weatherstation_h

#include "ULP.h"

#include <ESPmDNS.h>

//defines
#define TIMEDEEPSLEEP         150     // amount in seconds the ESP32 will deep sleep at a time.  
#define WIFITIME              600     // interval in seconds wifi will attempt to POST data to the hub

//globals in ULP which survive deep sleep
extern RTC_DATA_ATTR ULP ulp;  //low power processor

extern RTC_DATA_ATTR bool handshakeRequired;  //will put the sketch into server mode  (power hungry)
extern RTC_DATA_ATTR long epoch;  //Epoch from hub

extern RTC_DATA_ATTR char hubAddress[30];
extern RTC_DATA_ATTR int hubPort;

#endif
