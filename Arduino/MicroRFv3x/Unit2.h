//unit number when there are multiple temp sensors
//
// 0-Outside
// 1-Inside
// 2-Garage
#define UNITNUM 2   //This one will need to change for each physical Arduino

//
// Pin Assignments
//
#define LED_PIN 4     // LED pin (turns on when reading/transmitting)
#define THERMISTORPIN A1  // which analog pin for reading thermistor
#define REEDPIN 3  // which pin powers the reed switch 
#define INTERRUPTPIN 2 //Interrupt pin  (interrupt 0 is pin 2, interrupt 1 is pin 3)
#define INTERRUPTNUM 0 //Interrupt number  (make sure matches interrupt pin)

//
// RF Setup
//
#define CONTEXT_PIPE 0xF0F0F0F0F2LL     // 5 on PI - Pipe to transmit context on
#define CONTEXT_PAYLOADSIZE 3          // Size of package we're sending over the wire
#define STATE_PIPE   0xF0F0F0F0F1LL     // 4 on PI - Pipe to transmit state on
#define STATE_PAYLOADSIZE 12            // Size of package we're sending over the wire
#define EVENT_PIPE   0xF0F0F0F0E2LL     // 2 on PI - Pipe to transmit event on
#define EVENT_PAYLOADSIZE 4             // Size of package we're sending over the wire

//
// Config
//
#define SLEEPCYCLES 75  //Sleep cycles wanted  (75 is 10 min assuming 8s timer)
#define TRIGGERTYPE 1   //0- Interrupt is off, 1 - will trigger for both initial trigger of interrupt, AND release.  2 - only on initial interrupt
#define OPENSTATE LOW   //set pin (interrupt) state for what defines "open".  LOW is triggered   (only looked at if TRIGGERTYPE is non zero)
#define OPENINGTYPE 3   //0-not an opening, 1-generic door, 2-window, 3-garage door

//
// Termistor setup
//
#define THERMISTORNOMINAL 10000    // resistance at 25 degrees C
#define TEMPERATURENOMINAL 25   // temp. for nominal resistance (almost always 25 C)
#define NUMSAMPLES 10  // how many samples to take and average, more takes longer but is more 'smooth'
#define BCOEFFICIENT 3610 //For part number NTCS0603E3103HMT  - he beta coefficient of the thermistor (usually 3000-4000)
#define SERIESRESISTOR 10000  // the value of the 'other' resistor   
