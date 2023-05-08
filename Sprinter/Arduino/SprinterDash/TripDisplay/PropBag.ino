#include "PropBag.h"
#include "VanWifi.h"

void PropBag::resetPropData()
{
    logger.log(VERBOSE,"Resetting Prop Bag");
    data.pitotCalibration=0;
    data.instMPGFactor=0;
}

int PropBag::getPropDataSize()
{
    return sizeof(data);
}

//Save data to EEPROM
void PropBag::savePropBag()
{
  logger.log(VERBOSE,"Saving Prop Bag to EEPROM");

  // The begin() call is required to initialise the EEPROM library
  int dataSize=sizeof(data);
  EEPROM.begin(512);

  // put some data into eeprom
  EEPROM.put(dataSize, data); 

  // write the data to EEPROM
  logger.log(VERBOSE,"EEPROM Ret: %d",EEPROM.commit());

  dumpPropBag();
}

//Load data to EEPROM
void PropBag::loadPropBag()
{
  logger.log(VERBOSE,"Loading Prop Bag from EEPROM");

  // The begin() call is required to initialise the EEPROM library
  int dataSize=sizeof(data);
  EEPROM.begin(512);
  // get some data from eeprom
  EEPROM.get(dataSize, data);
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