#include <stdio.h>
#include <stdlib.h>     /* exit, EXIT_FAILURE */
#include <iostream>
#include <exception>
#include <string.h>
#include <time.h>
#include <unistd.h> //sleep
#include <signal.h>
#include <RF24/RF24.h>
#include <curl/curl.h>
#include <sys/resource.h> 

#include "Logger.h"
#include "HomeAutomationWebAPI.h"

void SigHandler(int s);
void ReadDeviceData();
void BuildAndSendPower();
void BuildAndSendEvent();
void BuildAndSendAlarm();
void BuildAndSendState();
void BuildAndSendContext();
void TimeStamp(FILE *file);
long GetLocalEpoch();
void FlushFileHandles();

// Setup for GPIO 15 CE and CE0 CSN with SPI Speed @ 8Mhz
RF24 radio(RPI_V2_GPIO_P1_15, RPI_V2_GPIO_P1_24, BCM2835_SPI_SPEED_8MHZ);

// First pipe is for writing, 2nd, 3rd, 4th, 5th & 6th is for reading...
const uint64_t pipes[6] = { 0xF0F0F0F0D2LL, 0xF0F0F0F0E1LL, 
                            0xF0F0F0F0E2LL, 0xF0F0F0F0E3LL, 
                            0xF0F0F0F0F1LL, 0xF0F0F0F0F2LL };

//
// Data structures for RF
//
// Payload size - 32 is the default.  Must be the same on transmitter
const uint8_t MAX_PAYLOAD_SIZE = 32;  
const uint8_t MAX_POSTDATA_SIZE = 128+1; 
uint8_t pipeNo;
int lastPayloadLen;
uint8_t bytesRecv[MAX_PAYLOAD_SIZE+1];

FILE *logFile;
FILE *rfFile;
time_t rawtime;
struct tm * timeinfo;
struct rusage memInfo;

//Automation API
HomeAutomationWebAPI api;

int main()
{
    //Ignore sig pipe errors.  There's a bug in curl...
    signal(SIGPIPE, SigHandler);

    //Instanciate API object
    LogLine() << "Starting Instance";
    api=HomeAutomationWebAPI();

    bool authenticatedFlag=false;
    int failCounter=0;
    while(!authenticatedFlag)
    {
        try
        {
            LogLine() << "Authenticating...";
        }
        catch(const std::exception& e)
        {
            LogErrorLine() << "Exception thrown while trying to authenticate: " << e.what();
            sleep(1);
        }

        //Don't loop forever
        if(++failCounter>5)
        {
            LogErrorLine() << "Tried authenticating more than 5 times.  Aborting and existung";
            exit(1);
        }
    }

    //open file for logging
    logFile = fopen("/home/pi/code/rfHub/HubMasterLog.txt", "w");
    rfFile = fopen("/home/pi/code/rfHub/RFLog.txt", "w");
    freopen ("/home/pi/code/rfHub/HubMasterStderr.txt","w",stderr);
    freopen ("/home/pi/code/rfHub/HubMasterStdout.txt","w",stdout);

    //tag log files
    time(&rawtime);
    fprintf(logFile,"-\n-\n-\nHubMaster Start - %s-\n\n",ctime(&rawtime));
    fprintf(rfFile, "-\n-\n-\nHubMaster Start - %s-\n\n", ctime(&rawtime));
    fprintf(stderr,"-\n-\n-\nHubMaster Start - %s\n-\n",ctime(&rawtime));
    fprintf(stdout,"-\n-\n-\nHubMaster Start - %s\n-\n",ctime(&rawtime));

    //setup radio
    radio.begin();
    radio.setPALevel(RF24_PA_MAX);          // Higher power level
    radio.setDataRate(RF24_250KBPS);        // Slower datarate for more distance
    radio.setAutoAck(1);                    // Ensure autoACK is enabled
    radio.enableAckPayload();               // Allow optional ack payloads
    radio.setRetries(15,15);                // Smallest time between retries, max no. of retries
    radio.enableDynamicPayloads();          // Read size off chip
    radio.printDetails();                   // Dump the configuration of the rf unit for debugging

    // Open 6 pipes for readings ( 5 plus pipe0, also can be used for reading )
    radio.openWritingPipe(pipes[0]);
    radio.openReadingPipe(1,pipes[1]); //Power reading pipe
    radio.openReadingPipe(2,pipes[2]); //Event pipe
    radio.openReadingPipe(3,pipes[3]); //Alarm pipe
    radio.openReadingPipe(4,pipes[4]); //State protocol
    radio.openReadingPipe(5,pipes[5]); //Context

    //Start listening!
    radio.startListening();

    //Flush logs
    FlushFileHandles();

    //Loop, checking for radio packets
    while(1)
    {
      //Check if something's on the radio
      if(radio.available(&pipeNo))
      {  
        //read data from the pipe
        ReadDeviceData();

        //Based on pipe, build and post data
        switch(pipeNo)
        { 
        case 1: //Power
            //buildPowerAPICall();
            //callSecureWebAPI(url, authHeader);
            break;
        case 2: //Motion
            BuildAndSendEvent();
            break;
        case 3: //Alarm
			BuildAndSendAlarm();
            break;
        case 4: //state protocol
            BuildAndSendState();
            break;
        case 5: //context protocol
            BuildAndSendContext();
            break;
        default: //Unexpected??
            TimeStamp(stderr);
            fprintf(stderr, "ERROR: Unexpected pipe number: %d\n",pipeNo);
        }        

        //Flush logs
        FlushFileHandles();
      }

      //check for carrier
      delay(150);
      if (radio.testCarrier())
      {
        fprintf(logFile,"^");
        FlushFileHandles();
      }
      delay(350);
    }
    return 0;
}

void SigHandler(int s) 
{
    TimeStamp(stderr);
    fprintf(stderr, "ERROR: Caught SIGPIPE error: %d\n", s);
    fflush(stderr);
}

void TimeStamp(FILE *file)
{
    //Dump header
    getrusage(RUSAGE_SELF,&memInfo);
    time(&rawtime);
    timeinfo = localtime (&rawtime);
    char *time=asctime(timeinfo);
    time[(strlen(time))-1]=0;
    fprintf(file,"Mem:%lu %s: ",memInfo.ru_maxrss,time);
    fflush(file);  
}

long GetLocalEpoch()
{
  time_t t = time(NULL);
  struct tm lt = {0};
  localtime_r(&t, &lt);

  return (t+lt.tm_gmtoff);
}

void FlushFileHandles()
{
    static int newLineCounter = 0;

    //If time for a newline, do it now
    if(++newLineCounter > 80)
    {
       TimeStamp(logFile);
       fprintf(logFile,"\n");

       //timeStamp(rfFile);
       //fprintf(rfFile, "\n");
       newLineCounter=0;
    }

    fflush(logFile);  
    fflush(rfFile);
    fflush(stderr); 
    fflush(stdout); 
}

void ReadDeviceData()
{
   // Clear any unused ACK payloads      
   radio.flush_tx();                                
                  
   //Read now
   lastPayloadLen = radio.getDynamicPayloadSize();
   radio.read(bytesRecv,lastPayloadLen);

   // Since this is a call-response. Respond directly with an ack payload.
   // Ack payloads are much more efficient than switching to transmit mode to respond to a call
   radio.writeAckPayload(pipeNo,&pipeNo,1);
}

/*
//Build post message for pipe 1
//
//Power sensor pipe
//
void buildPowerAPICall()
{
    time_t seconds_past_epoch = getLocalEpoch();
    uint8_t addr;
    float reading;

    addr = bytesRecv[0];
    memcpy(&reading, &bytesRecv[1], 4);
    sprintf(postData, "{'DeviceName':'Power%d','DeviceDate':'%ld','DeviceData1':'%f'}", addr, seconds_past_epoch, reading);
    //fprintf(logFile,"Addr: %d  Reading: %f\n",addr,reading);

    fprintf(logFile, ".");  //Small indication for log file
    fflush(logFile);
}
*/

//Build post message for pipe 2
//
// Event pipe
//
void BuildAndSendEvent()
{
    //
    // Over-the-air packet definition/spec
    //
    // 1 byte:  unit number (uint8)
    // 1 byte:  Payload type (S=state, C=context, E=event  
    // 2 bytes: Number of retries on last send (int)
    // 2 bytes: Number of send attempts since last successful send (int)  
    // 1 byte:  eventCodeType (O-opening change)
    // 1 byte:  eventCode (O-opened, C-closed, D-Detection)
    //

    time_t seconds_past_epoch = GetLocalEpoch();
    char eventCodeType,eventCode;
    int unitNum, lastRetryCount, lastGoodTransCount;

    unitNum=(uint8_t)bytesRecv[0];
    lastRetryCount=(uint16_t)bytesRecv[2];
    lastGoodTransCount=(uint16_t)bytesRecv[4];
    eventCodeType=bytesRecv[6];
    eventCode=bytesRecv[7];

    if(lastRetryCount > 0 || lastGoodTransCount>0)
    {
        TimeStamp(rfFile);
        fprintf(rfFile, "EVENT: U: %d - R: %d, A: %d\n", unitNum, lastRetryCount, lastGoodTransCount);  //Small indication for log file
        fflush(rfFile);
    }

    api.AddEvent(unitNum,eventCodeType,eventCode,(long)seconds_past_epoch);
    fprintf(logFile, "%c",eventCode);  //Small indication for log file
    fflush(logFile);
}

//Build post message for pipe 3 
//
//Alarm pipe
//
void BuildAndSendAlarm()
{
    //
    // Over-the-air packet definition/spec
    //
    // 1 byte:  unit number (uint8)
    // 1 byte:  Payload type (S=state, C=context, E=event  
    // 2 bytes: Number of retries on last send (int)
    // 2 bytes: Number of send attempts since last successful send (int)  
    // 1 byte:  alarmCodeType (A-Alarm)
    // 1 byte:  alarmCode (W-Water,F-Fire)
    //

    char alarmCodeType, alarmCode;
    int unitNum, lastRetryCount, lastGoodTransCount;

    unitNum=(uint8_t)bytesRecv[0];
    lastRetryCount=(uint16_t)bytesRecv[2];
    lastGoodTransCount=(uint16_t)bytesRecv[4];
    alarmCodeType = bytesRecv[6];
    alarmCode = bytesRecv[7];

    if(lastRetryCount > 0 || lastGoodTransCount>0)
     {
        TimeStamp(rfFile);
        fprintf(rfFile, "ALARM: U: %d - R: %d, A: %d\n", unitNum, lastRetryCount, lastGoodTransCount);  //Small indication for log file
        fflush(rfFile);
    }

    //Post data for ALARM 
	api.SendAlarm(unitNum, alarmCodeType, alarmCode);
    //sprintf(postData, "{'PayloadType':'%s','UnitNum':'%d','EventCodeType':'%c','EventCode':'%c','DeviceDate':'%ld'}", "ALARM", unitNum, alarmCodeType, alarmCode, seconds_past_epoch);

	fprintf(logFile, "%c", alarmCode);  //Small indication for log file
    fflush(logFile);
}

//Build post message for pipe 4
//
//State pipe
//
void BuildAndSendState()
{
    //
    // Over-the-air packet definition/spec
    //
  // 1:1 byte:  unit number (uint8)
  // 1:2 byte:  Payload type (S=state, C=context, E=event
  // 2:4 bytes: Number of retries on last send (int)
  // 2:6 bytes: Number of send attempts since last successful send (int)
  // 4:10 bytes: VCC (float)
  // 4:14 bytes: Temperature (float)
  // 1:15 byte:  Presence  ('P' = present, and 'A' = absent)
  // 1:16 byte:  Data Type  (a=ADC)
  // 2:18 bytes: value itself
    //

    float vcc, reading;
    time_t seconds_past_epoch = GetLocalEpoch();
    char presence;
    int unitNum, lastRetryCount, lastGoodTransCount, adcValue=0;

    //read known values
    unitNum=(uint8_t)bytesRecv[0];
    lastRetryCount=(uint16_t)bytesRecv[2];
    lastGoodTransCount=(uint16_t)bytesRecv[4];
    memcpy(&vcc, &bytesRecv[6], 4);
    memcpy(&reading, &bytesRecv[10], 4);
    presence= bytesRecv[14];

    //read ADC value if sent
    if(bytesRecv[15]=='a')
    {
      //adcValue=(int16_t)bytesRecv[16];
      memcpy(&adcValue, &bytesRecv[16], 2);
      LogLine() << " --ADC: " << adcValue; 
    }
 
 	//Record number of retries if exist
    if(lastRetryCount > 0 || lastGoodTransCount>0)
    {
        TimeStamp(rfFile);
        fprintf(rfFile, "STATE: U: %d - R: %d, A: %d\n", unitNum, lastRetryCount, lastGoodTransCount);  //Small indication for log file
        fflush(rfFile);
    }

    api.AddState(unitNum,vcc,reading,presence,seconds_past_epoch);
    fprintf(logFile, "#");  //Small indication for log file
    fflush(logFile);
}

//Build post message for pipe 5
//
//Context pipe
//
void BuildAndSendContext()
{
    //
    // Over-the-air context packet definition/spec
    //
    // 1 byte:  unit number (uint8)
    // 1 byte:  Payload type (S=state, C=context, E=event  
    // n bytes:  description (char [])
    //

	time_t seconds_past_epoch = GetLocalEpoch();

    //read known values
    uint8_t unitNum = bytesRecv[0];

    if(lastPayloadLen>2)
    {
		try
		{
				size_t len=(size_t)(lastPayloadLen-2);
				char *buffer=(char *)(bytesRecv+2);
				std::string description(buffer,len);
				//LogLine() << "Adding Device: " << (int)unitNum << " - " << description;

				api.AddDevice((int)unitNum,description,seconds_past_epoch);
		}
		catch(const std::exception& e)
		{
			LogErrorLine() << "HTTP Error adding Device: " << e.what();
		}
    }

    fflush(stdout);
}




