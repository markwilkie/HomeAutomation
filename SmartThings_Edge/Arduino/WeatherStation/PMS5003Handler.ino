#include "PMS5003Handler.h"

void PMS5003Handler::init()
{
  pmsSensor.init();
}

bool PMS5003Handler::storeSamples()
{
  bool success=pmsSensor.readPMSData();
  if(!success)
  {
    ERRORPRINTLN("Unable to read PMS5003 Sensor");
  }
  return success;
}

int PMS5003Handler::getPM0_3um()
{
  return pmsSensor.getParticles03um();
}

int PMS5003Handler::getPM2_5um()
{
  return pmsSensor.getParticles25um();
}

int PMS5003Handler::getPM10_0um()
{
  return pmsSensor.getParticles100um();
}

int PMS5003Handler::getPM10Standard()
{
  return pmsSensor.getpm10_standard();
}

int PMS5003Handler::getPM25Standard()
{
  return pmsSensor.getpm25_standard();
}

int PMS5003Handler::getPM100Standard()
{
  return pmsSensor.getpm100_standard();
}
