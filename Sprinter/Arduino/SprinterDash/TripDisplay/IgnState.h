#ifndef IgnState_h
#define IgnState_h

class IgnState 
{
  public:
    IgnState(int _refreshTicks);
    bool getIgnState();
    
  private: 
    int refreshTicks;

    //Data
    bool ignState;

    //Timing
    unsigned long nextTickCount;      //when to update/refresh the gauge again
};

#endif