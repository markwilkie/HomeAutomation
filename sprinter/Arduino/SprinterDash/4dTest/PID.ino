#include "PID.h"


PID::PID(int _service,int _pid)
{
    service=_service;
    pid=_pid;
}

bool PID::isMatch(int incomingSerivce,int incomingPid)
{
    if(incomingPid==pid && incomingSerivce==service)
        return true;

    return false;
}

void PID::setValue(int _value)
{
    value = _value;
}

int PID::getValue()
{
    return value;
}