#include "PropBag.h"
#include "VanWifi.h"

void PropBag::resetPropBag()
{
    logger.log(INFO,"Resetting Prop Bag");
    data.pitotCalibration=PITOT_CALIB;
    data.instMPGFactor=INST_MPG_FACTOR;
}

int PropBag::getPropDataSize()
{
    return sizeof(data);
}

//Save data to EEPROM
void PropBag::savePropBag()
{
  logger.log(INFO,"Saving Prop Bag to EEPROM");

  // The begin() call is required to initialise the EEPROM library
  EEPROM.begin(512);

  // put some data into eeprom in position zero
  EEPROM.put(0, data); 

  // write the data to EEPROM
  logger.log(VERBOSE,"EEPROM Ret: %d",EEPROM.commit());

  dumpPropBag();
}

//Load data to EEPROM
void PropBag::loadPropBag()
{
  logger.log(INFO,"Loading Prop Bag from EEPROM");

  // The begin() call is required to initialise the EEPROM library
  EEPROM.begin(512);
  // get some data from eeprom
  EEPROM.get(0, data);
  EEPROM.end();

  //dump
  dumpPropBag();
}

void PropBag::dumpPropBag()
{
    logger.log(INFO,"Dumping PropBag");
    logger.log(INFO,"   Inst MPG Factor: %f",data.instMPGFactor);
    logger.log(INFO,"   Pitot Calib: %f",data.pitotCalibration);
}
