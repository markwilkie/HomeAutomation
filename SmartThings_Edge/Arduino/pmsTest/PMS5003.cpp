#include "PMS5003.h"

PMS5003::PMS5003()
{
  // sensor baud rate is 9600
  Serial2.begin(9600);

  //turn on air quality
  pinMode(25,OUTPUT);
  digitalWrite(25, HIGH); 
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
      Serial.println("Timed out waiting for data");
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
    Serial.print("0x"); Serial.print(buffer[i], HEX); Serial.print(", ");
  }
  Serial.println();
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
    Serial.println("Checksum failure");
    return false;
  }
  // success!
  return true;
}
