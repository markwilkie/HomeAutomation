// CANBED DUAL TEST EXAMPLE
// CAN 0 Send, CAN 1 Recv

#include <Wire.h>
#include <stdio.h>

#include "isotp.h"

//Canbed specifics
//CANBedDual CAN0(0);
CANBedDual CAN1(1);

IsoTp isotp(&CAN1);

struct Message_t txMsg, rxMsg;

uint8_t coolantReq[] = { 0x01, 0x05 };  //coolant temp
uint8_t transTempReq[] = { 0x21, 0x30 };  //transmission temp  (multiframe response)

uint32_t broadcast_tx_can_id = 0x7DF;
uint32_t broadcast_rx_can_id = 0x7E8;

uint32_t trans_tx_can_id = 0x7E1;
uint32_t trans_rx_can_id = 0x7E9;

long coolantSendTime;
long transSendTime;

void setup()
{
    Serial.begin(115200);
    Serial.println("Let's begin!");
    
    Wire1.setSDA(6);
    Wire1.setSCL(7);
    Wire1.begin();
    
    //CAN0.init(500000);          // CAN0 baudrate: 500kb/s
    CAN1.init(500000);          // CAN1 baudrate: 500kb/s

    // buffers
    txMsg.Buffer = (uint8_t *)calloc(8, sizeof(uint8_t));
    rxMsg.Buffer = (uint8_t *)calloc(MAX_MSGBUF, sizeof(uint8_t));    

    coolantSendTime=millis();
    transSendTime=millis();      

    delay(10000); 
}

void loop()
{   
    //make sure buffers are cleared
    memset(txMsg.Buffer, (uint8_t)0, 8);
    memset(rxMsg.Buffer, (uint8_t)0, MAX_MSGBUF);

    if(millis()>coolantSendTime)
    {
     //send coolant request
      txMsg.len = sizeof(coolantReq);
      txMsg.tx_id = broadcast_tx_can_id;
      txMsg.rx_id = broadcast_rx_can_id;
      memcpy(txMsg.Buffer,coolantReq,sizeof(coolantReq));
      Serial.println(F("Sending Coolant..."));
      isotp.send(&txMsg);

      //Read Coolant
      rxMsg.tx_id = broadcast_tx_can_id;
      rxMsg.rx_id = broadcast_rx_can_id;
      Serial.println(F("Receiving coolant..."));
      isotp.receive(&rxMsg);
      isotp.print_buffer(rxMsg.rx_id, rxMsg.Buffer, rxMsg.len);       

      coolantSendTime=millis()+10000;
    }

    if(millis()>transSendTime)
    {
     //send trans request
      txMsg.len = sizeof(transTempReq);
      txMsg.tx_id = trans_tx_can_id;
      txMsg.rx_id = trans_rx_can_id;
      memcpy(txMsg.Buffer,transTempReq,sizeof(transTempReq));
      Serial.println(F("Sending Trans Temp..."));
      isotp.send(&txMsg);

      //Read trans temp
      rxMsg.tx_id = trans_tx_can_id;
      rxMsg.rx_id = trans_rx_can_id;
      Serial.println(F("Receiving trans temp..."));
      isotp.receive(&rxMsg);
      isotp.print_buffer(rxMsg.rx_id, rxMsg.Buffer, rxMsg.len);         

      transSendTime=millis()+5000;      
    }
}

void dump(unsigned char *data,int len)
{
    Serial.print("Bytes received size: ");
    Serial.println(len);  
    Serial.print("DUMP: ");
    for(int i=0; i<len; i++)
    {
        Serial.print("0x");
        Serial.print(data[i],HEX);
        Serial.print(",");
    }
    Serial.println();
}


/*
PID engineLoad(0x7DF,0x01,0x04,"Load","%","A/2.55");
PID coolantTemp(0x7DF,0x01,0x05,"Coolant Temp","C","A-40");
PID manPressure(0x7DF,0x01,0x0B,"Manifold","kPa","A");
PID engineRPM(0x7DF,0x01,0x0C,"RPM","RPM","((256*A)+B)/4");  
PID speed(0x7DF,0x01,0x0D,"Speed","km/h","A"); 
PID intakeTemp(0x7DF,0x01,0x0F,"Intake Temp","C","A-40");
PID mafFlow(0x7DF,0x01,0x10,"MAF","g/s","((256*A)+B)/100"); 
PID runtime(0x7DF,0x01,0x1F,"Runtime","seconds","(256*A)+B");
PID fuelLevel(0x7DF,0x01,0x2F,"Fuel","%","100/(255*A)");
PID transTemp(0x7E1,0x22,0x30,"Trans Temp","C","A-50",true);
PID distanceTrav(0x7DF,0x01,0x31,"Distance Travelled","km","(256*A)+B");
PID baraPressure(0x7DF,0x01,0x33,"Barameter","kPa","A");
PID ambientTemp(0x7DF,0x01,0x70,"Ambient Temp","C","A-40");
*/

// ENDIF
