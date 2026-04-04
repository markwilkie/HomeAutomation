#include <Arduino.h>
#include "PropBag.h"
#include "../Globals.h"
#include "../net/VanWifi.h"

void PropBag::resetPropBag()
{
    logger.log(INFO,"Resetting Prop Bag");
    data.pitotCalibration=PITOT_CALIB;
    data.instMPGFactor=INST_MPG_FACTOR;
    data.elevationOffset=0;
    //lastLat/lastLon intentionally preserved — they represent physical location, not trip state
}

int PropBag::getPropDataSize()
{
    return sizeof(data);
}

//Save data to EEPROM
void PropBag::savePropBag()
{
  logger.log(INFO,"Saving Prop Bag to EEPROM");

  EEPROM.begin(512);
  EEPROM.put(0, data); 
  logger.log(VERBOSE,"EEPROM Ret: %d",EEPROM.commit());

  dumpPropBag();
}

//Load data to EEPROM
void PropBag::loadPropBag()
{
  logger.log(INFO,"Loading Prop Bag from EEPROM");

  EEPROM.begin(512);
  EEPROM.get(0, data);
  EEPROM.end();

  dumpPropBag();
}

void PropBag::dumpPropBag()
{
    logger.log(INFO,"Dumping PropBag");
    logger.log(INFO,"   Inst MPG Factor: %f",data.instMPGFactor);
    logger.log(INFO,"   Pitot Calib: %f",data.pitotCalibration);
    logger.log(INFO,"   Elevation Offset: %d",data.elevationOffset);
    logger.log(INFO,"   Last GPS: %f,%f",(double)data.lastLat,(double)data.lastLon);
}
