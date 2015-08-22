//unit number when there are multiple temp sensors
//
// 3-Motion dectector (X10)
#define UNITNUM 3   //This one will need to change for each physical Arduino
#define UNITDESC "Hallway"

//
// Pin Assignments
//
//#define LED_PIN 13    // LED pin (turns on when reading/transmitting)
#define THERMISTORPIN A3  // which analog pin for reading thermistor
#define REEDPIN 4  // in this case it's the thermistor 
#define INTERRUPTPIN 2 //Interrupt pin  (interrupt 0 is pin 2, interrupt 1 is pin 3)
#define INTERRUPTNUM 0 //Interrupt number  (make sure matches interrupt pin)

//
// Config
//
#define MAXRRETRIES 10  //Number of times to retry before giving up
#define RETRYDELAY 100  //Milliseconds to wait between retries
#define SLEEPCYCLES 75  //Sleep cycles wanted  (75 is 10 min assuming 8s timer)
#define TRIGGERTYPE 2   //0- Interrupt is off, 1 - will trigger for both initial trigger of interrupt, AND release.  2 - only on initial interrupt
#define SENDFREQ 0      //0-Send on interrupt, but then not again until timeout - even if interrupted  1-Send every interrup
#define EVENTTYPE 1     //O-opening, 1-motion, 2-alarm
#define ALARMTYPE -1    //0-water, 1-fire
#define TRIGGERLEN 170  //ms for how long the interrupt has to be to send (NEEDS TO BE AT LEAST 50 FOR STABILITY)

//
// Termistor setup
//
#define THERMISTORNOMINAL 10000    // resistance at 25 degrees C
#define TEMPERATURENOMINAL 25   // temp. for nominal resistance (almost always 25 C)
#define NUMSAMPLES 10  // how many samples to take and average, more takes longer but is more 'smooth'
#define BCOEFFICIENT 4050 //For part number NTCS0603E3103HMT  - he beta coefficient of the thermistor (usually 3000-4000)
#define SERIESRESISTOR 9994  // the value of the 'other' resistor  
