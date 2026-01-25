#pragma once

/*
 * PowerLogger - Tracks power consumption for sparkline visualization
 * 
 * This class collects amp-hour data over time and feeds it to sparklines
 * for visual display of power consumption patterns.
 * 
 * TWO SPARKLINES:
 * 
 * 1. NIGHT SPARKLINE (nightSparkAh)
 *    - Tracks power draw from 10pm to 7am (typical sleep hours)
 *    - 54 data points at 10-minute intervals (9 hours)
 *    - Shows overnight battery drain pattern
 *    - RESETS at 10pm each night for fresh data
 *    - Display: Shows Ah consumed overnight next to moon icon
 * 
 * 2. DAY SPARKLINE (daySparkAh)
 *    - Tracks power flow over rolling 24-hour window
 *    - 72 data points at 20-minute intervals
 *    - Shows daily charging/consumption pattern
 *    - Does NOT reset - continuous rolling window
 *    - Display: Shows Ah consumed today next to calendar icon
 * 
 * DATA COLLECTION:
 * - add() called every PWR_UPD_TIME (1 second) with current amp value
 * - Values accumulated and averaged over the interval period
 * - Average added to sparkline at each interval
 * 
 * NEGATIVE VALUES:
 * - Positive amps = charging (solar/alternator input)
 * - Negative amps = discharging (load consumption)
 * - SparkLine class handles negative values via offset mechanism
 */

#include <ESP32Time.h>

// ============================================================================
// TIMING CONSTANTS - Adjust these to change sparkline behavior
// ============================================================================
#define PWR_UPD_TIME    1000      // Sample current every 1 second, average over interval

// Night sparkline: 9 hours (10pm-7am) at 10-minute intervals = 54 points
#define NIGHT_AH_DUR 	32400     // 9 hours in seconds
#define NIGHT_AH_INT	600.0     // 10 minutes in seconds

// Day sparkline: 24 hours rolling window at 20-minute intervals = 72 points  
#define DAY_AH_DUR		86400     // 24 hours in seconds
#define DAY_AH_INT		1200.0    // 20 minutes in seconds

// Night time boundaries (24-hour format)
#define NIGHT_BEG_HR	22        // Night begins at 10pm
#define NIGHT_END_HR	7         // Night ends at 7am

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
                    layout->getNightSparkPtr()->reset();  // Reset sparkline data for new night
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