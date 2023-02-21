#ifndef LDR_h
#define LDR_h

class LDR 
{
  public:
    LDR(int service,int pid,int _refreshTicks);
    int getService();
    int getPid();
    int readLightLevel();
    
  private: 
    int refreshTicks;

    //My own user definied - just needs to be unique to my application
    int service;
    int pid;

    //Data
    int lightLevel;

    //Timing
    unsigned long nextTickCount;      //when to update/refresh the gauge again
};

#endif
