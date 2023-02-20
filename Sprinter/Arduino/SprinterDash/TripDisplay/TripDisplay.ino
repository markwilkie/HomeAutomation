#include <genieArduino.h>
#include <Wire.h>

#include "PrimaryForm.h"

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

//Global objects
Genie genie;  
int currentActiveForm=0;
unsigned long totalMessages;
unsigned long totalCRC;

//Supported gauges
Gauge loadGauge = Gauge(&genie,0x41,0x04,0,0,0,100,100);  //genie*,service,pid,ang meter obj #,digits obj #,min,max,refresh ticks
Gauge boostGauge = Gauge(&genie,0x41,0x0B,3,2,0,22,100); 
Gauge coolantTempGauge = Gauge(&genie,0x41,0x05,1,1,130,250,1000);  
Gauge transTempGauge = Gauge(&genie,0x61,0x30,2,4,130,250,1000);    //0x61, 0x30

//Split bar gauges
SplitBarGauge windSpeedGauge = SplitBarGauge(&genie,3,0,3,-29,30,500);  
SplitBarGauge instMPG = SplitBarGauge(&genie,1,2,0,20,500);

//Extra values needed for calculations
PID baraPressure = PID(0x41,0x33);
PID speed = PID(0x41,0x0D);

//Other sensors
Pitot pitot = Pitot(400);  //update every 400 ms        

//Create objects for the digit displays
Digits avgMPG = Digits(&genie,6,0,99,1,1100);
Digits milesLeftInTank = Digits(&genie,7,0,999,0,2000);
Digits currentElevation = Digits(&genie,8,0,9999,0,1400);
Digits milesTravelled = Digits(&genie,9,0,999,0,1900);
Digits hoursDriving = Digits(&genie,5,0,9,1,1900); 

//Forms
PrimaryForm primaryForm = PrimaryForm(&genie,0,"Main Screen");

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

  // Set the brightness/Contrast of the Display - (Not needed but illustrates how)
  // Most Displays use 0-15 for Brightness Control, where 0 = Display OFF, though to 15 = Max Brightness ON.
  genie.WriteContrast(10); // About 2/3 Max Brightness


  //This is how we get notified when a button is pressed
  genie.AttachEventHandler(myGenieEventHandler); // Attach the user function Event Handler for processing events  

  //calibrate
  pitot.calibrate();

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
    primaryForm.updateData(service,pid,value);   //Update trip info  (miles travelled, mpg, etc)

    //Update display for active forms only
    if(primaryForm.getFormId()==currentActiveForm)
    {   
      primaryForm.updateDisplay();  //Time to update trip display?
    }
  }

  //Get any updates from display  (like button pressed etc.)  Needs to be run as often as possible
  genie.DoEvents(); 
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
    }

    if (Event.reportObject.object == GENIE_OBJ_WINBUTTON) // If a Winbutton was pressed
    {
      if (Event.reportObject.index == 0) // If Winbutton #0 was pressed   
      {
        Serial.println("Start Tip button pressed!");
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
