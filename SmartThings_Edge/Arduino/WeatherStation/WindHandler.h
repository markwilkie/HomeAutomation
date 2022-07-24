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

typedef struct gustStruct
{
  int gust;
  int gustDirection;
  long gustTime;
} GustLast12Struct;

static int windSamples[WIND_PERIOD_SIZE];                   //using bytes to save space as the count won't be more than 255 in one second
static GustLast12Struct gustLast12[GUST_LAST12_SIZE];        //max gusts (one for each period) for the last 12 hours

int windSeconds = 0;                //timer gets called every second, but we only want to read the pulse count and clear every SAMPLE_SIZE  (e.g. 3 seconds)
int windSampleIdx = 0;              //index for where in the sample array we are
long windSampleTotal = 0;            //running sum of pules (that is, samples)
int gustMax = 0;                    //current max gust (one sample)
unsigned long gustTime = 0;                  //time of max gust in epoch
int gustLast12Idx = 0;              //index for where in the last 12 gust array we are

//debug use
long lastWindPulseCount = 0;
bool pulseDetected=false;

//Setup pulse counter 0
#define PCNT_UNIT_Used      PCNT_UNIT_0                  /* Select the Pulse Count 0  as the unit..*/
#define PCNT_H_LIM_VAL      10000                        /* Set the max limit to trigger the interrupt*/
#define PCNT_INPUT_SIG_IO   14                            /* Pulse Input selected as GPIO 14 */
#define PCNT_FILTER         (2/portTICK_PERIOD_MS)       /* ms delay to filter out noise */

//Setup wind vane
#define WIND_VANE_PIN       36                            /* wind vane pin */
int windDirection = 0;

#endif
