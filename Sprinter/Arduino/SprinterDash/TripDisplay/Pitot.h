#ifndef PITOT_h
#define PITOT_h

class Pitot 
{
  public:
    void init(int _refreshTicks);
    void calibrate();
    int readSpeed();
    
  private: 
    int refreshTicks;

    //Data
    float g_reference_pressure;
    float g_air_pressure;
    float g_airspeed_mph;

    //Timing
    unsigned long nextTickCount;      //when to update/refresh the gauge again
};

#endif