#include <iostream>       // std::cout
#include <string>         // std::string
#include <sys/resource.h>
#include <unistd.h>

#include "Logger.h"
#include "Exception.h"
#include "HomeAutomationWebAPI.h"

void loop(HomeAutomationWebAPI &api);
struct rusage memInfo;

int main ()
{
    LogLine() << "Starting....";
    HomeAutomationWebAPI api=HomeAutomationWebAPI();

    bool authenticatedFlag=false;

    try
    {
        while(!authenticatedFlag)
        {
            std::cout << "Authenticating...\n";
            authenticatedFlag=api.Authenticate("6c061cldh0276dd9972bad582e669sjh6d14e4b9b7b6ddi862ad8c35");  //authenticate w/ pat
            std::cout << "SuccessFlag: " << authenticatedFlag << '\n';
            if(!authenticatedFlag)
                sleep(1);
        }


        api.AddDevice(99,"test");
    }
    catch(WebAPIException& e)
    {
        std::cout << "Exception details: " << e.errCode << ": " << e.errString << "\n";
    }
    catch(...)
    {
        std::cout << "darn...\n";
    }

    //loop(api);
    getrusage(RUSAGE_SELF,&memInfo);
    std::cout << "LAST:    Mem: " << memInfo.ru_maxrss << '\n';
}

void loop(HomeAutomationWebAPI &api)
{
    for(int i=0;i<15;i++)
    {
        api.GetTokens();
        std::cout << "Sending request #: " << i;

            getrusage(RUSAGE_SELF,&memInfo);
            std::cout << "    Mem: " << memInfo.ru_maxrss << '\n';
    }
}
