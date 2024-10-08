#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include "PapertrailLogger.h"

#define SERIALLOGGER    //If defined, we'll print to serial as well.

#define PAPERTRAIL_HOST       "logs4.papertrailapp.com"
#define PAPERTRAIL_PORT       54449
#define PAPERTRAIL_SYSTEMNAME "batterymonitor"

#define MAXLOGSIZE 2048   //Used for caching logs in a char array until there's a wifi connection

extern RTC_DATA_ATTR char logCache[];
extern RTC_DATA_ATTR int logCacheIndex;

//For our printf implementation
#define SERIAL_PRINTF_MAX_BUFF      512
#define F_PRECISION                 2

//Log Levels
#define ERROR 0
#define WARNING 1
#define INFO 2
#define VERBOSE 3

class Logger 
{

  public:
    Logger();
    void sendLogs(bool wifiConnected);
    void log(const char *input ,bool cr=true);
    void log(String str, bool cr);
    void log(int number, bool cr);    
    void log(long number, bool cr);    
    void log(unsigned long number, bool cr);      
    void log(double number, bool cr);
    bool log(int,const char *fmt, ...);
    
  private: 
    PapertrailLogger *errorLog;
    PapertrailLogger *warningLog;
    PapertrailLogger *noticeLog;
    PapertrailLogger *debugLog;
    PapertrailLogger *infoLog; 

};

#endif
