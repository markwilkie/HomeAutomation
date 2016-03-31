#include "WebAPI.h"

class HomeAutomationWebAPI 
{
public:
	HomeAutomationWebAPI();
	bool Authenticate(const std::string &pat); 
	void GetTokens(); 
	void AddDevice(int UnitNum,const std::string &Description);

private:
	std::string ParseToken(const std::string &webResponse);
	bool Authenticate();

	//State
	bool authenticatedFlag;  //this will be set after getting a token

	//Site specifics
	std::string token;     //access token - used for authorization 
	std::string baseURL;
	std::string pat;
};
