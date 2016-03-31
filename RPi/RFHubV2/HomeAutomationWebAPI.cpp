#include <stdio.h>
#include <exception>
#include <iostream>
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
	WebAPI webAPI(baseURL);
	std::string webResponse=webAPI.PlainPOST("/api/token","","{\"pat\":\""+pat+"\"}");
	token=ParseToken(webResponse);

	if(token.length()>2)
		authenticatedFlag=true;

	LogLine()  << "AuthenticatedFlag: " << authenticatedFlag << '\n';
	LogLine()  << "Token: " << token << '\n';

	return authenticatedFlag;
}

void HomeAutomationWebAPI::GetTokens()
{
	WebAPI webAPI(baseURL);
	webAPI.AuthenticatedGET("/api/token","",token);
}

void HomeAutomationWebAPI::AddDevice(int UnitNum,const std::string &Description)
{
	char unitNumStr[5];
	sprintf(unitNumStr,"%d",UnitNum);
	std::string unitString(unitNumStr);

	try
	{
		WebAPI webAPI(baseURL);
		webAPI.AuthenticatedPOST("/api/devices","","{\"UnitNum\":"+unitString+",\"Description\":\""+Description+"\"}",token);
	}
	catch(WebAPIException& e)
	{
		//if http error code 401 and type is http (1) or timeout
		if((e.type == 1 && e.errCode==401) || (e.type==0 && e.errCode==CURLE_OPERATION_TIMEDOUT))
		{
			LogLine()  << "ERROR: " << e.errCode << " About to authenticate again\n";

			//Auth again we're expired
			bool authFlag=Authenticate();

			LogLine()  << "That worked, let's try our request again\n";

			//Try our request again
			WebAPI webAPI(baseURL);
			webAPI.AuthenticatedPOST("/api/devices","","{\"UnitNum\":"+unitString+",\"Description\":\""+Description+"\"}",token);
		}
		else
		{
			LogLine()  << "Not 401 - sending on... -->" << e.errCode << ": " << e.errString << "\n";
			throw(e);
		}

	}
	catch(std::exception& e)
	{
		LogLine()  << "Exception not caught" << e.what() << "\n";
		throw(e);
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

