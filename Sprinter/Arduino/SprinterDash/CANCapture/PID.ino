#include "PID.h"
#include "tinyexpr.h"

//translation will have to conv 0x01 to 0x04 and 0x7E8 from 0x7DF
//
PID::PID(unsigned int _id,unsigned int _service,unsigned int _PIDCode,const char *_label,const char *_unit,const char *_formula,int _updateFreq)
{
    id=_id;
    service=_service;
    PIDCode=_PIDCode;
    label=_label;
    unit=_unit;
    formula=_formula;
    updateFreq=_updateFreq;

    //determine response IDs
    if(id==0x7DF)
        responseId=0x7E8;
    if(id==0x7E1)
        responseId=0x7E9;     
}

//Pass each response in to see if there's a match
bool PID::isMatch(unsigned int incomingId,unsigned char *canFrame)
{
    //If ext data mode, don't match on pid
    /*
    if(extDataMode && incomingId==responseId && service==canFrame[0])
    {       
        //Serial.printf("Match- id:0x%04x,resp:0x%04x,s:0x%02x,rs:0x%02x,ipid:0x%02x,pid:0x%02x \n",incomingId,responseId,data0,service); 
        return true;
    }
    */

    //Serial.printf("Match- id:0x%04x,resp:0x%04x,s:0x%02x,rs:0x%02x,ipid:0x%02x,pid:0x%02x \n",incomingId,responseId,testData->GetData(1),(service + 0x40),testData->GetData(2),PIDCode); 

    //Do full matching
    if(incomingId==responseId)
    {       
        if((service+0x40)==canFrame[0] && PIDCode==canFrame[1])
        {
            //Serial.printf("Match- id:0x%04x,resp:0x%04x,s:0x%02x,rs:0x%02x,ipid:0x%02x,pid:0x%02x \n",incomingId,responseId,incomingService,(service + 0x40),incomingPid,PIDCode); 
            return true;
        }
    }

    return false;
}

const unsigned int PID::getId()
{
    return id;
}

const unsigned int PID::getRxId()
{
    return responseId;
}

unsigned int PID::getService()
{
    return service;
}

unsigned int PID::getPID()
{
    return PIDCode;
}

unsigned long PID::getNextUpdateMillis()
{
    return nextUpdateMillis;
}

void PID::setNextUpdateMillis()
{
    nextUpdateMillis=millis()+updateFreq;
}

//Get results when there's a match
double PID::getResult(unsigned char *buffer)
{
    /* Store variable names and pointers. */
    double a,b,c,d,e;
    te_variable vars[] = {{"A", &a}, {"B", &b}, {"C", &c}, {"D", &d}, {"E", &e}};

    int err;
    double result; 

    /* Compile the expression with variables. */
    te_expr *expr = te_compile(formula, vars, 5, &err);
    if (expr) 
    {
        //If extended data mode, the data starts 2nd byte in.  otherwise, it's normal with len+svc+pid first....
        a=buffer[2]; b=buffer[3]; c=buffer[4]; d=buffer[5]; e=buffer[13];
        
        result = te_eval(expr);
        Serial.printf("Formula Result:%f using %s with %d\n", result,formula,a);
        te_free(expr);
    } 
    else 
    {
        Serial.printf("Parse error at %d\n", err);
    } 

    return result;     
}

const char *PID::getLabel()
{
    return label;
}

const char *PID::getUnit()
{
    return unit;
}
