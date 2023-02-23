#include <Wire.h>
#include <genieArduino.h>

#include "CurrentData.h"
#include "TripData.h"

#include "PrimaryForm.h"
#include "SummaryForm.h"

//
// SUPER IMPORT POST about setting up the programmer so that high speed transfers work
//
// https://forum.4dsystems.com.au/node/62492
//

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

//Using esp32 firebeatle where io25-->d2 and io26-->d3.  Note that io9/10 (default) conflict (probably with flash) and crash the board  
#define RXD1 25
#define TXD1 26

//Form object numbers
#define PRIMARY_FORM 0
#define STOPPED_FORM 1
#define SUMMARY_FORM 2
#define STARTING_FORM 3

#define SUMMARY_SCREEN_BUTTON 1
#define SUMMARY_TO_PRIMARY_BUTTON 3

//How often contrast and form switches based on external inputs happen
#define LCD_REFRESH_RATE 1000;

//Global objects for LCD
Genie genie;  
int currentActiveForm=0;
int currentContrast=10;
bool currentIgnState=false;
unsigned long nextLCDRefresh=0;
unsigned long totalMessages;
unsigned long totalCRC;

//Trip data
CurrentData currentData=CurrentData();
TripData sinceLastStop=TripData(&currentData);  //used for the primary form
TripData currentSegment=TripData(&currentData);
TripData fullTrip=TripData(&currentData);

//Forms
PrimaryForm primaryForm = PrimaryForm(&genie,PRIMARY_FORM,"Main Screen",&sinceLastStop,&currentData);
SummaryForm sinceLastStopSummaryForm = SummaryForm(&genie,SUMMARY_FORM,"Since Stopped",&currentSegment);

//Serial coms
byte serialBuffer[20];
int currentComIdx=0;
bool msgStarted=false;

long lastLoopTime;
long longestTick;

void setup()
{
  //USB port serial  (used for logging)
  Serial.begin(115200);
  delay(2000);

  //Used to receive CAN bus info from the other board
  Serial2.begin(921600); 
  
  //Used for talking to the display
  Serial1.begin(200000,SERIAL_8N1, RXD1, TXD1);
  genie.Begin(Serial1);   

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
  delay(5000); 

  //This is how we get notified when a button is pressed
  genie.AttachEventHandler(myGenieEventHandler); // Attach the user function Event Handler for processing events  

  //calibrate and setup
  currentData.init();

  Serial.println("starting....");
  lastLoopTime=millis();

  //
  // We only have 1k of EEPROM
  //
  //TripSegment segment=TripSegment();
  //Serial.println(sizeof(trip));
  //Serial.println(sizeof(segment));
}

void loop()
{
  //read serial from canbus board
  int service; int pid; int value;
  bool retVal=processIncoming(&service,&pid,&value);

  //new values to process?
  if(retVal)
  {
    //Update current data that's shared for everyone
    currentData.updateData(service,pid,value);

    //Now make sure all the trip data objects have the latest
    sinceLastStop.updateTripData();
    currentSegment.updateTripData();
    fullTrip.updateTripData();    

    //Update display for active forms only
    if(primaryForm.getFormId()==currentActiveForm)
    {   
      primaryForm.updateDisplay();  //Time to update trip display?
    }     
  } 

  //Update main LCD, like adjust contract, switch forms, etc
  updateLCD();

  //Get any updates from display  (like button pressed etc.)  Needs to be run as often as possible
  genie.DoEvents(); 
}

//These are user defined PIDs and sensors which control the main display in various ways
void updateLCD()
{
    //don't update if it's not time to
    if(millis()<nextLCDRefresh)
        return;

    //Update timing
    nextLCDRefresh=millis()+LCD_REFRESH_RATE;

    //Adjust screen based on light level
    int contrast = map(currentData.currentLightLevel, 500, 3000, 1, 15);
    if(contrast<1) contrast=1;
    if(contrast>15) contrast=15;
    if(currentContrast!=contrast)
    {
      Serial.println("adjusting CONTRAST");
      genie.WriteContrast(contrast);   
      currentContrast=contrast;
    }

    //Go to stopped form if ignition is turned off, or to start when first turned on
    bool state=currentData.ignitionState;
    if(state!=currentIgnState)
    {
      currentIgnState=state;

      if(currentIgnState)
      {
        Serial.println("activating STARTING form");
        currentActiveForm=STARTING_FORM;
        genie.WriteObject(GENIE_OBJ_FORM,STARTING_FORM,0);
      }
      else
      {
        Serial.println("activating STOPPING form");
        currentActiveForm=STOPPED_FORM;
        genie.WriteObject(GENIE_OBJ_FORM,STOPPED_FORM,0);
      }
      return;
    }

    //If on stopped form, and our speed goes above 0, switch to main
    if(currentActiveForm==STOPPED_FORM && currentData.currentSpeed>5)
    {
        Serial.println("activating PRIMARY form");
        currentActiveForm=PRIMARY_FORM;
        genie.WriteObject(GENIE_OBJ_FORM,PRIMARY_FORM,0);
        return;
    }
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
  while(Serial2.available() && !msgStarted)
  {
    data=Serial2.read();
    if(data=='[')
    {
      currentComIdx=0;
      msgStarted=true;
      break;
    }
  }

  //Read message
  while(Serial2.available() && msgStarted)
  {
    data=Serial2.read();

    //start again?
    if(data=='[')
    {
      currentComIdx=0;
      data=Serial2.read();
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
    //increment message count
    totalMessages++;

    //reset flags
    msgStarted=false;
    currentComIdx=0;

    //check crc
    int crc=0;
    memcpy(&crc,serialBuffer+6,2);
    int calcdCRC=checksumCalculator(serialBuffer,6);
    if(crc!=calcdCRC)
    {
      //If the percentage is high, we'll print to the screen
      totalCRC++;

      double crcFailureRate=(double)totalCRC/(double)totalMessages;
      if(crcFailureRate > .025)
      {
        Serial.print("Higher rate of CRC errors than normal: ");
        Serial.print(totalMessages);
        Serial.print("/");
        Serial.print(totalCRC);
        Serial.print(" Failure Rate: ");
        Serial.println(crcFailureRate);
      }

      //Just drop it and move on
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

//COMPACT VERSION HERE, LONGHAND VERSION BELOW WHICH MAY MAKE MORE SENSE
void myGenieEventHandler(void)
{
  genieFrame Event;
  genie.DequeueEvent(&Event);

  Serial.print("CMD: ");
  Serial.print(Event.reportObject.cmd);
  Serial.print(" OBJ: ");
  Serial.print(Event.reportObject.object);
  Serial.print(" IDX: ");
  Serial.println(Event.reportObject.index);  

  //If the cmd received is from a Reported Event (Events triggered from the Events tab of Workshop4 objects)
  if (Event.reportObject.cmd == GENIE_REPORT_EVENT)
  {
    //Form activate 
    if (Event.reportObject.object == GENIE_OBJ_FORM) //If a form is activated
    {
      currentActiveForm=Event.reportObject.index;
      Serial.print("Form Activated: ");
      Serial.println(currentActiveForm);
      return;
    }

    if (Event.reportObject.object == GENIE_OBJ_USERBUTTON)
    {
      if (Event.reportObject.index == SUMMARY_SCREEN_BUTTON)
      {
        Serial.println("activating SUMMARY form");
        currentActiveForm=SUMMARY_FORM;
        genie.WriteObject(GENIE_OBJ_FORM,SUMMARY_FORM,0);
        sinceLastStopSummaryForm.updateDisplay();
        return;
      }
    } 

    if (Event.reportObject.object == GENIE_OBJ_USERBUTTON) 
    {
      if (Event.reportObject.index == SUMMARY_TO_PRIMARY_BUTTON) 
      {
        Serial.println("activating PRIMARY form");
        currentActiveForm=PRIMARY_FORM;
        genie.WriteObject(GENIE_OBJ_FORM,PRIMARY_FORM,0);
        return;
      }
    }       

    if (Event.reportObject.object == GENIE_OBJ_WINBUTTON) // If a Winbutton was pressed
    {
      if (Event.reportObject.index == 0) // If Winbutton #0 was pressed   
      {
        Serial.println("Start Tip button pressed!");
        return;
      }
    }
  }
}

  /********** This can be expanded as more objects are added that need to be captured *************
  *************************************************************************************************
  Event.reportObject.cmd is used to determine the command of that event, such as an reported event
  Event.reportObject.object is used to determine the object type, such as a Slider
  Event.reportObject.index is used to determine the index of the object, such as Slider0
  genie.GetEventData(&Event) us used to save the data from the Event, into a variable.
  *************************************************************************************************/
