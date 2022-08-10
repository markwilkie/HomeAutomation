#include "PMS5003.h"
#include "debug.h"

void PMS5003::init()
{
  // sensor baud rate is 9600
  Serial2.begin(9600);
}

uint16_t PMS5003::getpm10_standard()
{
  return data.pm10_env;
}

uint16_t PMS5003::getpm25_standard()
{
  return data.pm25_env;
}

uint16_t PMS5003::getpm100_standard()
{
  return data.pm100_env;
}

uint16_t PMS5003::getpm10_env()
{
  return data.pm10_env;
}

uint16_t PMS5003::getpm25_env()
{
  return data.pm25_env;
}

uint16_t PMS5003::getpm100_env()
{
  return data.pm100_env;
}

uint16_t PMS5003::getParticles03um()
{
  return data.particles_03um;
}

uint16_t PMS5003::getParticles05um()
{
  return data.particles_05um;
}

uint16_t PMS5003::getParticles10um()
{
  return data.particles_10um;
}

uint16_t PMS5003::getParticles25um()
{
  return data.particles_25um;
}

uint16_t PMS5003::getParticles50um()
{
  return data.particles_50um;
}

uint16_t PMS5003::getParticles100um()
{
  return data.particles_100um;
}


bool PMS5003::readPMSData()
{
  Stream *s = &Serial2;

  //Wait for data
  int tryNumber=0;
  while(!s->available())
  {
    tryNumber++;
    
    if(tryNumber>TOTALTRIES)
    {
      ERRORPRINTLN("Timed out waiting for data");
      return false;
    }
    
    delay((TIMEOUT/TOTALTRIES));
  }
  
  // Read a byte at a time until we get to the special '0x42' start-byte
  if (s->peek() != 0x42) {
    s->read();
    return false;
  }
 
  // Now read all 32 bytes
  if (s->available() < 32) {
    return false;
  }
    
  uint8_t buffer[32];    
  uint16_t sum = 0;
  s->readBytes(buffer, 32);
 
  // get checksum ready
  for (uint8_t i=0; i<30; i++) {
    sum += buffer[i];
  }
 
  /* debugging
  for (uint8_t i=2; i<32; i++) {
    VERBOSEPRINT("0x"); VERBOSEPRINT(buffer[i], HEX); VERBOSEPRINT(", ");
  }
  VERBOSEPRINTln();
  */
  
  // The data comes in endian'd, this solves it so it works on all platforms
  uint16_t buffer_u16[15];
  for (uint8_t i=0; i<15; i++) {
    buffer_u16[i] = buffer[2 + i*2 + 1];
    buffer_u16[i] += (buffer[2 + i*2] << 8);
  }
 
  // put it into a nice struct :)
  memcpy((void *)&data, (void *)buffer_u16, 30);
 
  if (sum != data.checksum) {
    ERRORPRINTLN("Checksum failure");
    return false;
  }

  VERBOSEPRINTLN("---------------------------------------");
  VERBOSEPRINTLN("Concentration Units (standard)");
  VERBOSEPRINT("PM 1.0: "); VERBOSEPRINT(data.pm10_standard);
  VERBOSEPRINT("\t\tPM 2.5: "); VERBOSEPRINT(data.pm25_standard);
  VERBOSEPRINT("\t\tPM 10: "); VERBOSEPRINTLN(data.pm100_standard);
  VERBOSEPRINTLN("---------------------------------------");
  VERBOSEPRINTLN("Concentration Units (environmental)");
  VERBOSEPRINT("PM 1.0: "); VERBOSEPRINT(data.pm10_env);
  VERBOSEPRINT("\t\tPM 2.5: "); VERBOSEPRINT(data.pm25_env);
  VERBOSEPRINT("\t\tPM 10: "); VERBOSEPRINTLN(data.pm100_env);
  VERBOSEPRINTLN("---------------------------------------");
  VERBOSEPRINT("Particles > 0.3um / 0.1L air:"); VERBOSEPRINTLN(data.particles_03um);
  VERBOSEPRINT("Particles > 0.5um / 0.1L air:"); VERBOSEPRINTLN(data.particles_05um);
  VERBOSEPRINT("Particles > 1.0um / 0.1L air:"); VERBOSEPRINTLN(data.particles_10um);
  VERBOSEPRINT("Particles > 2.5um / 0.1L air:"); VERBOSEPRINTLN(data.particles_25um);
  VERBOSEPRINT("Particles > 5.0um / 0.1L air:"); VERBOSEPRINTLN(data.particles_50um);
  VERBOSEPRINT("Particles > 10.0 um / 0.1L air:"); VERBOSEPRINTLN(data.particles_100um);
  VERBOSEPRINTLN("---------------------------------------"); 
  
  // success!
  return true;
}
