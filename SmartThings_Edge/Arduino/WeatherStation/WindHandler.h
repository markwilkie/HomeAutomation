#ifndef wind_h
#define wind_h

#include "debug.h"
#include "driver/pcnt.h"   //pulse counter for esp32

//
//  Setup for pulse counter
//
//  There are two main setup pieces:
//  1) Sample Size: Determines how long pulses are counted.  e.g. anenometer is 3 seconds
//  2) Sample Period: Determines over what length of time we're averaging and doing min/max for.  e.g. 3 second samples over 10 minutes would be 200
//
#define WIND_SAMPLE_SEC     3       //# of seconds we're sampling (counting pulses) for
#define WIND_PERIOD_MIN     10      //sample period in minutes

#define WIND_PERIOD_SIZE    ((60/WIND_SAMPLE_SEC)*WIND_PERIOD_MIN) //# of samples we need to store for the period
#define GUST_LAST12_SIZE    ((60/WIND_PERIOD_MIN)*12)              //# of max gusts (one per period) we need to store for the last 12 hours

static byte windSamples[WIND_PERIOD_SIZE];                   //using bytes to save space as the count won't be more than 255 in one second
static int gustLast12[GUST_LAST12_SIZE];                     //max gusts (one for each period) for the last 12 hours
static long gustLast12Time[GUST_LAST12_SIZE];                //parallel array to store the time (epach) for each gust

int windSeconds = 0;                //timer gets called every second, but we only want to read the pulse count and clear every SAMPLE_SIZE  (e.g. 3 seconds)
int windSampleIdx = 0;              //index for where in the sample array we are
int windSampleTotal = 0;            //running sum of pules (that is, samples)
int gustMax = 0;                    //current max gust (one sample)
long gustTime = 0;                  //time of max gust in epoch
int gustLast12Idx = 0;              //index for where in the last 12 gust array we are

//Setup pulse counter 0
#define PCNT_UNIT_Used      PCNT_UNIT_0                  /* Select the Pulse Count 0  as the unit..*/
#define PCNT_H_LIM_VAL      10000                        /* Set the max limit to trigger the interrupt*/
#define PCNT_INPUT_SIG_IO   4                            /* Pulse Input selected as GPIO 4 */
#define PCNT_FILTER         (2/portTICK_PERIOD_MS)       /* ms delay to filter out noise */

#endif
