#include <stdio.h>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/resource.h> 
#include <curl/curl.h>

#include "Logger.h"
#include "Exception.h"
#include "HomeAutomationWebAPI.h"

HomeAutomationWebAPI::HomeAutomationWebAPI()
{
    authenticatedFlag=false;
    token="";
    baseURL="http://wilkiehomeautomation.azurewebsites.net";
}

bool HomeAutomationWebAPI::Authenticate(const std::string &PAT)
{
    pat=PAT;
    return HomeAutomationWebAPI::Authenticate();
}

bool HomeAutomationWebAPI::Authenticate()
{
    try
    {
        WebAPI webAPI(baseURL);
        std::string webResponse=webAPI.PlainPOST("/api/token","","{\"pat\":\""+pat+"\"}");
        token=ParseToken(webResponse);

        if(token.length()>2)
            authenticatedFlag=true;

        LogLine()  << "AuthenticatedFlag: " << authenticatedFlag;
    }
    catch(WebAPIException& e)
    {
        LogErrorLine()  << "Problem Authenticating - ERROR: " << e.errCode << ": " << e.errString;
    }

    return authenticatedFlag;
}

void HomeAutomationWebAPI::GetTokens()
{
    try
    {
        WebAPI webAPI(baseURL);
        webAPI.AuthenticatedGET("/api/token","",token);
    }
    catch(WebAPIException& e)
    {
        LogErrorLine()  << "Problem Getting Tokens - ERROR: " << e.errCode << ": " << e.errString;
    }
}

void HomeAutomationWebAPI::AddDevice(int UnitNum,const std::string &description,long seconds_past_epoch)
{
    char unitNumStr[5];
    sprintf(unitNumStr,"%d",UnitNum);
    std::string unitString(unitNumStr);

	std::ostringstream urlString;
	urlString << "{\"UnitNum\":" << UnitNum <<
		",\"Description\":\"" << description <<
		"\",\"DeviceDate\":\"" << (long)seconds_past_epoch <<
		"\"}";

	AuthenticatedPOST("/api/devices","",urlString.str(),token);
}

void HomeAutomationWebAPI::AddEvent(int UnitNum,char eventCodeType,char eventCode,long seconds_past_epoch)
{
    std::ostringstream urlString;
    urlString << "{\"UnitNum\":" << UnitNum << 
    	",\"EventCodeType\":\"" << eventCodeType << 
    	"\",\"EventCode\":\"" << eventCode << 
    	"\",\"DeviceDate\":\"" << (long)seconds_past_epoch << 
    	"\"}";

    AuthenticatedPOST("/api/events","",urlString.str(),token);
}

void HomeAutomationWebAPI::AddState(int UnitNum,float vcc,float temperature,char presence,long seconds_past_epoch)
{
    std::ostringstream urlString;
    urlString << "{\"UnitNum\":" << UnitNum <<
    	",\"vcc\":\"" << vcc << 
    	"\",\"temperature\":\"" << temperature << 
    	"\",\"Presence\":\"" << presence << 
    	"\",\"DeviceDate\":\"" << (long)seconds_past_epoch << 
    	"\"}";

    AuthenticatedPOST("/api/states","",urlString.str(),token);
}

void HomeAutomationWebAPI::AddWeather(int unitNum,float temperature,float humidity,int windSpeed,int windDirection,long seconds_past_epoch)
{
    std::ostringstream urlString;
    urlString << "{\"unitNum\":" << unitNum<<
    	",\"temperature\":\"" << temperature<< 
    	"\",\"humidity\":\"" << humidity<< 
    	"\",\"windSpeed\":\"" << windSpeed<< 
    	"\",\"windDirection\":\"" << windDirection<< 
    	"\",\"ReadingDate\":\"" << (long)seconds_past_epoch << 
    	"\"}";

    AuthenticatedPOST("/api/weather","",urlString.str(),token);
}

void HomeAutomationWebAPI::SendAlarm(int UnitNum, char alarmType, char alarmCode)
{
	std::ostringstream urlString;
	urlString << "{\"UnitNum\":" << UnitNum <<
		",\"AlarmType\":\"" << alarmType <<
		"\",\"AlarmCode\":\"" << alarmCode <<
		"\"}";

	AuthenticatedPOST("/api/alarms", "", urlString.str(), token);
}

//
// This "wraps" WebAPI.AuthenticatePOST with logic to re-authorize is needed
//

void HomeAutomationWebAPI::AuthenticatedPOST(const std::string &controller,const std::string &api,const std::string &postdata,const std::string &token)
{
	try
    {
        WebAPI webAPI(baseURL);
        webAPI.AuthenticatedPOST(controller,api,postdata,token);
    }
    catch(WebAPIException& e)
    {
        //if http error code 401 and type is http (1) or timeout
        if((e.type == 1 && e.errCode==401) || (e.type==0 && e.errCode==CURLE_OPERATION_TIMEDOUT))
        {
            LogErrorLine()  << "ERROR: " << e.errCode << " About to authenticate again\n";

            //Auth again we're expired
            bool authFlag=Authenticate();

            try
            {
                //Try our request again
                WebAPI webAPI(baseURL);
                webAPI.AuthenticatedPOST(controller,api,postdata,token);
            }
            catch(WebAPIException& e)
            {
                LogErrorLine()  << "ERROR - could not authenticate: " << e.errCode << ": " << e.errString;
            }
        }
        else
        {
            LogErrorLine()  << "Not 401 or timeout - ERROR: " << e.errCode << ": " << e.errString;
        }

    }
}

std::string HomeAutomationWebAPI::ParseToken(const std::string &webResponse)
{
    if(webResponse.length()<5)
        return "";

    size_t startPos=webResponse.find("token")+8; 
    size_t endPos=webResponse.find(',',startPos);
    if (startPos==std::string::npos || endPos==std::string::npos)
        return "";

    std::string token=webResponse.substr(startPos,(endPos-startPos-1));

    return (token);
}

