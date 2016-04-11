//
// Several things you should do:
//
// 1) Make sure UNITNUM is unique
// 2) Make sure the UNITDESC is accurate.  This is how the board will be reported
// 3) If using interrupts a) choose pin and number (pin assignments) and then b) set triggertype in confg
//


//unit number when there are multiple temp sensors
//
#define UNITNUM 8   //This one will need to change for each physical Arduino
#define UNITDESC "Garage"

//
// Pin Assignments
//
#define LED_PIN 4    // LED pin (turns on when reading/transmitting)
#define THERMISTORPIN A1  // which analog pin for reading thermistor
#define REEDPIN 6  // which pin powers the reed switch 

//
// General Config
//
#define MAXRRETRIES 10  //Number of times to retry RF before giving up
#define RETRYDELAY 100  //Milliseconds to wait between retries
#define SLEEPCYCLES 75  //Sleep cycles wanted  (900 is 15 min assuming a 1 sec timer)
//#define DELAYTIME 1000  //If defined, loop will delay for these ms and NOT SLEEP - useful for powered situations when doing extra work

//
// Interrupt config
//
#define INTERRUPTPIN 2 //Interrupt pin  (interrupt 0 is pin 2, interrupt 1 is pin 3)
#define INTERRUPTNUM 0 //Interrupt number - 0 is reversed, and 1 is straight  (make sure matches interrupt pin)
#define TRIGGERTYPE 1   //0- Interrupt is off, 1 - will trigger for both initial trigger of interrupt, AND release.  2 - only on initial interrupt
#define SENDFREQ 1      //0-Send on interrupt, but then not again until timeout - even if interrupted  1-Send every interrup
#define EVENTTYPE 0     //O-opening, 1-motion, 2-alarm, 3-counter
#define ALARMTYPE -1    //0-water, 1-fire
#define TRIGGERLEN 1000   //ms for how long the interrupt has to be to send (NEEDS TO BE AT LEAST 50 FOR STABILITY)
#define SEND_STATE_ON_EVENT 1 //1 if state should be send on every event (0 if not)

//
// Peripheral power setup
//
#define POWERPIN 5  //pin which powers the screw contacts (w/ 4.x, it turns power booster on/off)
#define POWER_WAKEUP 50 //how long it takes for the peripheral device to wake up -- IF not defined, power is just on

//
// ADC setup
//
#define ADC_PIN A0  // will read ADC pin and send it as part of state
#define ADCREAD 200     //Number of times to sample ADC pin  (ADC_PIN)

//
// ADC setup w/ thresholds
//
#define ADC_THRESHOLD 505  //Meaningful threshold
#define ADC_PRESENT 0      //0 means that below threshold means present

//
// Thermistor setup
//
#define THERMISTORNOMINAL 10000    // resistance at 25 degrees C
#define TEMPERATURENOMINAL 25   // temp. for nominal resistance (almost always 25 C)
#define NUMSAMPLES 10  // how many samples to take and average, more takes longer but is more 'smooth'
#define BCOEFFICIENT 3610 //For part number NTCS0603E3103HMT  - he beta coefficient of the thermistor (usually 3000-4000)
#define SERIESRESISTOR 10000  // the value of the 'other' resistor   
