#include <Wire.h>
#include <stdio.h>

#include "PID.h"
#include "TestData.h"
#include "isotp.h"

//Can bus interfaces
//CANBedDual CAN0(0);
CANBedDual CAN1(1);
IsoTp isotp(&CAN1);

//Used for testing and simulation (actual dump)
TestData testData;

//Buffers for CAN bus commms
struct Message_t txMsg, rxMsg;

//Setup PIDs
PID engineLoad(0x7DF,0x01,0x04,"Load","%","A/2.55",200);
PID coolantTemp(0x7DF,0x01,0x05,"Coolant Temp","C","A-40",10000);
PID manPressure(0x7DF,0x01,0x0B,"Manifold","kPa","A",200);
PID engineRPM(0x7DF,0x01,0x0C,"RPM","RPM","((256*A)+B)/4",500);  
PID speed(0x7DF,0x01,0x0D,"Speed","km/h","A",500); 
PID intakeTemp(0x7DF,0x01,0x0F,"Intake Temp","C","A-40",1000);
PID mafFlow(0x7DF,0x01,0x10,"MAF","g/s","((256*A)+B)/100",200); 
PID runtime(0x7DF,0x01,0x1F,"Runtime","seconds","(256*A)+B",1000);
PID fuelLevel(0x7DF,0x01,0x2F,"Fuel","%","(100/255)*A",60000);
PID transTemp(0x7E1,0x21,0x30,"Trans Temp","C","E-50",10000);
PID distanceTrav(0x7DF,0x01,0x31,"Distance Travelled","km","(256*A)+B",60000);
PID baraPressure(0x7DF,0x01,0x33,"Barameter","kPa","A",1000);
PID ambientTemp(0x7DF,0x01,0x46,"Ambient Temp","C","A-40",30000);
PID* pidArray[]={&engineLoad,&coolantTemp,&manPressure,&engineRPM,&speed,&intakeTemp,&mafFlow,&runtime,&fuelLevel,&distanceTrav,&baraPressure,&transTemp,&ambientTemp};

void setup()
{
    Serial.begin(115200);  //logging
    Serial1.begin(921600); //main arduino board

    //Setup pins
    pinMode(18, OUTPUT);   //LED
    pinMode(10, INPUT_PULLUP);  //pull low for simulation mode

    delay(10000);

    //Setup I2C to CAN transceiver
    Wire1.setSDA(6);
    Wire1.setSCL(7);
    Wire1.begin();
  
    //CAN0.init(500000);          // CAN0 baudrate: 500kb/s
    CAN1.init(500000);          // CAN1 baudrate: 500kb/s

    //Setup buffers for CAN comm
    txMsg.Buffer = (uint8_t *)calloc(8, sizeof(uint8_t));
    rxMsg.Buffer = (uint8_t *)calloc(MAX_MSGBUF, sizeof(uint8_t));    
}

//not yet used....  
char readFromMaster()
{
  char retVal='0';
  
  if (Serial1.available())
  {
    retVal=Serial1.read();
    Serial.print(retVal);
    Serial.println();
  }

  return retVal;
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

    //make sure CAN com buffers are cleared
    memset(txMsg.Buffer, (uint8_t)0, 8);
    memset(rxMsg.Buffer, (uint8_t)0, MAX_MSGBUF);    

    //Let's go through each PID we setup and get the values
    int retVal=0;
    const int arrLen = sizeof(pidArray) / sizeof(pidArray[0]);
    for(int i=0;i<arrLen;i++)
    {
      //Is it time??
      Serial.print("Next PID / Current Time: ");
      Serial.print(pidArray[i]->getLabel());
      Serial.print("-");
      Serial.print(pidArray[i]->getNextUpdateMillis());
      Serial.print("/");
      Serial.println(millis());

      //be nice
      delay(200);
      
      if(millis()>pidArray[i]->getNextUpdateMillis())
      {
        pidArray[i]->setNextUpdateMillis();

        //Build request for ECU's and then send it!
        txMsg.len = 2;  //stnd size
        txMsg.tx_id = pidArray[i]->getId();
        txMsg.rx_id = pidArray[i]->getRxId();
        txMsg.Buffer[0]=pidArray[i]->getService();
        txMsg.Buffer[1]=pidArray[i]->getPID();
        Serial.print("Sending ");
        Serial.println(pidArray[i]->getLabel());
        retVal=isotp.send(&txMsg); 

        if(retVal)
        {
          Serial.println("ERROR sending");
        }
        else
        {
          //OK, now receive the data
          rxMsg.tx_id = pidArray[i]->getId();
          rxMsg.rx_id = pidArray[i]->getRxId();
          Serial.print(F("Receiving "));
          Serial.println(pidArray[i]->getLabel());
          retVal=isotp.receive(&rxMsg);
        }

        //If we successfully received the data, let's parse and send it back now
        if(retVal)
        {
          Serial.println("ERROR receving");
        }
        else
        {
          isotp.print_buffer(rxMsg.rx_id, rxMsg.Buffer, rxMsg.len);        

          //OK, now get the result from the buffer we just received
          unsigned int result=(int)pidArray[i]->getResult(rxMsg.Buffer);
          
          //1 = service and 2= pid
          sendToMaster(rxMsg.Buffer[0],rxMsg.Buffer[1],result);
          Serial.printf("Service/Pid: 0x%02x 0x%02x -  %s: %d%s\n",rxMsg.Buffer[0],rxMsg.Buffer[1],pidArray[i]->getLabel(),result,pidArray[i]->getUnit());
        }
      }
    }     
}

// ENDIF
