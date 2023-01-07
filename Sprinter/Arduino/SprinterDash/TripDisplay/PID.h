#ifndef PID_h
#define PID_h

class PID 
{
  public:
    PID(int _service,int _pid);

    //Pass each response in to see if there's a match
    bool isMatch(int incomingSvc, int incomingPid);

    void setValue(int value);
    int getValue();
    
  private: 

    int service;
    int pid;
    int value;

};

#endif