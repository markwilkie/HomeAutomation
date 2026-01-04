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
    int nightAhSamples;
    double nightHours;
    double dayAhSum;
    int dayAhSamples;
    double dayHours;
};

class PowerLogger
{
    public:
        PowerLogData data;

        void add(float currentAh,ESP32Time *rtc,Layout *layout)
        {
            //Sum of Ah's every PWR_UPD_TIME for DAY_AH_INT  (e.g. every 1 sec for 20 minutes)
            data.dayAhSum+=currentAh;
            data.dayAhSamples++;
            
            //Time to update day spark?  Does so every DAY_AH_INT (e.g. 20 minutes)
            if(rtc->getEpoch()>=(data.startDaySeconds+DAY_AH_INT))
            {
                double intervalAh=data.dayAhSum/data.dayAhSamples;  //get avg value for DAY_AH_INT  (e.g. avg last 20 minutes)
                data.dayHours=layout->getDaySparkPtr()->getElements()/(3600/DAY_AH_INT); //number of hours we have so far
                //logger.log(VERBOSE,"Adding %fAh to day spark line",intervalAh);
                layout->getDaySparkPtr()->add(intervalAh);
                resetDay(rtc);
            }

            //night time?
            if(rtc->getHour(true)>=NIGHT_BEG_HR || rtc->getHour(true)<NIGHT_END_HR)
            {
                //reset spark because it's the beginning of the evening
                if(rtc->getHour(true)==NIGHT_BEG_HR && (rtc->getEpoch()-data.startNightSeconds) > NIGHT_AH_DUR)
                {
                    logger.log(VERBOSE,"Resetting night spark");
                    resetNight(rtc);
                }

                //Keep running total
                data.nightAhSum+=currentAh;
                data.nightAhSamples++;

                //Time to update night spark?
                if(rtc->getEpoch()>=(data.startNightSeconds+NIGHT_AH_INT))
                {
                    double intervalAh=data.nightAhSum/data.nightAhSamples;  //get avg value for NIGHT_AH_INT  (e.g. avg last 10 minutes)
                    data.nightHours=layout->getNightSparkPtr()->getElements()/(3600/NIGHT_AH_INT); //number of hours we have so far
                    //logger.log(VERBOSE,"Adding %fAh to night spark line",intervalAh);
                    layout->getNightSparkPtr()->add(intervalAh);
                    resetNight(rtc);
                }
            }
        }       

        void reset(ESP32Time *rtc)
        {
            resetDay(rtc);
            resetNight(rtc);
        }

        void resetDay(ESP32Time *rtc)
        {
            data.startDaySeconds=rtc->getEpoch();
            data.dayAhSum=0; 
            data.dayAhSamples=0;
        }

        void resetNight(ESP32Time *rtc)
        {
            data.startNightSeconds=rtc->getEpoch();;
            data.nightAhSum=0; 
            data.nightAhSamples=0;
        }
};