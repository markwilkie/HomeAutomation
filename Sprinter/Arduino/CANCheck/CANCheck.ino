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
static IsoTpLink g_link;

/* Alloc send and receive buffer statically in RAM */
static uint8_t g_isotpRecvBuf[8];
static uint8_t g_isotpSendBuf[8];


//Flags
bool load=false;
bool coolant=false;
bool manPressure=false;
bool rpm=false;
bool speed=false;
bool intake=false;
bool maf=false;
bool runtime=false;
bool fuel=false;
bool transTemp=false;
bool distance=false;
bool bara=false;
bool ambientTemp=false;

bool newUpdate=false;
bool sentFlag=false;

unsigned char recData[10];
unsigned char sndData[10];


void setup()
{
    Serial.begin(115200);
    Serial.println("Let's begin!");
    
    Wire1.setSDA(6);
    Wire1.setSCL(7);
    Wire1.begin();
    
    //CAN0.init(500000);          // CAN0 baudrate: 500kb/s
    CAN1.init(500000);          // CAN1 baudrate: 500kb/s

    /* Initialize link, 0x7DF is the CAN ID you send with */
    isotp_init_link(&g_link, 0x7DF,	g_isotpSendBuf, sizeof(g_isotpSendBuf), g_isotpRecvBuf, sizeof(g_isotpRecvBuf));
}

/* required, this must send a single CAN message with the given arbitration
    * ID (i.e. the CAN message ID) and data. The size will never be more than 8
    * bytes. */
int  isotp_user_send_can(const uint32_t arbitration_id, const uint8_t* data, const uint8_t size) 
{
    //void CANBedDual::send(unsigned long id, unsigned char ext, unsigned char rtr, unsigned char fd, unsigned char len, unsigned char *dta)    
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

    int ret    ;


    //Only send if we've already sent, and are still waiting for an update
    if(!sentFlag && !newUpdate)
    { 
      //Be very nice to the bus
      delay(2000);

      sndData[0]=0x02;  //length
      sndData[1]=0x01;  //service
      sndData[2]=0x05;  //coolant

      Serial.println("sending request");
      
      /* In case you want to send data w/ functional addressing, use isotp_send_with_id */
      ret = isotp_send_with_id(&g_link, 0x7df, sndData, sizeof(sndData));
      if (ISOTP_RET_OK == ret) 
      {
          sentFlag=true;
      } 
      else 
      {
          Serial.println("Problem sending request");
      }
    }

    if(CAN1.read(&id, &ext, &rtr, &fd, &len, recData))
    {
        Serial.print("Received code: ");      
        Serial.println(recData[2]);      
        
        if(recData[2]==0x05 && !coolant)
        {
            isotp_on_can_message(&g_link, recData, len);
            coolant=true;
            newUpdate=true;

            Serial.println("Received Coolant!");

            /* You can receive message with isotp_receive.
            payload is upper layer message buffer, usually UDS;
            payload_size is payload buffer size;
            out_size is the actuall read size;
            */
            uint16_t out_size;
            ret = isotp_receive(&g_link, recData, sizeof(recData), &out_size);
            if (ISOTP_RET_OK == ret) 
            {
                Serial.print("Lib receive size: ");
                Serial.println(out_size);
                Serial.print("Fancy Received code: ");      
                Serial.println(recData[2]);                   
            }                  
        }  

        
        //if(dtaGet[2]==0x30 && !transTemp){
        //    transTemp=true;
        //    newUpdate=true;
        //}         
    }

    if(newUpdate || !newUpdate)
    {
        //reset flags so we'll send again
        newUpdate=false;
        sentFlag=false;
        
        //Serial.print("load"); Serial.print(load); 
        Serial.print("| coolant:"); Serial.print(coolant);
        //Serial.print(" | manPressure:"); Serial.print(manPressure);
        //Serial.print(" | rpm:"); Serial.print(rpm);
        //Serial.print(" | speed:"); Serial.print(speed);
        //Serial.print(" | intake:"); Serial.print(intake);
        //Serial.print(" | maf:"); Serial.print(maf);
        //Serial.print(" | runtime:"); Serial.print(runtime);
        //Serial.print(" | fuel:"); Serial.print(fuel);
        //Serial.print(" | transTemp:"); Serial.print(transTemp);
        //Serial.print(" | distance:"); Serial.print(distance);
        //Serial.print(" | bara:"); Serial.print(bara);
        //Serial.print(" | ambientTemp:"); Serial.print(ambientTemp);
        Serial.println("");
    }
}

/*
      //Serial.println(dtaGet[2],HEX);
        if(dtaGet[2]==0x04 && !load){
            load=true;
            newUpdate=true;
        }
        if(dtaGet[2]==0x05 && !coolant){
            coolant=true;
            newUpdate=true;
        }        
        if(dtaGet[2]==0x0B && !manPressure){
            manPressure=true;
            newUpdate=true;
        }  
        if(dtaGet[2]==0x0C && !rpm){
            rpm=true;
            newUpdate=true;
        } 
        if(dtaGet[2]==0x0D && !speed){
            speed=true;
            newUpdate=true;
        } 
        if(dtaGet[2]==0x0F && !intake){
            intake=true;
            newUpdate=true;
        } 
        if(dtaGet[2]==0x10 && !maf){
            maf=true;
            newUpdate=true;
        } 
        if(dtaGet[2]==0x1F && !runtime){
            runtime=true;
            newUpdate=true;
        } 
        if(dtaGet[2]==0x2F && !fuel){
            fuel=true;
            newUpdate=true;
        } 
        if(dtaGet[2]==0x30 && !transTemp){
            transTemp=true;
            newUpdate=true;
        } 
        if(dtaGet[2]==0x31 && !distance){
            distance=true;
            newUpdate=true;
        } 
        if(dtaGet[2]==0x33 && !bara){
            bara=true;
            newUpdate=true;
        } 
        if(dtaGet[2]==0x70 && !ambientTemp){
            ambientTemp=true;
            newUpdate=true;
        } */


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
