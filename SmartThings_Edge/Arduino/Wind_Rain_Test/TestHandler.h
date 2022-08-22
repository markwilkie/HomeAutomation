#ifndef TESTHANDLER_H
#define TESTHANDLER_H

#include "WindRain.h"

class TestHandler 
{

  public:
    void setVal(int);
    int getVal();
    
  private: 
    WindRain windRain;
};

#endif
