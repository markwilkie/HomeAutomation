#include <genieArduino.h>

// This Demo communicates with a 4D Systems Display, configured with ViSi-Genie, utilising the Genie Arduino Library - https://github.com/4dsystems/ViSi-Genie-Arduino-Library.
// The display has a slider, a cool gauge, an LED Digits, a string box and a User LED. Workshop4 Demo Project is located in the /extras folder
// The program receives messages from the Slider0 object using the Reported Events. This is triggered each time the Slider changes on the display, and an event
// is genereated and sent automatically. Reported Events originate from the On-Changed event from the slider itself, set in the Workshop4 software.
// Coolgauge is written to using Write Object, and the String is updated using the Write String command, showing the version of the library.
// The User LED is updated by the Arduino, by first doing a manual read of the User LED and then toggling it based on the state received back.

// As the slider changes, it sends its value to the Arduino (Arduino also polls its value using genie.ReadObject, as above), and the Arduino then
// tells the LED Digit to update its value using genie.WriteObject. So the Slider message goes via the Arduino to the LED Digit.
// Coolgauge is updated via simple timer in the Arduino code, and updates the display with its value.
// The User LED is read using genie.ReadObject, and then updated using genie.WriteObject. It is manually read, it does not use an Event.

// This demo illustrates how to use genie.ReadObject, genie.WriteObject, Reported Messages (Events), genie.WriteStr, genie.WriteContrast, plus supporting functions.

// Application Notes on the 4D Systems Website that are useful to understand this library are found: https://docs.4dsystems.com.au/app-notes
// Good App Notes to read are: 
// ViSi-Genie Connecting a 4D Display to an Arduino Host - https://docs.4dsystems.com.au/app-note/4D-AN-00017/
// ViSi-Genie Writing to Genie Objects Using an Arduino Host - https://docs.4dsystems.com.au/app-note/4D-AN-00018/
// ViSi-Genie A Simple Digital Voltmeter Application using an Arduino Host - https://docs.4dsystems.com.au/app-note/4D-AN-00019/
// ViSi-Genie Connection to an Arduino Host with RGB LED Control - https://docs.4dsystems.com.au/app-note/4D-AN-00010/
// ViSi-Genie Displaying Temperature values from an Arduino Host - https://docs.4dsystems.com.au/app-note/4D-AN-00015/
// ViSi-Genie Arduino Danger Shield - https://docs.4dsystems.com.au/app-note/4D-AN-00025

/*
We're solving for a position at a given time, e.g. p(t)

t = time = where in time (x axis) I'm at
b = beginning = beginning position
c = change = how much it'll be changed at the end, or end pos - beg pos (velocity is c/d)
d = duration = how long we want to take to change

So for linear, (e.g. car travelling at a specific speed) it'd be:
t = 2 hours
position = 2 X velocity
p = 2 X (change / duration)    e.g. 120 miles traveled / 2 hours == 60

Of the 4 variables, only 1 changes....time.  So, we'll solve for position given time.

So for a gauge, the variables will corrospond to:

t = time = current millis()
b = beginning = gauge # (e.g. 0 for the load gauge)
c = change = e.g. end pos - beg pos.  This case let's say it's 40-0 (velocity is c/d)
d = duration = 500 * (c / range)   e.g. 500*(40/100) (500ms for the biggest swing possible?  Should be a configurable value)
 */

//defines
#define RESETLINE 4  //Bringing D4 low resets the display

#define TICK_MS 250   //Number of milli-seconds between "ticks" in the loop
#define DELAY_MS 10   //Milli-seconds we "delay" in loop

#define CHANGE_THRESHOLD 0  //10 percent = .1...e.g., we'll just adjust end position and not reset curve if within 10%
#define REFRESH_TICKS 4       //how many ticks the screen refreshes

#define LOAD_TICK_NEW_VALUE_MAX ((1*1000)/TICK_MS)   //1-max is the range of how long to wait before giving a new value
#define LOAD_TICKS 2                                 //Frequency of updates
#define LOAD_NOR_FACTOR 100.0                        //Range for load so it's used to normalize


//Global objects
Genie genie;  
unsigned long currentTickCount;
unsigned long lastTickTime;
unsigned long nextRefreshTickTime;
unsigned long nextNewLoadTickTime;
unsigned long nextLoadTickTime;

//Load globals
float loadReading;  //current reading from CAN bus
float lastLoadReading;  //last reading from CAN bus to use for comparison
float loadEndPos;
float loadCurrentReading;
float loadBeg;
float loadChange;
long loadDuration;
long loadStartTime;  //recorded when we enter value update  (not when a new value comes across the wire)

void setup()
{
  Serial.begin(115200);
  
  // Serial1 for the TX/RX pins, as Serial0 is for USB.  
  Serial1.begin(9600);  
  genie.Begin(Serial1);   // Use Serial1 for talking to the display (Serial is for serial terminal and programming)

  //genie.AttachEventHandler(myGenieEventHandler); // Attach the user function Event Handler for processing events

  // Reset the Display (change D4 to D2 if you have original 4D Arduino Adaptor)
  // THIS IS IMPORTANT AND CAN PREVENT OUT OF SYNC ISSUES, SLOW SPEED RESPONSE ETC
  // If NOT using a 4D Arduino Adaptor, digitalWrites must be reversed as Display Reset is Active Low, and
  // the 4D Arduino Adaptors invert this signal so must be Active High.  
  pinMode(RESETLINE, OUTPUT);  // Set D4 on Arduino to Output (4D Arduino Adaptor V2 - Display Reset)
  digitalWrite(RESETLINE, 0);  // Reset the Display via D4
  delay(100);
  digitalWrite(RESETLINE, 1);  // unReset the Display via D4

  // Let the display start up after the reset (This is important)
  // Increase to 4500 or 5000 if you have sync problems as your project gets larger. Can depent on microSD init speed.
  delay (5000); 

  // Set the brightness/Contrast of the Display - (Not needed but illustrates how)
  // Most Displays use 0-15 for Brightness Control, where 0 = Display OFF, though to 15 = Max Brightness ON.
  // Some displays are more basic, 1 (or higher) = Display ON, 0 = Display OFF.  
  genie.WriteContrast(10); // About 2/3 Max Brightness

  //Write to String0 on the Display to show the version of the library used
  //genie.WriteStr(0, GENIE_VERSION);
  //OR to illustrate (comment out the above, uncomment the below)
  //genie.WriteStr(0, (String) "Hello 4D World");

  //Setup ticks
  currentTickCount=0;

  //general
  randomSeed(analogRead(0));  //seed psuedo ran number generator

  //Init
  loadCurrentReading=0;
  nextNewLoadTickTime=0;
}

void loop()
{   
  //Determine how often a new value comes in (simulates values from the CAN bus)
  if((currentTickCount>=nextNewLoadTickTime) || currentTickCount==0)
  {
    lastLoadReading=loadReading;
    loadReading=random(100);  //number between 0 and 100....e.g. load
    nextNewLoadTickTime=currentTickCount+random(LOAD_TICK_NEW_VALUE_MAX-1)+1;  //generate next "random" time to generate a new value
  }
  //else if(currentTickCount>=(nextNewLoadTickTime/10))    //Simulates the smaller random fluxuations fromt he CAN bus
  //{
  //  lastLoadReading=loadReading;
  //  loadReading=loadReading+(5-random(10));  //small number that's both negative and positive
  //}  

  //Determine if it's time to update load value (e.g. generate a new smoothing curve)
  int loadDelta=abs(lastLoadReading-loadReading);  
  if((currentTickCount>=nextLoadTickTime || currentTickCount==0) && loadDelta>0)
  {
    if(loadDelta>(float)loadReading*CHANGE_THRESHOLD)
    {   
      loadStartTime=millis();
      loadDuration=(LOAD_TICKS*TICK_MS);
      loadBeg=loadCurrentReading;
      loadEndPos=loadReading/LOAD_NOR_FACTOR;   //normalize load to between 0-1    
      loadChange=loadEndPos-loadBeg;
    }
    else
    {
      loadEndPos=loadReading/LOAD_NOR_FACTOR;   //normalize load to between 0-1    
      loadChange=loadEndPos-loadBeg;    
    }
    nextLoadTickTime=currentTickCount+LOAD_TICKS;
    lastLoadReading=loadReading;
  }

  //Update load smoothing using time, not ticks of course
  long tms=millis()-loadStartTime;
  if(tms<=loadDuration)
  {
    float t=(float)tms/loadDuration;  //needs to be normalized between 0 and 1
    if (t < .5) {
      loadCurrentReading = (loadChange) * (t*t*t*4) + loadBeg;
    }
    else {
      float tt=(t*2)-2;
      loadCurrentReading = (loadChange) * ((tt*tt*tt*.5)+1) + loadBeg;
    }

    //Update gauge  (gotta have this here because of the smoothing)
    genie.WriteObject(GENIE_OBJ_IANGULAR_METER, 0, loadCurrentReading*LOAD_NOR_FACTOR);
    genie.WriteObject(GENIE_OBJ_ILED_DIGITS, 0, loadReading);       
  }
  /*
  else if(currentTickCount>=nextRefreshTickTime)    //Refresh screen if time too
  {
    //Update gauge
    genie.WriteObject(GENIE_OBJ_IANGULAR_METER, 0, loadReading);
    genie.WriteObject(GENIE_OBJ_ILED_DIGITS, 0, loadReading);    

    nextRefreshTickTime=currentTickCount+REFRESH_TICKS;
  }
  */

  //Increment ticks
  if((millis()-lastTickTime)>=TICK_MS)
  {
    lastTickTime=millis();
    currentTickCount++;
  }

  //Get any updates from display
  //genie.DoEvents(); // This calls the library each loop to process the queued responses from the display

  //small delay to be nice
  delay(DELAY_MS);
}

/////////////////////////////////////////////////////////////////////
//
// This is the user's event handler. It is called by genieDoEvents()
// when the following conditions are true
//
//    The link is in an IDLE state, and
//    There is an event to handle
//
// The event can be either a REPORT_EVENT frame sent asynchronously
// from the display or a REPORT_OBJ frame sent by the display in
// response to a READ_OBJ (genie.ReadObject) request.
//

/* COMPACT VERSION HERE, LONGHAND VERSION BELOW WHICH MAY MAKE MORE SENSE
void myGenieEventHandler(void)
{
  genieFrame Event;
  genie.DequeueEvent(&Event);

  int slider_val = 0;

  //Filter Events from Slider0 (Index = 0) for a Reported Message from Display
  if (genie.EventIs(&Event, GENIE_REPORT_EVENT, GENIE_OBJ_SLIDER, 0))
  {
    slider_val = genie.GetEventData(&Event);  // Receive the event data from the Slider0
    genie.WriteObject(GENIE_OBJ_LED_DIGITS, 0, slider_val);     // Write Slider0 value to LED Digits 0
  }

  //Filter Events from UserLed0 (Index = 0) for a Reported Object from Display (triggered from genie.ReadObject in User Code)
  if (genie.EventIs(&Event, GENIE_REPORT_OBJ,   GENIE_OBJ_USER_LED, 0))
  {
    bool UserLed0_val = genie.GetEventData(&Event);               // Receive the event data from the UserLed0
    UserLed0_val = !UserLed0_val;                                 // Toggle the state of the User LED Variable
    genie.WriteObject(GENIE_OBJ_USER_LED, 0, UserLed0_val);       // Write UserLed0_val value back to UserLed0
  }
} */

/*

// LONG HAND VERSION, MAYBE MORE VISIBLE AND MORE LIKE VERSION 1 OF THE LIBRARY
void myGenieEventHandler(void)
{
  genieFrame Event;
  genie.DequeueEvent(&Event); // Remove the next queued event from the buffer, and process it below

  int slider_val = 0;

  //If the cmd received is from a Reported Event (Events triggered from the Events tab of Workshop4 objects)
  if (Event.reportObject.cmd == GENIE_REPORT_EVENT)
  {
    if (Event.reportObject.object == GENIE_OBJ_SLIDER)                // If the Reported Message was from a Slider
    {
      if (Event.reportObject.index == 0)                              // If Slider0 (Index = 0)
      {
        slider_val = genie.GetEventData(&Event);                      // Receive the event data from the Slider0
        genie.WriteObject(GENIE_OBJ_LED_DIGITS, 0, slider_val);       // Write Slider0 value to LED Digits 0
      }
    }
  }

  //If the cmd received is from a Reported Object, which occurs if a Read Object (genie.ReadOject) is requested in the main code, reply processed here.
  if (Event.reportObject.cmd == GENIE_REPORT_OBJ)
  {
    if (Event.reportObject.object == GENIE_OBJ_USER_LED)              // If the Reported Message was from a User LED
    {
      if (Event.reportObject.index == 0)                              // If UserLed0 (Index = 0)
      {
        bool UserLed0_val = genie.GetEventData(&Event);               // Receive the event data from the UserLed0
        UserLed0_val = !UserLed0_val;                                 // Toggle the state of the User LED Variable
        genie.WriteObject(GENIE_OBJ_USER_LED, 0, UserLed0_val);       // Write UserLed0_val value back to UserLed0
      }
    }
  }
}
  */

  /********** This can be expanded as more objects are added that need to be captured *************
  *************************************************************************************************
  Event.reportObject.cmd is used to determine the command of that event, such as an reported event
  Event.reportObject.object is used to determine the object type, such as a Slider
  Event.reportObject.index is used to determine the index of the object, such as Slider0
  genie.GetEventData(&Event) us used to save the data from the Event, into a variable.
  *************************************************************************************************/
