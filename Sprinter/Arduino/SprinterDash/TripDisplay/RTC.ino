#include "RTC.h"

void RTC::init(int _refreshTicks)
{
    refreshTicks=_refreshTicks;
}

void RTC::setup()
{
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
  }

  // When the RTC was stopped and stays connected to the battery, it has
  // to be restarted by clearing the STOP bit. Let's do this to ensure
  // the RTC is running.
  rtc.start();  
}

bool RTC::isOnline()
{
  return online;
}

uint32_t RTC::getSecondsSinc2000()
{
  //don't update if it's not time to
  if(millis()<nextTickCount)
      return secondsSince2000;
  nextTickCount=millis()+refreshTicks;

  //get current time from clock
  DateTime now = rtc.now();
  secondsSince2000=now.secondstime();

  //Mark sensor as online
  if(secondsSince2000>0)
    online=true;
  else
    online=false;

  return secondsSince2000;
}
