#ifndef PID_h
#define PID_h

#include "TestData.h"

class PID 
{

  public:
    PID(unsigned int _id,unsigned int _service,unsigned int _PID,const char *_label,const char *_unit,const char *_formula,int _updateFreq);  

    //Pass each response in to see if there's a match
    bool isMatch(unsigned int id,unsigned char *canFrame);
    bool isExtData();

    //Get results when there's a match
    double getResult(unsigned char *canFrame);
    const unsigned int getId();
    const unsigned int getRxId();
    unsigned int getService();
    unsigned int getPID();
    const char *getLabel();
    const char *getUnit();
    unsigned long getNextUpdateMillis();
    void setNextUpdateMillis();
    
  private: 
    unsigned int id;
    unsigned int responseId;
    unsigned int service;
    unsigned int PIDCode;
    const char *label;
    const char *unit;
    const char *formula;

    int updateFreq;  //in millis
    unsigned long nextUpdateMillis;
};

#endif
