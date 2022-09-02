#include <Arduino.h>
#include "logger.h"
#include "debug.h"

extern Logger logger;

#ifndef PMS5003_H
#define PMS5003_H

#define PMSTIMEOUT      5000  //how long to wait for data before erroring in ms
#define PMSTOTALTRIES   10    //how often to try while waiting to timeout
#define PMSMINVOLTAGE   4.0   //won't try and get a reading if below this voltage

struct pms5003data 
{
  uint16_t framelen;
  uint16_t pm10_standard, pm25_standard, pm100_standard;
  uint16_t pm10_env, pm25_env, pm100_env;
  uint16_t particles_03um, particles_05um, particles_10um, particles_25um, particles_50um, particles_100um;
  uint16_t unused;
  uint16_t checksum;
};

class PMS5003 
{
  public:
    void init();
    bool readPMSData();

    uint16_t getpm10_standard();
    uint16_t getpm25_standard();
    uint16_t getpm100_standard();
    
    uint16_t getpm10_env();
    uint16_t getpm25_env();
    uint16_t getpm100_env();

    uint16_t getParticles03um();
    uint16_t getParticles05um();
    uint16_t getParticles10um();
    uint16_t getParticles25um();
    uint16_t getParticles50um();
    uint16_t getParticles100um();
    
  private:
    struct pms5003data data;

    uint16_t pm10_standard;
    uint16_t pm25_standard;
    uint16_t pm100_standard;
};
#endif
