// CANBED DUAL TEST EXAMPLE
// CAN 0 Send, CAN 1 Recv

#include <Wire.h>
#include <stdio.h>

#include "PID.h"
#include "TestData.h"
#include "canbed_dual.h"

//Can bus interfaces
CANBedDual CAN0(0);
CANBedDual CAN1(1);

//Used for testing and simulation (actual dump)
TestData testData;

//Setup PIDs
PID engineLoad(0x7DF,0x01,0x04,"Load","%","A/2.55");
PID coolantTemp(0x7DF,0x01,0x05,"Coolant Temp","C","A-40");
PID manPressure(0x7DF,0x01,0x0B,"Manifold","kPa","A");
PID engineRPM(0x7DF,0x01,0x0C,"RPM","RPM","(256*A+B)/4");
PID speed(0x7DF,0x01,0x0D,"Speed","km/h","A");
PID intakeTemp(0x7DF,0x01,0x0F,"Intake Temp","C","A-40");
PID mafFlow(0x7DF,0x01,0x10,"MAF","g/s","(256*A+B)/100");
PID runtime(0x7DF,0x01,0x1F,"Runtime","seconds","A");
PID fuelLevel(0x7DF,0x01,0x2F,"Fuel","%","(100/255)*A");
PID transTemp(0x7E1,0x22,"Trans Temp","C","A-50");
PID distanceTrav(0x7DF,0x01,0x31,"Distance Travelled","km","A");
PID baraPressure(0x7DF,0x01,0x33,"Barameter","kPa","A");
PID ambientTemp(0x7DF,0x01,0x70,"Ambient Temp","C","A-40");
PID* pidArray[]={&engineLoad,&coolantTemp,&manPressure,&engineRPM,&speed,&intakeTemp,&mafFlow,&runtime,&fuelLevel,&distanceTrav,&baraPressure,&transTemp,&ambientTemp};

void setup()
{
    Serial.begin(115200);  //logging
    Serial1.begin(57600); //main arduino board

    //Setup pins
    pinMode(18, OUTPUT);   //LED
    pinMode(10, INPUT_PULLUP);  //pull low for simulation mode

    //Setup I2C to CAN transceiver
    Wire1.setSDA(6);
    Wire1.setSCL(7);
    Wire1.begin();
  
    CAN0.init(500000);          // CAN0 baudrate: 500kb/s
    CAN1.init(500000);          // CAN1 baudrate: 500kb/s
}

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

void loop()
{
    //Check if simulation mode
    if(digitalRead(10))
    {
        //sendData();
        readData();
    }
    else
    {
        //Blink light
        digitalWrite(18,HIGH);
        delay(2);
        digitalWrite(18,LOW);

        //testdata
        unsigned long testId=testData.GetId();
        unsigned int service=testData.GetData(1);  // 0 is the length
        unsigned int pid=testData.GetData(2);
        unsigned int a=testData.GetData(3);
        unsigned int b=testData.GetData(4);
        unsigned int c=testData.GetData(5);
        unsigned int d=testData.GetData(6);
        unsigned int e=testData.GetData(7);

        //buffer used for communication
        byte sendBuffer[15];

        //Serial.printf("ID: 0x%04x - 0x%02x 0x%02x    0x%02x \n",testId,service,pid,data1);

        //Dump pids we read
        const int arrLen = sizeof(pidArray) / sizeof(pidArray[0]);
        for(int i=0;i<arrLen;i++)
        {
          if(pidArray[i]->isMatch(testId,service,pid,testData.GetData(0)))
          {
            int result=0;
            
            //If extended data mode, then we don't have a pid, and we grab the data from the 2nd field and on
            //  This implimentation is not robust, but should work if this continues to be the only gauge
            if(pidArray[i]->isExtData())
            {
              result=(int)pidArray[i]->getResult(testData.GetData(1),testData.GetData(2),testData.GetData(3),testData.GetData(4),testData.GetData(5));

              int svc=testData.GetData(0);
              memcpy(sendBuffer,&svc,2);
              memcpy(sendBuffer+2,&svc,2);
              memcpy(sendBuffer+4,&result,2);
            }
            else
            {
              result=(int)pidArray[i]->getResult(a,b,c,d,e);
              memcpy(sendBuffer,&service,2);
              memcpy(sendBuffer+2,&pid,2);
              memcpy(sendBuffer+4,&result,2);
            }

            //crc
            int crc=checksumCalculator(sendBuffer,6);
            memcpy(sendBuffer+6,&crc,2);

            Serial1.write('[');
            Serial1.write(sendBuffer,10);
            Serial1.write(']');

            //sprintf(sendBuffer,"[0x%04x,0x%02x,%f,%s]\0",pidArray[i]->getId(),pid,result,pidArray[i]->getUnit());
            //Serial.print(sendBuffer);
            //writeToMaster(sendBuffer);
            Serial.printf("Service/Pid: 0x%02x 0x%02x -  %s: %d%s\n",service,pid,pidArray[i]->getLabel(),result,pidArray[i]->getUnit());
          }          
        }

        //Go to next row from test data
        testData.NextRow();        

        //read data
        char data=readFromMaster();
        if(data != '0')
        {
          Serial.print("Read: ");
          Serial.println(data);
        }
    }
}

void readData()
{
    unsigned long id = 0;
    int ext = 0;
    int rtr = 0;
    int fd = 0;
    int len = 0;
    
    unsigned char dtaGet[100];

    if(CAN1.read(&id, &ext, &rtr, &fd, &len, dtaGet))
    {
        Serial.print(id,HEX);
        Serial.print(",");
        //Serial.print("ext = ");
        //Serial.println(ext);
        //Serial.print("rtr = ");
        //Serial.println(rtr);
        //Serial.print("fd = ");
        //Serial.println(fd);
        //Serial.print("len = ");
        //Serial.println(len);

        for(int i=0; i<len; i++)
        {
            Serial.print(dtaGet[i],HEX);
            if(i<len-1)
              Serial.print(" ");
        }
        Serial.println();
    }
}

void sendData()
{
    static unsigned int cnt = 0;
    cnt++;
    if(cnt > 99)cnt = 0;
    unsigned char str[8];
    for(int i=0; i<8; i++)str[i] = cnt;
    CAN0.send(0x01, 0, 0, 0, 8, str);
}

// ENDIF
