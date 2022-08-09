#ifndef weatherstation_h
#define weatherstation_h

#include <ESPmDNS.h>

//defines
#define TIMEDEEPSLEEP         10      // amount in seconds the ESP32 sleeps.  Is also the time most sensors use to get their samples

//globals in ULP which survive deep sleep
extern RTC_DATA_ATTR int bootCount;
extern RTC_DATA_ATTR long millisSinceEpoch;

extern long epoch;  //Epoch from hub

#endif
