#ifndef RTC_h
#define RTC_h

#include "RTClib.h"

//  RTC_PCF8523
//
// https://github.com/adafruit/RTClib/blob/master/src/RTClib.h   

class RTC 
{
  public:
    void init(int _refreshTicks);
    bool isOnline();
    void setup();
    uint32_t getSecondsSinc2000();
    
  private: 
    RTC_PCF8523 rtc;;
    int refreshTicks;

    //Data
    uint32_t secondsSince2000;
    bool online=false;

    //Timing
    unsigned long nextTickCount;      //when to update/refresh the gauge again
};

#endif