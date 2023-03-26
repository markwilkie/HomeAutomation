#include <Wire.h>
#include <genieArduino.h>

#include "CurrentData.h"
#include "TripData.h"

#include "FormHelpers.h"
#include "PrimaryForm.h"
#include "SummaryForm.h"
#include "Forms.h"

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
#define LED_PIN D9  //Built in LED

//Power supply values
#define PS_PIN D7   // Note that ignition is on D4, see sensors.ino
#define PS_STAY_ON_TIME 30  //Time to stay on after ignition is off in seconds
unsigned long turnOffTime;

//Using esp32 firebeatle where io25-->d2 and io26-->d3.  Note that io9/10 (default) conflict (probably with flash) and crash the board  
#define RXD1 25
#define TXD1 26

//timing
#define LCD_REFRESH_RATE 5000;
#define SHUTDOWN_STARTUP_RATE 1000;  //how often we check ignition
#define VERIFY_TIMEOUT 60000;   //How long we'll wait for everything to come online when we first start

//Misc defines
#define NUMBER_OF_SUMMARY_FORMS 3

//Global objects for LCD
Genie genie;  
int currentContrast=10;
bool currentIgnState=false;
unsigned long nextLCDRefresh=0;
unsigned long nextIgnRefresh=0;
char displayBuffer[150];  //used for status form
bool online= false;  //If true, it means every PID and sensor is online  (will list those which are not on boot)

//Trip data
int currentActiveSummaryData=0;
CurrentData currentData=CurrentData();
TripData sinceLastStop=TripData(&currentData,0);  //used for the primary form
TripData currentSegment=TripData(&currentData,1);
TripData fullTrip=TripData(&currentData,2);

//Forms
FormNavigator formNavigator;
PrimaryForm primaryForm = PrimaryForm(&genie,PRIMARY_FORM,"Main Screen",&sinceLastStop,&currentData);
SummaryForm sinceLastStopSummaryForm = SummaryForm(&genie,SUMMARY_FORM,"Since Stopped",&sinceLastStop);
SummaryForm currentSegmentSummaryForm = SummaryForm(&genie,SUMMARY_FORM,"Current Segment",&currentSegment);
SummaryForm fullTripSummaryForm = SummaryForm(&genie,SUMMARY_FORM,"Full Trip",&fullTrip);
StartingForm startForm = StartingForm(&genie,STARTING_FORM,&fullTrip,1000);
StoppedForm stopForm = StoppedForm(&genie,STOPPED_FORM,&sinceLastStop,1000);
StatusForm statusForm = StatusForm(&genie,STATUS_FORM,600);

//Serial coms
byte serialBuffer[20];
int currentComIdx=0;
bool msgStarted=false;
unsigned long totalMessages;
unsigned long totalCRC;
double crcFailureRate;
double lastCrcFailureRate;

void setup()
{
  // Bring PS control pin high so that when we shut the van off, there's some time before everything shuts down
  pinMode(PS_PIN, OUTPUT); 
  digitalWrite(PS_PIN, 1); 

  //USB port serial  (used for logging)
  Serial.begin(115200);
  delay(2000);

  //Used to receive CAN bus info from the other board
  Serial2.begin(921600); 
  
  //Used for talking to the display
  Serial1.begin(200000,SERIAL_8N1, RXD1, TXD1);
  genie.Begin(Serial1);   

  // Display is being reset using the 'reset' pin on the processor, so it boots when the proc boots.
  //pinMode(RESETLINE, OUTPUT);  // Set D4 on Arduino to Output (4D Arduino Adaptor V2 - Display Reset)
  //digitalWrite(RESETLINE, 0);  // Reset the Display
  //delay(100);
  //digitalWrite(RESETLINE, 1);  // unReset the Display

  // Let the display start up after the reset (This is important)
  // Increase to 4500 or 5000 if you have sync problems as your project gets larger. Can depent on microSD init speed.
  Serial.println("Waiting for screen");
  delay(5000); 

  //This is how we get notified when a button is pressed
  formNavigator.init(&genie);
  genie.AttachEventHandler(myGenieEventHandler); // Attach the user function Event Handler for processing events  

  //calibrate and setup sensors
  Serial.println("Initializing sensors");
  currentData.init();
  delay(500);

  //Initialize or load data from EEPROM for each of the trip bucket objects
  Serial.println("Loading data from EEPROM");
  currentSegment.loadTripData();
  fullTrip.loadTripData();

  //Calibrate Pitot
  currentData.calibratePitot();

  Serial.println("Validating interfaces"); 
  verifyInterfaces();

  //Reset the data since last stop because we're booting
  Serial.println("Reseting trip data from last stop");
  sinceLastStop.resetTripData();  

  //Let's go!
  Serial.println("Starting now....");
}

void verifyInterfaces()
{
  //read serial from canbus board
  int service=0; int pid=0; int value=0;

  //Show setup form
  formNavigator.activateForm(STATUS_FORM); 
  statusForm.updateTitle("Validating Links");

  bool online=false;
  unsigned long timeout=millis()+VERIFY_TIMEOUT;
  while(!online && millis()<timeout)
  {
    displayBuffer[0]='\0';

    //get latest from sensors
    currentData.updateDataFromSensors();

    //get latest from PIDs coming in
    bool retVal=processIncoming(&service,&pid,&value);
    if(retVal)
    {
      online=currentData.verifyInterfaces(service,pid,value,displayBuffer);
      statusForm.updateText(displayBuffer);
    }

    //Show current serial error rate between processors
    if(crcFailureRate != lastCrcFailureRate)
    {
      lastCrcFailureRate=crcFailureRate;
      statusForm.updateStatus(totalMessages,totalCRC,crcFailureRate);
    }

    //so that the buttons will work
    genie.DoEvents();   

    //If someone pressed the button, time to bail
    if(formNavigator.getActiveForm()!=STATUS_FORM)
      return;
  }

  //Pause and give message if not all online
  if(!online)
  {
    statusForm.updateTitle("Links NOT available:");
    delay(10000);
  } 

  formNavigator.activateForm(STARTING_FORM);    
}

void loop()
{
  //read serial from canbus board
  int service; int pid; int value;
  bool retVal=processIncoming(&service,&pid,&value);

  //Update data from sensors
  currentData.updateDataFromSensors();

  //new values to process?
  if(retVal)
  {
    //Update current data that's shared for everyone
    currentData.updateDataFromPIDs(service,pid,value);

    //Now make sure all the trip data objects have the latest
    sinceLastStop.updateTripData();
    currentSegment.updateTripData();
    fullTrip.updateTripData();    

    //Update display for active forms only
    if(primaryForm.getFormId()==formNavigator.getActiveForm())
    {   
      primaryForm.updateDisplay();  //Time to update trip display?
    }     
    if(startForm.getFormId()==formNavigator.getActiveForm())
    {   
      startForm.updateDisplay();
    }  
    if(stopForm.getFormId()==formNavigator.getActiveForm())
    {   
      stopForm.updateDisplay(); 
    }
  }

  //Take care of business
  updateContrast();
  handleStatupAndShutdown();

  //Get any updates from display  (like button pressed etc.)  Needs to be run as often as possible
  genie.DoEvents(); 
}

//Update Contrast of LCD
void updateContrast()
{
    //don't update if it's not time to
    if(millis()<nextLCDRefresh)
        return;

    //Update timing
    nextLCDRefresh=millis()+LCD_REFRESH_RATE;

    //Adjust screen based on light level
    int contrast = map(currentData.currentLightLevel, 500, 3000, 1, 15);

    //slow down how "twitchy" it is
    if(currentContrast>contrast)
      contrast=currentContrast-1;
    if(currentContrast<contrast)
      contrast=currentContrast+1;

    //Check limits, then update
    if(contrast<1) contrast=1;
    if(contrast>15) contrast=15;
    if(currentContrast!=contrast)
    {
      genie.WriteContrast(contrast);   
      currentContrast=contrast;
    }
}

//Handle navigation and power supply status based on ignition state
void handleStatupAndShutdown()
{
    //don't update if it's not time to
    if(millis()<nextIgnRefresh)
        return;

    //Update timing
    nextIgnRefresh=millis()+SHUTDOWN_STARTUP_RATE;

    //Go to stopped form if ignition has changed, and is turned off
    bool state=currentData.ignitionState;
    if(state!=currentIgnState)
    {
      currentIgnState=state;

      if(!currentIgnState)
      {
        //Save to EEPROM
        Serial.println("saving to EEPROM and activating STOPPING form");
        currentSegment.saveTripData();
        fullTrip.saveTripData();

        //Set time to actually turn off
        turnOffTime=currentData.currentSeconds+PS_STAY_ON_TIME;

        //Activate stopping form
        formNavigator.activateForm(STOPPED_FORM);
      }
      return;
    }

    //If ign is Off AND it's time, shut things down
    if(!currentData.ignitionState && currentData.currentSeconds>=turnOffTime)
    {
      Serial.println("Shutting things down!");
      digitalWrite(PS_PIN, 0); 
      while(1) {delay(1000);}
    }

    //If on stopped form, and our speed goes above 0, switch to main
    if(formNavigator.getActiveForm()==STARTING_FORM && currentData.currentSpeed>5)
    {
      formNavigator.activateForm(PRIMARY_FORM);
      return;
    }
}

void calibratePitot()
{
    Serial.println("Calibrating Pitot");
    formNavigator.activateForm(STATUS_FORM); 
    statusForm.updateTitle("Calibrating Pitot");
    currentData.calibratePitot();

    //Now read the pitot gauge until the user exits out using a button
    statusForm.updateTitle("Current Readings");
    while(1)
    {
      int pitotSpeed=currentData.readPitot();
      statusForm.updateText(pitotSpeed);
      genie.DoEvents();   //so that the buttons will work

      //If someone pressed the button, time to bail
      if(formNavigator.getActiveForm()!=STATUS_FORM)
        break;
    }
}

void myGenieEventHandler(void)
{
  genieFrame event;
  genie.DequeueEvent(&event);

  Serial.print("CMD: ");
  Serial.print(event.reportObject.cmd);
  Serial.print(" OBJ: ");
  Serial.print(event.reportObject.object);
  Serial.print(" IDX: ");
  Serial.println(event.reportObject.index);

  //Act on returned action
  int action=formNavigator.determineAction(&event);
  switch(action)
  {
    case ACTION_ACTIVATE_PRIMARY_FORM:
      formNavigator.activateForm(PRIMARY_FORM);
      break;
    case ACTION_ACTIVATE_SUMMARY_FORM:
      formNavigator.activateForm(SUMMARY_FORM);
      sinceLastStopSummaryForm.updateDisplay();
      break;
    case ACTION_CYCLE_SUMMARY:
      Serial.println("cycling SUMMARY form");
       currentActiveSummaryData++;
      if(currentActiveSummaryData>=NUMBER_OF_SUMMARY_FORMS)
        currentActiveSummaryData=0;

      if(currentActiveSummaryData==0)
        sinceLastStopSummaryForm.updateDisplay();
      if(currentActiveSummaryData==1)
        currentSegmentSummaryForm.updateDisplay();
      if(currentActiveSummaryData==2)
        fullTripSummaryForm.updateDisplay();                    
      break; 
    case ACTION_ACTIVATE_STARTING_FORM:
      formNavigator.activateForm(STARTING_FORM);
      break;  
    case ACTION_ACTIVATE_MENU_FORM:
      formNavigator.activateForm(MENU_FORM);
      break;  
    case ACTION_START_NEW_TRIP:
      Serial.println("Start Trip button pressed!");
      currentSegment.resetTripData();
      fullTrip.resetTripData();        
      formNavigator.activateForm(PRIMARY_FORM);
      break;
    case ACTION_START_NEW_SEGMENT:
      Serial.println("Start Segment button pressed!");
      currentSegment.resetTripData();     
      formNavigator.activateForm(PRIMARY_FORM);
      break;
    case ACTION_ACTIVATE_STATUS_FORM:
      verifyInterfaces();
      break;
    case ACTION_PITOT:
      calibratePitot();
      break;
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

      crcFailureRate=(double)totalCRC/(double)totalMessages;
      if(crcFailureRate >= .05)
      {
        Serial.print("CRC Failure rate is high: ");
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
