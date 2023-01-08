#include <genieArduino.h>
#include <Wire.h>
#include <SoftwareSerial.h>

#include "Gauge.h"
#include "PID.h"
#include "Trip.h"

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

//defines
#define RESETLINE 4  //Bringing D4 low resets the display

#define TICK_MS 100   //Number of milli-seconds between "ticks" in the loop
#define DELAY_MS 10   //Milli-seconds we "delay" in loop

//Global objects
Genie genie;  
unsigned long currentTickCount;
unsigned long lastTickTime;
unsigned long nextRefreshTickTime;

//Supported gauges
Gauge loadGauge(&genie,0x41,0x04,0,0,0,100,1);  //genie*,service,pid,ang meter obj #,digits obj #,min,max,refresh ticks
Gauge boostGauge(&genie,0x41,0x0B,3,2,0,22,1); 
Gauge coolantTempGauge(&genie,0x41,0x05,1,1,130,250,10);  
Gauge transTempGauge(&genie,0x22,0x22,2,4,130,250,10);  

//Extra values needed for calculations
PID baraPressure(0x41,0x33);

//Trip
Trip trip = Trip(&genie);

//Serial coms
byte serialBuffer[20];
int currentComIdx=0;
bool msgStarted=false;

SoftwareSerial mySerial(10, 11); // RX, TX

long lastLoopTime;
long longestTick;

void setup()
{
  //USB port serial
  Serial.begin(115200);
  
  // set the data rate for the SoftwareSerial port that's used to communicate with can bus
  mySerial.begin(57600);
  
  // Serial1 for the TX/RX pins, as Serial0 is for USB.  
  Serial1.begin(115200);  
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
  genie.WriteContrast(10); // About 2/3 Max Brightness

  //Setup ticks
  currentTickCount=0;

  Serial.println("starting....");
  lastLoopTime=millis();


  //
  // We only have 1k of EEPROM
  //
  //TripSegment segment=TripSegment();
  //Serial.println(sizeof(trip));
  //Serial.println(sizeof(segment));
}

uint16_t checksumCalculator(uint8_t * data, uint16_t length)
{
   uint16_t curr_crc = 0x0000;
   uint8_t sum1 = (uint8_t) curr_crc;
   uint8_t sum2 = (uint8_t) (curr_crc >> 8);
   int index;
   for(index = 0; index < length; index = index+1)
   {
      sum1 = (sum1 + data[index]) % 255;
      sum2 = (sum2 + sum1) % 255;
   }
   return (sum2 << 8) | sum1;
}

bool processIncoming(int *service,int *pid,int *value)
{
  char data='\0';

  //Get to start of message
  while(mySerial.available() && !msgStarted)
  {
    data=mySerial.read();
    if(data=='[')
    {
      msgStarted=true;
      break;
    }
  }

  //Read message
  while(mySerial.available() && msgStarted)
  {
    data=mySerial.read();

    //start again?
    if(data=='[')
    {
      currentComIdx=0;
      data=mySerial.read();
    }

    //done?
    if(data==']')
    {
      serialBuffer[currentComIdx]='\0';
      break;
    }

    serialBuffer[currentComIdx]=data;
    currentComIdx++;
  }

  //tokenize if complete message
  if(data==']')
  {
    //reset flags
    msgStarted=false;
    currentComIdx=0;

    //check crc
    int crc=0;
    memcpy(&crc,serialBuffer+6,2);
    int calcdCRC=checksumCalculator(serialBuffer,6);
    if(crc!=calcdCRC)
    {
      //Just drop it and move on
      Serial.println("CRC ERROR!!!!");
      return false;
    }

    //copy values
    memcpy(service,serialBuffer,2);
    memcpy(pid,serialBuffer+2,2);
    memcpy(value,serialBuffer+4,2);

    return true;    
  }

  return false;  //nothing new
}

void loop()
{
  long currentMillis=millis();
  if((currentMillis-lastLoopTime)>longestTick || (currentMillis-lastLoopTime) > 100)
  {
      longestTick=currentMillis-lastLoopTime;
      Serial.println(longestTick/1000.0);
  }
  lastLoopTime=currentMillis;

  //read serial from canbus board
  int service; int pid; int value;
  bool retVal=processIncoming(&service,&pid,&value);

  //new values to process?
  if(retVal)
  { 
    updateGauges(service,pid,value);  //Update gauges
    trip.update(service,pid,value);   //Update trip info  (miles travelled, mpg, etc)
  }

  //Time to update trip display?
  trip.updateDisplay(currentTickCount);

  //Increment ticks to we keep timing sorted
  if((millis()-lastTickTime)>=TICK_MS)
  {
    lastTickTime=millis();
    currentTickCount++;
  }

  //Get any updates from display
  //genie.DoEvents(); // This calls the library each loop to process the queued responses from the display  (used to change forms???)

  //small delay might be nice?
  //delay(DELAY_MS);
}

void updateGauges(int service,int pid,int value)
{
    //grab values used for calculations
    if(baraPressure.isMatch(service,pid))
    {
      baraPressure.setValue(value);
    }

    //update gauges after doing any needed calculations
    if(loadGauge.isMatch(service,pid))
    {
      loadGauge.setValue(value);
      loadGauge.update(currentTickCount);
    }  
    if(coolantTempGauge.isMatch(service,pid))
    {
      coolantTempGauge.setValue((float)value*(9.0/5.0)+32.0);
      coolantTempGauge.update(currentTickCount);
    }
    if(transTempGauge.isMatch(service,pid))
    {
      transTempGauge.setValue((float)value*(9.0/5.0)+32.0);      
      transTempGauge.update(currentTickCount);
    }    
    if(boostGauge.isMatch(service,pid))
    {
      int bara=baraPressure.getValue();
      float boost=value-bara;
      boostGauge.setValue(boost*.145);
      boostGauge.update(currentTickCount);
    }
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
