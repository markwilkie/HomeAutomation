#ifndef PID_h
#define PID_h

class PID 
{

  public:
    PID(unsigned long _id,unsigned char _service,unsigned char _PID,const char *_label,const char *_unit,const char *_formula);  

    //Pass each response in to see if there's a match
    bool isMatch(unsigned long id, unsigned int service, unsigned int pid);
    bool isMatch(unsigned long id, unsigned int service);

    //Get results when there's a match
    double getResult(unsigned int a,unsigned int b,unsigned int c,unsigned int d,unsigned int e);
    const char *getLabel();
    const char *getUnit();
    
  private: 
    unsigned long id;
    unsigned long responseId;
    unsigned int service;
    unsigned int PIDCode;
    const char *label;
    const char *unit;
    const char *formula;

};

#endif