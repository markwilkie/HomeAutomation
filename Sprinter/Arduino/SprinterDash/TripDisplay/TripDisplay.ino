#include <Wire.h>
#include <genieArduino.h>

#include "version.h"
#include "CurrentData.h"
#include "PropBag.h"
#include "TripData.h"
#include "VanWifi.h"

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
#define LCD_REFRESH_RATE 5000
#define SHUTDOWN_STARTUP_RATE 1000 //how often we check ignition
#define CHECK_START_VALUES 60000 //how often we check if we need to update start state
#define CAN_VERIFY_TIMEOUT 5000    //How long we'll wait for something from the CAN controller before showing error screen
#define VERIFY_TIMEOUT 20000      //How long we'll wait for everything to come online at a time

//Misc defines
#define NUMBER_OF_SUMMARY_FORMS 3
#define SECONDS_IDLING_THRESHOLD 300   //past this number stopped - but engine running - is considered a "stop".  Same as ign off...

//Global objects for LCD
Genie genie;  
VanWifi wifi;
int currentContrast=10;
bool currentIgnState=false;
bool currentIdlingState=false;
unsigned long nextLCDRefresh=0;
unsigned long nextIgnRefresh=0;
unsigned long nextIdlingRefresh=0;
unsigned long nextUpdateStartValues=0;
char displayBuffer[1050];  //used for status form
bool online= false;  //If true, it means every PID and sensor is online  (will list those which are not on boot)

//Trip data
int currentActiveSummaryData=0;
CurrentData currentData=CurrentData();
PropBag propBag=PropBag();
TripData sinceLastStop=TripData(&currentData,&propBag,2);  //used for the primary form and not save to eeprom
TripData currentSegment=TripData(&currentData,&propBag,0);
TripData fullTrip=TripData(&currentData,&propBag,1);

//Forms
FormNavigator formNavigator;
PrimaryForm primaryForm = PrimaryForm(&genie,PRIMARY_FORM,"Main Screen",&sinceLastStop,&currentData);
SummaryForm sinceLastStopSummaryForm = SummaryForm(&genie,SUMMARY_FORM,"Since Stopped",&sinceLastStop);
SummaryForm currentSegmentSummaryForm = SummaryForm(&genie,SUMMARY_FORM,"Current Segment",&currentSegment);
SummaryForm fullTripSummaryForm = SummaryForm(&genie,SUMMARY_FORM,"Full Trip",&fullTrip);
StartingForm startForm = StartingForm(&genie,STARTING_FORM,&fullTrip,1000);
StoppedForm stopForm = StoppedForm(&genie,STOPPED_FORM,&sinceLastStop,1000);
StatusForm statusForm = StatusForm(&genie,STATUS_FORM,600);
BootForm bootForm = BootForm(&genie,BOOT_FORM);

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

  //Let's begin
  logger.log(INFO,"=================================="); 
  logger.log(INFO,"            Booting"); 
  logger.log(INFO,"=================================="); 

  // Display is being reset using the 'reset' pin on the processor, so it boots when the proc boots.
  //pinMode(RESETLINE, OUTPUT);  // Set D4 on Arduino to Output (4D Arduino Adaptor V2 - Display Reset)
  //digitalWrite(RESETLINE, 0);  // Reset the Display
  //delay(100);
  //digitalWrite(RESETLINE, 1);  // unReset the Display

  // Let the display start up after the reset (This is important)
  // Increase to 4500 or 5000 if you have sync problems as your project gets larger. Can depent on microSD init speed.
  delay(4500); 

  //start wifi and http server
  bootForm.updateDisplay("Connecting to WiFi...",formNavigator.getActiveForm());
  wifi.startWifi();
  wifi.startServer();  

  //This is how we get notified when a button is pressed
  formNavigator.init(&genie);
  genie.AttachEventHandler(myGenieEventHandler); // Attach the user functiotion Event Handler for processing events  

  //calibrate and setup sensors
  logger.log(INFO,"Initializing sensors");
  bootForm.updateDisplay("Init Sensors...",formNavigator.getActiveForm());  
  currentData.init();
  currentData.updateDataFromSensors();
  delay(500);

  //Load property bag from EEPROM
  logger.log(INFO,"Loading prop data from EEPROM");
  bootForm.updateDisplay("Loading data from EEPROM...",formNavigator.getActiveForm());
  propBag.loadPropBag();

  //Initialize or load data from EEPROM for each of the trip bucket objects
  logger.log(INFO,"Loading trip data from EEPROM");
  currentSegment.loadTripData(propBag.getPropDataSize());
  fullTrip.loadTripData(propBag.getPropDataSize());

  //3 x because not everything always gets through
  logger.log(INFO,"Telling CAN to go");
  Serial2.write('!');
  delay(200);
  Serial2.write('!');
  delay(200);
  Serial2.write('!');
  
  logger.log(INFO,"Verifying CAN connection");
  bootForm.updateDisplay("Verifying CAN connection...",formNavigator.getActiveForm());
  verifyCAN();

  logger.log(INFO,"Validating interfaces"); 
  bootForm.updateDisplay("Verifying interfaces...",formNavigator.getActiveForm());  
  logger.sendLogs(wifi.isConnected());
  bool allOnline=verifyInterfaces();
  if(!allOnline)
    showOfflineLinks();

  //Update pitot calibration
  currentData.setPitotCalibrationFactor(propBag.data.pitotCalibration);

  //Reset the data since last stop because we're booting
  logger.log(INFO,"Reseting trip data from last stop");
  sinceLastStop.resetTripData();  

  //Update parked and stopped time
  currentSegment.ignitionOn();
  fullTrip.ignitionOn();    

  //Let's go!
  logger.log(INFO,"Starting now....");
  formNavigator.activateForm(STARTING_FORM);    

  logger.sendLogs(wifi.isConnected());  
}

//Verifying we're hearing from CAN bus
bool verifyCAN()
{
  //read serial from canbus board
  int service=0; int pid=0; int value=0;
  bool CANlinkOK=false;
  bool CANcntrOK=false;

  //Set timeout for how long we'll wait before showing the error screen
  unsigned long timeout=millis()+CAN_VERIFY_TIMEOUT;
  unsigned long nextRefresh=0;
  unsigned long nextUpdate=0;

  //Loop until we've got a successful link  
  while(!CANlinkOK)
  {
    //get latest from PIDs coming in
    bool retVal=processIncoming(&service,&pid,&value);
    if(retVal)
    {
      CANcntrOK=true;  //Ok, we're at least hearing from our canbed dual controller

      //Do we have a non zero pid?
      if(pid)
      {
        CANlinkOK=true;  //hooray!  we've got real data
      }
      else
      {
        nextUpdate=(millis()/1000)+value;  //Let's record when we'll get another update from the CAN controller
      }
    }

    //listen on web server and send logs
    if(wifi.isConnected())
      wifi.listen();
    logger.sendLogs(wifi.isConnected());

    //Time to show the error screen?
    if(millis()>timeout && !CANlinkOK && millis()>nextRefresh)
    {
      //set next form refresh
      nextRefresh=millis()+1000;

      //Switch forms if necessary
      if(formNavigator.getActiveForm()!=STATUS_FORM)
      {        
        formNavigator.activateForm(STATUS_FORM);          
        statusForm.updateTitle("CAN error");
      }

      //Show current CAN/ECU link state
      displayBuffer[0]='\0';
      if(CANcntrOK)
      {
        sprintf(displayBuffer, "Nothing from ECU\nNext update in %lu sec", nextUpdate-(millis()/1000));
        //logger.log(ERROR,displayBuffer);
        statusForm.updateText(displayBuffer);
      }
      else
      {
        statusForm.updateText("Connection problem\nwith CanDual board");
      }
    }
  } 

  return CANlinkOK;
}

//Verifying we have all sensors online and PIDs accounted for
bool verifyInterfaces()
{
  //read serial from canbus board
  int service=0; int pid=0; int value=0;

  bool allOnline=false;
  unsigned long timeout=millis()+VERIFY_TIMEOUT;

  while(!allOnline && millis()<timeout)
  {
    //get latest from PIDs coming in
    bool retVal=processIncoming(&service,&pid,&value);
    if(retVal)
    {         
      //Verify this particular PID
      allOnline=currentData.verifyInterfaces(service,pid,value);
    }
  }

  //Dump what we have
  currentData.dumpData();

  return allOnline;
}

void showOfflineLinks()
{
  //Show all link that are not online
  if(formNavigator.getActiveForm()!=STATUS_FORM)
  {
    formNavigator.activateForm(STATUS_FORM); 
  }
  statusForm.updateTitle("Links NOT available:");

  //List all expected sensors/PIDs that are not yet online
  displayBuffer[0]='\0';
  currentData.updateStatusForm(displayBuffer);
  statusForm.updateText(displayBuffer);

  //Show current serial error rate between processors
  if(crcFailureRate != lastCrcFailureRate)
  {
    lastCrcFailureRate=crcFailureRate;
    statusForm.updateStatus(totalMessages,totalCRC,crcFailureRate);
  }  

  delay(10000); 
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

    //Update elevation
    sinceLastStop.updateElevation();
    currentSegment.updateElevation();
    fullTrip.updateElevation();    

    //Update display for active forms only
    if(primaryForm.getFormId()==formNavigator.getActiveForm())
    {
      primaryForm.updateDisplay();  
    }     
    if(startForm.getFormId()==formNavigator.getActiveForm())
    {   
      startForm.updateDisplay();
    }  
  }

  //Take care of business
  updateContrast();
  updateStartValues();
  handleIdlingAsStop();
  handleStatupAndShutdown();

  //Get any updates from display  (like button pressed etc.)  Needs to be run as often as possible
  genie.DoEvents(); 

  //listen on web server and send logs
  wifi.listen();
  logger.sendLogs(wifi.isConnected());
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

//Update start values if needed.  This handles the case where we're going, but fuel or miles aren't online yet
void updateStartValues()
{
    //don't update if it's not time to
    if(millis()<nextUpdateStartValues)
        return;

    //Update timing
    nextUpdateStartValues=millis()+CHECK_START_VALUES;

    //check
    sinceLastStop.updateStartValuesIfNeeded();
    currentSegment.updateStartValuesIfNeeded();
    fullTrip.updateStartValuesIfNeeded();
}

void handleIdlingAsStop()
{
    //don't update if it's not time to
    if(millis()<nextIdlingRefresh)
        return;

    //Update timing
    nextIdlingRefresh=millis()+SHUTDOWN_STARTUP_RATE;

    //Are we idling and we don't already know it?
    if(currentData.currentSpeed<1 && !currentData.idlingAsStoppedFlag)
    {
      //Have we started the timer?
      if(currentData.idlingStartSeconds==0)
        currentData.idlingStartSeconds=currentData.currentSeconds;

      //Has it been over the threshold?
      if((currentData.currentSeconds-currentData.idlingStartSeconds)>SECONDS_IDLING_THRESHOLD)
      {
        logger.log(INFO,"Idling detected");
        currentData.idlingAsStoppedFlag=true;
      }
    }
    else
    {
      currentData.idlingStartSeconds=0;
    }

    //Need to reset now that we're moving again?
    if(currentData.currentSpeed>0 && currentData.idlingAsStoppedFlag)
    {
      logger.log(INFO,"Reseting idle state as we're moving again");
      currentData.idlingAsStoppedFlag=false;
      currentData.idlingStartSeconds=0;
    }

    //If idling state has changed, act like ign is turning off or on
    bool state=currentData.idlingAsStoppedFlag;
    if(state!=currentIdlingState)
    {
      currentIdlingState=state;    

      //ok, act like we're stopped now
      if(currentData.idlingAsStoppedFlag)
      {
        //Now make sure all the trip data objects have the latest
        sinceLastStop.ignitionOff();
        currentSegment.ignitionOff();
        fullTrip.ignitionOff();           

        //Activate stopping form
        formNavigator.activateForm(STOPPED_FORM);
        stopForm.updateDisplay();
      }
      else
      {
        //Reset the data since last stop because we're now going again after idling
        logger.log(INFO,"Reseting trip data after idling");
        sinceLastStop.resetTripData();  
        currentSegment.ignitionOn();
        fullTrip.ignitionOn();    
       
        //now that we're going again, show primary form
        formNavigator.activateForm(PRIMARY_FORM);
      }
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
        //Set time to actually turn off
        turnOffTime=currentData.currentSeconds+PS_STAY_ON_TIME;

        //Now make sure all the trip data objects have the latest
        sinceLastStop.ignitionOff();
        currentSegment.ignitionOff();
        fullTrip.ignitionOff();           

        //Save to EEPROM
        logger.log(INFO,"Saving trip and prop bag data to EEPROM and activating STOPPING form");
        logger.sendLogs(wifi.isConnected());
        propBag.savePropBag();
        currentSegment.saveTripData(propBag.getPropDataSize());
        fullTrip.saveTripData(propBag.getPropDataSize());

        //Activate stopping form
        formNavigator.activateForm(STOPPED_FORM);
        stopForm.updateDisplay();
      }
      return;
    }

    //If ign is Off AND it's time, shut things down
    if(!currentData.ignitionState && currentData.currentSeconds>=turnOffTime)
    {
      logger.log(INFO,"****  Shutting things down!");
      logger.sendLogs(wifi.isConnected());
      digitalWrite(PS_PIN, 0); 
      while(1) {delay(1000);}
    }

    //If on starting form, and our speed goes above 0, switch to main
    if(formNavigator.getActiveForm()==STARTING_FORM && currentData.currentSpeed>5)
    {
      formNavigator.activateForm(PRIMARY_FORM);
      return;
    }
}

void ShowDebugInfo()
{
    logger.log(VERBOSE,"Show debug info");
    formNavigator.activateForm(STATUS_FORM); 
    statusForm.updateTitle("Debug Info");

    //Try and start wifi if not already connected
    if(!wifi.isConnected())
    {
      statusForm.updateText("Wifi NOT connected\nTrying to connect...");
      wifi.startWifi();    
    }

    //check wifi connection
    if(!wifi.isConnected())
    {
      statusForm.updateText("Unable to Connect...");
    }

    //Show url if connected
    if(wifi.isConnected())
    {    
      String line = "SSID: "+wifi.getSSID()+"\nIP: "+wifi.getIP()+"\nDumped Logs";
      statusForm.updateText(line.c_str());
    }
    else
    {
      statusForm.updateText("Unable to Connect.\nDumped Logs");
    }

    //Output log data
    logCurrentData();
    logTripData();

    //Show version
    sprintf(displayBuffer, "Version: %s",SKETCH_VERSION);
    statusForm.updateStatus(displayBuffer);    
    while(1)
    {
      delay(100);
      genie.DoEvents();   //so that the buttons will work
      if(formNavigator.getActiveForm()!=STATUS_FORM)
        break;
    }
}

void showPitotInfo()
{
    logger.log(WARNING,"Calibrating Pitot");
    logger.sendLogs(wifi.isConnected());

    formNavigator.activateForm(STATUS_FORM); 
    statusForm.updateTitle("Calibrated Pitot");

    //Calibrate
    propBag.data.pitotCalibration=currentData.calibratePitot();
    statusForm.updateText(displayBuffer);

    //Show what we're doing
    char calibBuffer[200];
    sprintf(calibBuffer, "Calib Speed/Pitot: %d/%d=%f",currentData.currentSpeed,currentData.readPitot(),(double)propBag.data.pitotCalibration);
    Serial.println(calibBuffer);

    //Now read the pitot gauge until the user exits out using a button
    unsigned long nextUpdateTime=0;
    statusForm.updateStatus("Exit at any time...");    
    while(1)
    {
      if(millis()>nextUpdateTime)
      {
        sprintf(displayBuffer, "%s\n\nCurrent Pitot Speed: %d",calibBuffer,currentData.readPitot());
        statusForm.updateText(displayBuffer);
        nextUpdateTime=millis()+1000;  //update every second
      }
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

  logger.log(VERBOSE,"CMD: %d OBJ: &d IDX: %d",event.reportObject.cmd,event.reportObject.object,event.reportObject.index);

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
      logger.log(VERBOSE,"Cycling SUMMARY form");
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
      logger.log(VERBOSE,"Start Trip button pressed!");
      currentData.setTime(10);
      sinceLastStop.resetTripData();
      currentSegment.resetTripData();
      fullTrip.resetTripData();    
      propBag.resetPropBag();    
      formNavigator.activateForm(PRIMARY_FORM);
      break;
    case ACTION_START_NEW_SEGMENT:
      logger.log(VERBOSE,"Start Segment button pressed!");
      currentSegment.resetTripData();     
      formNavigator.activateForm(PRIMARY_FORM);
      break;
    case ACTION_ACTIVATE_STATUS_FORM:
      verifyInterfaces();
      break;
    case ACTION_DEBUG:
      ShowDebugInfo();
      break;      
    case ACTION_PITOT:
      showPitotInfo();
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

    //make sure we don't overflow
    if(currentComIdx>18)
    {
      Serial2.flush();
      return false;
    }
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
      //diag
      /*
      int _service;int _pid;int _value;
      memcpy(&_service,serialBuffer,2);
      memcpy(&_pid,serialBuffer+2,2);
      memcpy(&_value,serialBuffer+4,2);      
      Serial.print("CRC ERROR: service: ");Serial.print(_service,HEX);Serial.print("/");Serial.print(_pid,HEX);Serial.print(":");Serial.print(_value);
      Serial.print("   ---> crc/ccrc: ");Serial.print(crc);Serial.print("/");Serial.println(calcdCRC);
      */
      
      //If the percentage is high, we'll print to the screen
      totalCRC++;

      crcFailureRate=(double)totalCRC/(double)totalMessages;
      if(crcFailureRate >= .05)
      {
        logger.log(INFO,"CRC Failure rate is over 5%: %lu/%lu  Rate: %f",totalMessages,totalCRC,crcFailureRate);
        logger.sendLogs(wifi.isConnected());
      }
      if(crcFailureRate >= .30)
      {
        logger.log(ERROR,"CRC Failure rate is VERY high: %lu/%lu  Rate: %f",totalMessages,totalCRC,crcFailureRate);
        logger.sendLogs(wifi.isConnected());
      }      

      //Just drop it and move on
      return false;
    }

    //copy values
    *service=0;*pid=0;*value=0;  //reseting vars because we're only filling the first 2 bytes
    memcpy(service,serialBuffer,2);
    memcpy(pid,serialBuffer+2,2);
    memcpy(value,serialBuffer+4,2);

    return true;    
  }

  return false;  //nothing new
}

//post in response to a GET request to show which options are avaialble
void logTripData()
{
  logger.log(INFO,"Since Last Stop");
  sinceLastStop.dumpTripData();
  logger.log(INFO,"Current Segment");
  currentSegment.dumpTripData();
  logger.log(INFO,"Full Trip");
  fullTrip.dumpTripData();
  propBag.dumpPropBag();
  logger.sendLogs(wifi.isConnected());

  wifi.sendResponse("Done!");
}

void logCurrentData()
{
  logger.log(INFO,"Dumping Current Data");
  currentData.dumpData();
  logger.log(INFO,"Current CRC Failures (msg/err): %lu/%lu  Rate: %f",totalMessages,totalCRC,crcFailureRate);  
  logger.sendLogs(wifi.isConnected());

  wifi.sendResponse("Done!");
}
