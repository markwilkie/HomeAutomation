// CANBED DUAL TEST EXAMPLE
// CAN 0 Send, CAN 1 Recv

#include <Wire.h>
#include <stdio.h>

#include "canbed_dual.h"

#include "isotp.h"


//Canbed specifics
//CANBedDual CAN0(0);
CANBedDual CAN1(1);

//ISP TP implementation (mostly for multi frame support)
/* Alloc IsoTpLink statically in RAM */

static IsoTpLink g_coolantlink;
static IsoTpLink g_translink;

/* Alloc send and receive buffer statically in RAM */
static uint8_t g_isotpCoolantRecvBuf[20];
static uint8_t g_isotpCoolantSendBuf[20];

static uint8_t g_isotpTransRecvBuf[512];
static uint8_t g_isotpTransSendBuf[20];

unsigned char recData[512];
unsigned char sndData[100];

long coolantSendTime;
long transSendTime;
bool coolantDone;
bool transDone;


void setup()
{
    Serial.begin(115200);
    Serial.println("Let's begin!");
    
    Wire1.setSDA(6);
    Wire1.setSCL(7);
    Wire1.begin();
    
    //CAN0.init(500000);          // CAN0 baudrate: 500kb/s
    CAN1.init(500000);          // CAN1 baudrate: 500kb/s

    delay(10000);

    /* Initialize link, 0x7DF is the CAN ID you send with */
    isotp_init_link(&g_coolantlink, 0x7DF,	g_isotpCoolantSendBuf, sizeof(g_isotpCoolantSendBuf), g_isotpCoolantRecvBuf, sizeof(g_isotpCoolantRecvBuf));
    isotp_init_link(&g_translink, 0x7E1,  g_isotpTransSendBuf, sizeof(g_isotpTransSendBuf), g_isotpTransRecvBuf, sizeof(g_isotpTransRecvBuf));
    
    coolantSendTime=millis();
    transSendTime=millis();    

    coolantDone=false;
    transDone=false;    
}

/* required, this must send a single CAN message with the given arbitration
    * ID (i.e. the CAN message ID) and data. The size will never be more than 8
    * bytes. */
int  isotp_user_send_can(const uint32_t arbitration_id, const uint8_t* data, const uint8_t size) 
{
    //void CANBedDual::send(unsigned long id, unsigned char ext, unsigned char rtr, unsigned char fd, unsigned char len, unsigned char *dta)    

    Serial.print("0x");
    Serial.print(arbitration_id,HEX);
    Serial.print(",");
    for(int i=0; i<size; i++)
    {
        Serial.print("0x");
        Serial.print(data[i],HEX);
        Serial.print(",");
    }
    Serial.println();

    
    CAN1.send(arbitration_id, 0, 0, 0, size, (unsigned char*)data);
    return 0;
}

/* required, return system tick, unit is millisecond */
uint32_t isotp_user_get_ms(void) 
{
    return millis();
}

/* optional, provide to receive debugging log messages */
void isotp_user_debug(const char* message, ...) 
{
    Serial.println(message);
}

void loop()
{   
    unsigned long id = 0;
    int ext = 0;
    int rtr = 0;
    int fd = 0;
    int len = 0;
    int ret;
    uint16_t out_size;

    //Read raw bytes off the CAN bus
    if(CAN1.read(&id, &ext, &rtr, &fd, &len, recData))
    {
      Serial.print("Received CAN ID: 0x");      
      Serial.println(id,HEX);  

      //Most everything
      if(id == 0x7E8)
      {
        Serial.println("Received coolant!");
        isotp_on_can_message(&g_coolantlink, recData, len);
      }  

      //Transmission temp
      if(id == 0x7E9)
      {
        Serial.println("Received trans temp!");
        dump(recData,len);
        isotp_on_can_message(&g_translink, recData, len);
      }          
    }

    //Links for multi frame and general house keeping  (might only need one for multi frame)
    isotp_poll(&g_coolantlink);  
    isotp_poll(&g_translink);


    if(!coolantDone)
    {
      //Receiving now
      ret = isotp_receive(&g_coolantlink, recData, sizeof(recData), &out_size);  
      if (ret == ISOTP_RET_OK) 
      {
          Serial.println("Coolant Data: ");
          dump(recData,out_size);
          coolantDone=true;   
      }
      else
      {
        //Serial.print("Coolant Data Recv Error: ");
        //Serial.println(ret);
      }
    }

    if(!transDone)
    {                  
      ret = isotp_receive(&g_translink, recData, sizeof(recData), &out_size);   
      if (ret == ISOTP_RET_OK) 
      {
          Serial.println("Trans Temp Data: ");
          dump(recData,out_size);
          transDone=true; 
      }
      else
      {
        //Serial.print("Trans Temp Data Recv Error: ");
        //Serial.println(ret);
      }
    }

    if(millis()>coolantSendTime)
    {
     //send coolant request
      //Serial.println("sending coolant request");
      sndData[0]=0x01;  //service
      sndData[1]=0x05;  //coolant    
      ret = isotp_send(&g_coolantlink, sndData, 2);
      if (ISOTP_RET_OK == ret) 
      {
          Serial.println("Sent coolant request");
          coolantDone=false;
      } 
      else 
      {
          Serial.print("Coolant request not sent: ");
          Serial.println(ret);
      }

      coolantSendTime=millis()+10000;
    }

    if(millis()>transSendTime)
    {
      //send trans request
      //Serial.println("sending trans temp request");
      sndData[0]=0x21;  //service
      sndData[1]=0x30;  //trans temp    
      ret = isotp_send(&g_translink, sndData, 2);
      if (ISOTP_RET_OK == ret) 
      {
          Serial.println("Sent trans temp request");
          transDone=false;
      } 
      else 
      {
          Serial.print("Trans temp request not sent: ");
          Serial.println(ret);
      }

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
