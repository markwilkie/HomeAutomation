#pragma once
#include <ESP32Time.h>

#define PWR_UPD_TIME    1000  //will add to the sum (to average) every second.  This resulting avg will be added to the spark line every INT  

#define NIGHT_AH_DUR 	32400 //9 hours (in seconds)
#define NIGHT_AH_INT	600.0   //every 600 sec, or 10 minutes
#define DAY_AH_DUR		86400 //24 hours (in seconds)
#define DAY_AH_INT		1200.0  //every 20 minutes

#define NIGHT_BEG_HR	22
#define NIGHT_END_HR	7

struct PowerLogData
{
    long startDaySeconds;
    long startNightSeconds;
    double nightAhSum;
    double dayAhSum;
};

class PowerLogger
{
    public:
        PowerLogData data;

        void reset(ESP32Time *rtc)
        {
            data.startDaySeconds=rtc->getEpoch();;
            data.startNightSeconds=rtc->getEpoch();;
            data.nightAhSum=0;
            data.dayAhSum=0; 
        }

        void resetDay(ESP32Time *rtc)
        {
            data.startDaySeconds=rtc->getEpoch();
            data.dayAhSum=0; 
        }

        void resetNight(ESP32Time *rtc)
        {
            data.startNightSeconds=rtc->getEpoch();;
            data.nightAhSum=0; 
        }
};