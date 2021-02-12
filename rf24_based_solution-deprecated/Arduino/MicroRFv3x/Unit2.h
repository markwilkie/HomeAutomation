//unit number when there are multiple temp sensors
//
// 2-Garage
#define UNITNUM 2   //This one will need to change for each physical Arduino
#define UNITDESC "Weather"

//
// Extension setup
//
#include "Weather.h"
#define SETUPFUNC setupWeather  //called once when in setup if defined
#define WORKFUNC pollWeather 
#define WEATHER_INSTALLED
#define ANEMOMETER_POLL_TIME 30000 //time we'll "hang out" and poll the anemometer
#define ALTITUDE 141 //in meters

//
// Pin Assignments
//
#define LED_PIN 4     // LED pin (turns on when reading/transmitting)
//#define THERMISTORPIN A1  // which analog pin for reading thermistor
#define REEDPIN 3  // which pin powers the reed switch 
#define INTERRUPTPIN 3 //Interrupt pin  (interrupt 0 is pin 2, interrupt 1 is pin 3)
#define INTERRUPTPIN2 2 //ONLY COUNTS for weather (only increments a counter for that interrupt)

//
// ADC setup
//
#define ADC_PIN A6  // will read ADC pin and send it as part of state
#define ADCREAD 25     //Number of times to sample ADC pin  (ADC_PIN)

//
// Config
//
#define MAXRRETRIES 10   //Number of times to retry before giving up
#define RETRYDELAY 100   //Milliseconds to wait between retries
#define SLEEPCYCLES 75   //Sleep cycles wanted  (75 is 10 min assuming 8s timer)
#define TRIGGERTYPE 3    //0- Interrupt is off, 1 - will trigger for both initial trigger of interrupt, AND release.  2 - only on initial interrupt, 3 - only counts (does not send)
#define SENDFREQ 1       //0-Send on interrupt, but then not again until timeout - even if interrupted  1-Send every interrup

#define ALARMTYPE -1    //0-water, 1-fire
#define EVENTTYPE 3     //O-opening, 1-motion, 2-alarm, 3-counter
#define EVENTTYPE2 3    //O-opening, 1-motion, 2-alarm, 3-counter
#define TRIGGERLEN 50   //ms for how long the interrupt has to be to send (NEEDS TO BE AT LEAST 50 FOR STABILITY)
#define SEND_STATE_ON_EVENT 1 //1 if state (and weather) should be send on every event (0 if not)

//
// Termistor setup
//
//#define THERMISTORNOMINAL 10000    // resistance at 25 degrees C
//#define TEMPERATURENOMINAL 25   // temp. for nominal resistance (almost always 25 C)
//#define NUMSAMPLES 10  // how many samples to take and average, more takes longer but is more 'smooth'
//#define BCOEFFICIENT 3610 //For part number NTCS0603E3103HMT  - he beta coefficient of the thermistor (usually 3000-4000)
//#define SERIESRESISTOR 10000  // the value of the 'other' resistor   
