// CANBED DUAL TEST EXAMPLE
// CAN 0 Send, CAN 1 Recv

#include <Wire.h>
#include <stdio.h>

#include "PID.h"
#include "TestData.h"
#include "canbed_dual.h"

CANBedDual CAN0(0);
CANBedDual CAN1(1);

TestData testData;

//Setup PIDs
PID engineLoad(0x7DF,0x01,0x04,"Load","%","A/2.55");
PID coolantTemp(0x7DF,0x01,0x05,"Coolant Temp","C","A-40");
PID manPressure(0x7DF,0x01,0x0B,"Manifold","kPa","A");
PID engineRPM(0x7DF,0x01,0x0C,"RPM","RPM","(256*A+B)/4");
PID speed(0x7DF,0x01,0x0D,"Speed","km/h","A");
PID mafFlow(0x7DF,0x01,0x10,"MAF","g/s","(256*A+B)/100");
PID fuelLevel(0x7DF,0x01,0x2F,"Fuel","%","(100/255)*A");
PID baraPressure(0x7DF,0x01,0x33,"Barameter","kPa","A");
PID transTemp(0x7E1,0x22,0x30,"Trans Temp","C","A-50");



void setup()
{
    Serial.begin(115200);  //logging
    Serial1.begin(115200); //main arduino board

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

void writeToMaster(char data)
{
  //if(Serial1.availableForWrite(sizeof(data)))
  //{
    Serial1.write(data);
  //}
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
        //Turn light on
        delay(10);
        digitalWrite(18,HIGH);
        delay(5);
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

        //Serial.printf("ID: 0x%04x - 0x%02x 0x%02x    0x%02x \n",testId,service,pid,data1);

        //check pids
        if(engineRPM.isMatch(testId,service,pid))
        {
          double result=engineRPM.getResult(a,b,0,0,0);
          Serial.printf("Val: %d  %s: %f%s\n",a,engineRPM.getLabel(),result,engineRPM.getUnit());
        }
        if(engineLoad.isMatch(testId,service,pid))
        {
          double result=engineLoad.getResult(a,0,0,0,0);
          Serial.printf("Val: %d  %s: %f%s\n",a,engineLoad.getLabel(),result,engineLoad.getUnit());
        }
        if(coolantTemp.isMatch(testId,service,pid))
        {
          double result=coolantTemp.getResult(a,0,0,0,0);
          Serial.printf("Val: %d  %s: %f%s\n",a,coolantTemp.getLabel(),result,coolantTemp.getUnit());
        }
        if(fuelLevel.isMatch(testId,service,pid))
        {
          double result=fuelLevel.getResult(a,0,0,0,0);
          Serial.printf("Val: %d  %s: %f%s\n",a,fuelLevel.getLabel(),result,fuelLevel.getUnit());
        }
        if(manPressure.isMatch(testId,service,pid))
        {
          double result=manPressure.getResult(a,0,0,0,0);
          Serial.printf("Val: %d  %s: %f%s\n",a,manPressure.getLabel(),result,manPressure.getUnit());
        }
        if(mafFlow.isMatch(testId,service,pid))
        {
          double result=mafFlow.getResult(a,b,0,0,0);
          Serial.printf("Val: %d  %s: %f%s\n",a,mafFlow.getLabel(),result,mafFlow.getUnit());
        }
        if(speed.isMatch(testId,service,pid))
        {
          double result=speed.getResult(a,0,0,0,0);
          Serial.printf("Val: %d  %s: %f%s\n",a,speed.getLabel(),result,speed.getUnit());
        }
        if(baraPressure.isMatch(testId,service,pid))
        {
          double result=baraPressure.getResult(a,0,0,0,0);
          Serial.printf("Val: %d  %s: %f%s\n",a,baraPressure.getLabel(),result,baraPressure.getUnit());
        }
        if(transTemp.isMatch(testId,testData.GetData(0))) 
        {
          double result=transTemp.getResult(testData.GetData(1),0,0,0,0);
          Serial.printf("====================>  Val: %d  %s: %f%s\n",a,transTemp.getLabel(),result,transTemp.getUnit());
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

        //Send data
        writeToMaster('2');
        //Serial.print("Sent: ");
        //Serial.println('2');
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
