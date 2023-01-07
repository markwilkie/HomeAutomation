// CANBED DUAL TEST EXAMPLE
// CAN 0 Send, CAN 1 Recv

#include <Wire.h>
#include <stdio.h>
#include "canbed_dual.h"

CANBedDual CAN0(0);
CANBedDual CAN1(1);

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


void setup()
{
    Serial.begin(115200);
    Serial.println("Let's begin!");
    
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
        } 
    }

    if(newUpdate || !newUpdate)
    {
        newUpdate=false;
        Serial.print("load"); Serial.print(load); 
        Serial.print("| coolant:"); Serial.print(coolant);
        Serial.print(" | manPressure:"); Serial.print(manPressure);
        Serial.print(" | rpm:"); Serial.print(rpm);
        Serial.print(" | speed:"); Serial.print(speed);
        Serial.print(" | intake:"); Serial.print(intake);
        Serial.print(" | maf:"); Serial.print(maf);
        Serial.print(" | runtime:"); Serial.print(runtime);
        Serial.print(" | fuel:"); Serial.print(fuel);
        Serial.print(" | transTemp:"); Serial.print(transTemp);
        Serial.print(" | distance:"); Serial.print(distance);
        Serial.print(" | bara:"); Serial.print(bara);
        Serial.print(" | ambientTemp:"); Serial.print(ambientTemp);
        Serial.println("");
    }
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
