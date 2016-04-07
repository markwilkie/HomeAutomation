#include "WebAPI.h"

class HomeAutomationWebAPI 
{
public:
    HomeAutomationWebAPI();
    bool Authenticate(const std::string &pat); 
    void GetTokens(); 
    void AddDevice(int UnitNum,const std::string &description,long seconds_past_epoch);
    void AddEvent(int UnitNum,char eventCodeType,char eventCode,long seconds_past_epoch);
    void AddState(int UnitNum,float vcc,float temperature,int pinState,long seconds_past_epoch);
    void SendAlarm(int UnitNum,char alarmType,char alarmCode);

private:
    std::string ParseToken(const std::string &webResponse);
    bool Authenticate();
    void AuthenticatedPOST(const std::string &controller,const std::string &api,const std::string &postdata,const std::string &token);

    //State
    bool authenticatedFlag;  //this will be set after getting a token

    //Site specifics
    std::string token;     //access token - used for authorization 
    std::string baseURL;
    std::string pat;
};
