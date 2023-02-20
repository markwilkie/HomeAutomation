#ifndef DIGITS_h
#define DIGITS_h

#include <genieArduino.h>

class Digits 
{
  public:
    Digits(Genie *_geniePtr,int _digitsObjNum,int _min,int _max,int _decimal,int _refreshTicks);

    void setValue(double value);  //does not actually update the screen
    double getCurrentValue();
    void update();  //update actual digits on screen
    
  private: 
    Genie *geniePtr;

    //Data
    double currentValue;
    double lastValue;
    int currentDigitValue;

    //Config
    int min=0;                  //Gauge minimum
    int max=100;                //Gauge maximum
    int decimal=0;              //Decimal
    int refreshTicks=1;         //How many ticks the gauge refreshes    
    int digitsObjNum;

    //Timing
    unsigned long nextTickCount;      //when to update/refresh the gauge again
};

#endif