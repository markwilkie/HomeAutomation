//unit number when there are multiple temp sensors
//
#define UNITNUM 7   //This one will need to change for each physical Arduino
#define UNITDESC "MediaCabFan"


//
// Pin Assignments
//
#define LED_PIN 4    // LED pin (turns on when reading/transmitting)
#define THERMISTORPIN A1  // which analog pin for reading thermistor
//#define REEDPIN 6  // which pin powers the reed switch 
//#define POWERPIN 5  //pin which powers the screw contacts (w/ 4.x, it turns mosfet on/off)
//#define INTERRUPTPIN 3 //Interrupt pin  (interrupt 0 is pin 2, interrupt 1 is pin 3)
//#define INTERRUPTNUM 1 //Interrupt number  (make sure matches interrupt pin)

//
// Extension setup
//
#include <PID_v1.h>

#define SETUPFUNC setupFanPID;  //called once when in setup if defined
#define WORKFUNC doFanPID; //called every sleep loop if defined
#define WORKINNERLOOP; //if definied, work is called after every sleep or delay (8s is default)  Otherwise, only called in main loop

#define FANPIN 6
#define SETPOINT 85.00  //Target temperature
#define CRITICAL 150.00  //Critical temperature to ignore PID and turn on fans
#define FAN_CUTOFF 50 //don't bother if below this number because it's too slow

double PIDSetpoint, PIDInput, PIDOutput;                                     //I/O for PID
double aggKp=40, aggKi=2, aggKd=10;                                         //original: aggKp=4, aggKi=0.2, aggKd=1, Aggressive Turning,50,20,20
double consKp=20, consKi=1, consKd=5;                                       //original consKp=1, consKi=0.05, consKd=0.25, Conservative Turning,20,10,
PID myPID(&PIDInput, &PIDOutput, &PIDSetpoint, consKp, consKi, consKd, REVERSE);  //Initialize PID;

//
// Config
//
#define MAXRRETRIES 10  //Number of times to retry before giving up
#define RETRYDELAY 100  //Milliseconds to wait between retries
#define DELAYTIME 1000  //If definied, loop will delay for these ms and not sleep - useful for powered situations when doing extra work
#define SLEEPCYCLES 900 //Sleep cycles wanted  (900 is 15 min assuming a 1 sec timer)
#define TRIGGERTYPE 0   //0- Interrupt is off, 1 - will trigger for both initial trigger of interrupt, AND release.  2 - only on initial interrupt
#define SENDFREQ 1      //0-Send on interrupt, but then not again until timeout - even if interrupted  1-Send every interrup
#define EVENTTYPE 0     //O-opening, 1-motion, 2-alarm
#define ALARMTYPE -1    //0-water, 1-fire
#define TRIGGERLEN 50   //ms for how long the interrupt has to be to send (NEEDS TO BE AT LEAST 50 FOR STABILITY)

//
// Thermistor setup
//
#define THERMISTORNOMINAL 10000    // resistance at 25 degrees C
#define TEMPERATURENOMINAL 25   // temp. for nominal resistance (almost always 25 C)
#define NUMSAMPLES 10  // how many samples to take and average, more takes longer but is more 'smooth'
#define BCOEFFICIENT 3610 //For part number NTCS0603E3103HMT  - he beta coefficient of the thermistor (usually 3000-4000)
#define SERIESRESISTOR 10000  // the value of the 'other' resistor   
