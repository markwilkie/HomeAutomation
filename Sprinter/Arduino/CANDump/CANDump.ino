// CANBED DUAL TEST EXAMPLE
// CAN 0 Send, CAN 1 Recv

#include <Wire.h>
#include <stdio.h>
#include "canbed_dual.h"

CANBedDual CAN0(0);
CANBedDual CAN1(1);


void setup()
{
    Serial.begin(115200);
    
    Wire1.setSDA(6);
    Wire1.setSCL(7);
    Wire1.begin();
    
    CAN0.init(500000);          // CAN0 baudrate: 500kb/s
    CAN1.init(500000);          // CAN1 baudrate: 500kb/s
}

void loop()
{   
    unsigned long id = 0;
    int ext = 0;
    int rtr = 0;
    int fd = 0;
    int len = 0;
    
    unsigned char dtaGet[100];

    if(CAN1.read(&id, &ext, &rtr, &fd, &len, dtaGet))
    {
        Serial.print("0x");
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
            Serial.print("0x");
            Serial.print(dtaGet[i],HEX);
            if(i<len-1)
              Serial.print(",");
        }
        Serial.println();
    }
}

// ENDIF