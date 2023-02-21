#ifndef LED_h
#define LED_h

#include <genieArduino.h>

class Led 
{
  public:
    Led(int _ledObjNum,int _service,int _pid,int _refreshTicks);

    bool isMatch(int incomingSvc, int incomingPid);

    void turnOn();
    void startBlink();
    void turnOff();
    bool isActive();
    void update();  //update actual led on screen
    
  private: 
    //What this is  (e.g. rpm)
    int service;
    int pid;

    //Config
    int refreshTicks=1;         //How many ticks the gauge refreshes    
    int ledObjNum;
    bool blink=false;
    
    //State
    bool ledState=false;
    bool lastState=false;

    //Timing
    unsigned long nextTickCount;      //when to update/refresh the gauge again
};

#endif