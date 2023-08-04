#include <Wire.h>
#include <stdio.h>

#include "PID.h"
#include "TestData.h"
#include "isotp.h"

#define MIN_TIME_BETWEEN_REQUESTS  25
#define ALL_ONLINE_WAIT            10000;

//Can bus interfaces
//CANBedDual CAN0(0);
CANBedDual CAN1(1);
IsoTp isotp(&CAN1);

//timing 
unsigned long lastSend;
int slowArrayIdx = 0;

//Used for testing and simulation (actual dump)
TestData testData;
unsigned int testId;
unsigned char canTestFrame[30];

//Buffers for CAN bus commms
struct Message_t txMsg, rxMsg;

//Misc
PID pidsSupported(0x7DF,0x01,0x00,"PIDs Supported"," ","A",1000);

//Setup fast PIDs
PID engineLoad(0x7DF,0x01,0x04,"Load","%","A/2.55",200);
PID manPressure(0x7DF,0x01,0x0B,"Manifold","kPa","A",200);
PID speed(0x7DF,0x01,0x0D,"Speed","km/h","A",400); 
PID mafFlow(0x7DF,0x01,0x10,"MAF","g/s","((256*A)+B)/100",200); 

//Setup slow PIDs
PID coolantTemp(0x7DF,0x01,0x05,"Coolant Temp","C","A-40",10000);
PID intakeTemp(0x7DF,0x01,0x0F,"Intake Temp","C","A-40",1000);
PID fuelLevel(0x7DF,0x01,0x2F,"Fuel","%","(100/255)*A",60000);
PID transTemp(0x7E1,0x21,0x30,"Trans Temp","C","E-50",10000);
PID distanceTrav(0x7DF,0x01,0x31,"Distance Travelled","km","(256*A)+B",60000);
PID ambientTemp(0x7DF,0x01,0x46,"Ambient Temp","C","A-40",30000);
PID diagnostics(0x7DF,0x01,0x01,"Diag","","A",10000);

//Cycle through the entire fast PIDs for each slow PID
PID* slowPidArray[]={&coolantTemp,&intakeTemp,&fuelLevel,&transTemp,&distanceTrav,&ambientTemp,&diagnostics};
PID* fastPidArray[]={&engineLoad,&manPressure,&speed,&mafFlow};

const int fastArrLen = sizeof(fastPidArray) / sizeof(fastPidArray[0]);
const int slowArrLen = sizeof(slowPidArray) / sizeof(slowPidArray[0]);

/*
Add DTC support

Number of codes == 2  (remember to take off the on/off bit for CEL)
https://en.wikipedia.org/wiki/OBD-II_PIDs#Service_01_PID_01
09:56:31.907 -> 0x7DF,0x2,0x1,0x1,0xFF,0xFF,0xFF,0xFF,0xFF,
09:56:31.907 -> 0x7E8,0x6,0x41,0x1,-->0x2<--,0x7,0x81,0x0,0x0,

unsigned int b=0x02;
int cel = b>>7;
int numCodes = b & 0x7F;
Serial.println(cel);
Serial.println(numCodes);

Pending codes - service x07  (in this case the code is P0672, no CEL)
09:57:07.715 -> 0x7DF,0x1,0x7,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
09:57:07.715 -> 0x7E8,0x6,0x47,0x2,0x6,0x72,0x0,0x0,0x0,

Stored codes - service x03 (same code...P0672)
https://en.wikipedia.org/wiki/OBD-II_PIDs#Service_03_(no_PID_required)
09:57:27.965 -> 0x7DF,0x1,0x3,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
09:57:27.965 -> 0x7E8,0x6,0x43,0x2,0x6,0x72,0x0,0x0,0x0,

unsigned int b1=0x06;
unsigned int b2=0x72;
int code = b1>>6;
int d1 = (b1 & 0x3F) >> 4;
int d2 = b1 & 0x0F;
int d3 = b2 >> 4;
int d4 = b2 & 0x0F;

Serial.println("===============");
Serial.println(code);
Serial.print(d1);
Serial.print(d2);
Serial.print(d3);
Serial.println(d4);
*/

void setup()
{
    Serial.begin(115200);  //logging
    Serial1.begin(921600); //main arduino board

    //Setup pins
    pinMode(18, OUTPUT);   //LED
    pinMode(10, INPUT_PULLUP);  //pull low for simulation mode

    Serial.println("Waiting 10 seconds to give things a chance to settle");
    delay(10000);

    //Wait until we have the go-ahead from master before continuing  (blocks indefinitely)
    Serial.println("Waiting for the go-ahead from master....");
    while(Serial1.available())  //flush
      Serial.println(Serial1.read());
    while(!Serial1.available());
    Serial.println("Received go-ahead from master to start sending");

    //Setup I2C to CAN transceiver
    Wire1.setSDA(6);
    Wire1.setSCL(7);
    Wire1.begin();
  
    //CAN0.init(500000);          // CAN0 baudrate: 500kb/s
    CAN1.init(500000);          // CAN1 baudrate: 500kb/s

    //Setup buffers for CAN comm
    txMsg.Buffer = (uint8_t *)calloc(8, sizeof(uint8_t));
    rxMsg.Buffer = (uint8_t *)calloc(MAX_MSGBUF, sizeof(uint8_t));  

    Serial.println("Making sure we can talk to the ECU ok");
    if(digitalRead(10))  //make sure we're not in debug mode
      initECU();
      
    //Send quickly 30 times to get initialized ok
    for(int i=0;i<30;i++)
    {
      sendAllPids();
      delay(500);
    }

    Serial.println("...and we're off!");
}

//Make sure the ECU is online and happy
void initECU()
{
  int exp=2;
  long backoff=0;

  while(updatePID(&pidsSupported))
  {
    //Determine delay, starting w/ 1/2 second and then backing off exponetially until approx 4 minutes between tries
    backoff=250*pow(2,exp);

    //Send error
    sendToMaster(0,0,backoff/1000);
    Serial.print("No response from ECU.  Seconds Delay: ");
    Serial.println(backoff/1000);

    //Waiting now
    delay(backoff);

    //increase exponential backoff until we hit 10
    if(exp<10)
      exp++;
  } 

  //Give quick breather to ECU
  delay(1000);
}

//Send all PIDs to get things initialized
void sendAllPids()
{
      //Looping through all fast PIDs for each slow one
    for(int i=0;i<fastArrLen;i++)
    {
      //Set timing that that we're about to send
      fastPidArray[i]->setNextUpdateMillis();

      //Ask ECU for the update
      updatePID(fastPidArray[i]);

      //Make sure we don't overwhelm the ECU
      delay(MIN_TIME_BETWEEN_REQUESTS*2);
    }

    //Processing slow PIDs 
    for(int i=0;i<slowArrLen;i++)
    {
      //Set timing that that we're about to send
      slowPidArray[i]->setNextUpdateMillis();

      //Ask ECU for the update
      updatePID(slowPidArray[i]);

      //Make sure we don't overwhelm the ECU
      delay(MIN_TIME_BETWEEN_REQUESTS);
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

//Sends PID to master via serial
void sendToMaster(unsigned int service,unsigned int pid,unsigned int value)
{
  //buffer used for communication
  byte sendBuffer[15];

  //fill buffer with data
  memcpy(sendBuffer,&service,2);
  memcpy(sendBuffer+2,&pid,2);
  memcpy(sendBuffer+4,&value,2);

  //crc
  unsigned int crc=checksumCalculator(sendBuffer,6);
  memcpy(sendBuffer+6,&crc,2);

  //send it off
  Serial1.write('[');
  Serial1.write(sendBuffer,8);
  Serial1.write(']');
}

void loop()
{
    //Blink light
    digitalWrite(18,HIGH);
    delay(2);
    digitalWrite(18,LOW);

    //Check for simulator mode
    if(!digitalRead(10))
    {
      simulatorMode();
    }

    //Looping through all fast PIDs for each slow one
    for(int i=0;i<fastArrLen;i++)
    {
      //Is it time to send this PID?     
      if(millis()>fastPidArray[i]->getNextUpdateMillis())
      {
        //Set timing that that we're about to send
        fastPidArray[i]->setNextUpdateMillis();

        //Ask ECU for the update
        updatePID(fastPidArray[i]);
      }
    }

    //Processing one slow PID for each pass through the fast loop

    //Is it time to send this PID?     
    if(millis()>slowPidArray[slowArrayIdx]->getNextUpdateMillis())
    {
      //Set timing that that we're about to send
      slowPidArray[slowArrayIdx]->setNextUpdateMillis();

      //Ask ECU for the update
      updatePID(slowPidArray[slowArrayIdx]);
    }

    //Start at the beginning again
    slowArrayIdx++;
    if(slowArrayIdx>=slowArrLen)
      slowArrayIdx=0;

}

int updatePID(PID *pid)
{
    //make sure CAN com buffers are cleared
    memset(txMsg.Buffer, (uint8_t)0, 8);
    memset(rxMsg.Buffer, (uint8_t)0, MAX_MSGBUF);    

    //Let's go through each PID we setup and get the values
    int retVal=0;

    //be nice by delaying at least min_time before sending
    long timeSinceLast=millis()-lastSend;   
    if(timeSinceLast<MIN_TIME_BETWEEN_REQUESTS)
    {
      delay(MIN_TIME_BETWEEN_REQUESTS-timeSinceLast);
    }
    lastSend=millis();    

    //Build request for ECU's and then send it!
    txMsg.len = 2;  //stnd size
    txMsg.tx_id = pid->getId();
    txMsg.rx_id = pid->getRxId();
    txMsg.Buffer[0]=pid->getService();
    txMsg.Buffer[1]=pid->getPID();
    Serial.print(millis());
    Serial.print(": Sending ");
    Serial.println(pid->getLabel());
    retVal=isotp.send(&txMsg); 

    if(retVal)
    {
      Serial.println("ERROR sending");
      return retVal;
    }
    else
    {
      //OK, now receive the data
      rxMsg.tx_id = pid->getId();
      rxMsg.rx_id = pid->getRxId();
      Serial.print(F("Receiving "));
      Serial.println(pid->getLabel());
      retVal=isotp.receive(&rxMsg);
    }

    //If we successfully received the data, let's parse and send it back now
    if(retVal)
    {
      Serial.println("ERROR receving");
      return retVal;
    }
    else
    {
      isotp.print_buffer(rxMsg.rx_id, rxMsg.Buffer, rxMsg.len);        

      //OK, now get the result from the buffer we just received
      unsigned int result=(int)pid->getResult(rxMsg.Buffer);
      
      //1 = service and 2= pid
      sendToMaster(rxMsg.Buffer[0],rxMsg.Buffer[1],result);
      Serial.printf("Service/Pid: 0x%02x 0x%02x -  %s: %d%s\n",rxMsg.Buffer[0],rxMsg.Buffer[1],pid->getLabel(),result,pid->getUnit());
    }

    return 0;   
}

void simulatorMode()
{
  //Loop while in simulation mode
  while(!digitalRead(10))
  {
      testId=testData.GetId();
      testData.FillCanFrame(canTestFrame);
      delay(10);

      simulatorMatch(fastPidArray,fastArrLen);
      simulatorMatch(slowPidArray,slowArrLen);

      testData.NextRow(); 
  }
}

void simulatorMatch(PID* pidArray[],int arrLen)
{
    for(int i=0;i<arrLen && testId>0;i++)
    {
      if(pidArray[i]->isMatch(testId,canTestFrame))
      {
        unsigned int result=result=(int)pidArray[i]->getResult(canTestFrame);
        //1 = service and 2= pid

        sendToMaster(canTestFrame[0],canTestFrame[1],result);
        Serial.printf("Service/Pid: 0x%02x 0x%02x -  %s: %d%s\n",canTestFrame[0],canTestFrame[1],pidArray[i]->getLabel(),result,pidArray[i]->getUnit());        
      }
    }
}

// ENDIF
