#include "PID.h"
#include "tinyexpr.h"

//translation will have to conv 0x01 to 0x04 and 0x7E8 from 0x7DF
PID::PID(unsigned long _id,unsigned char _service,unsigned char _PIDCode,const char *_label,const char *_unit,const char *_formula)
{
    id=_id;
    service=_service;
    PIDCode=_PIDCode;
    label=_label;
    unit=_unit;
    formula=_formula;

    //determine response IDs
    if(id==0x7DF)
        responseId=0x7E8;
    if(id==0x7E1)
        responseId=0x7E9;
}

//Pass each response in to see if there's a match
bool PID::isMatch(unsigned long incomingId, unsigned int incomingService, unsigned int incomingPid)
{
    if(incomingId==responseId)
    {       
        if(incomingService==(service + 0x40) && incomingPid==PIDCode)
        {
            //Serial.printf("Match- id:0x%04x,resp:0x%04x,s:0x%02x,rs:0x%02x,ipid:0x%02x,pid:0x%02x \n",incomingId,responseId,incomingService,(service + 0x40),incomingPid,PIDCode); 
            return true;
        }
    }

    return false;
}

bool PID::isMatch(unsigned long incomingId, unsigned int incomingService)
{
    if(incomingId==responseId && incomingService==service)
    {       
        //Serial.printf("Match- id:0x%04x,resp:0x%04x,s:0x%02x,rs:0x%02x,ipid:0x%02x,pid:0x%02x \n",incomingId,responseId,incomingService,(service + 0x40),incomingPid,PIDCode); 
        return true;
    }

    return false;
}

//Get results when there's a match
double PID::getResult(unsigned int _a,unsigned int _b,unsigned int _c,unsigned int _d,unsigned int _e)
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
        a=_a; b=_b; c=_c; d=_d; e=_e;
        result = te_eval(expr);
        //Serial.printf("Formula Result:%f using %s with %d\n", result,formula,a);
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