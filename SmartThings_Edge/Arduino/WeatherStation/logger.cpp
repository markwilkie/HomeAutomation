#include "logger.h"
#include "weatherwifi.h"

RTC_DATA_ATTR char logCache[MAXLOGSIZE];
RTC_DATA_ATTR int logCacheIndex=0;

extern WeatherWifi weatherWifi;

void Logger::init()
{
  //errorLog = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Error, "\033[0;31m", "weatherstation", " ");
  //warningLog = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Warning, "\033[0;33m", "weatherstation", " ");
  //noticeLog = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Notice, "\033[0;36m", "weatherstation", " ");
  //debugLog= new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Debug, "\033[0;32m","weatherstation", " ");
  infoLog = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Info, "\033[0;34m", "weatherstation", " ");
}

void Logger::sendLogs()
{
  if(logCacheIndex<1)
    return;

  if(!weatherWifi.isConnected())    
    return;

  //make sure there's a <cr> at the end
  if(logCache[logCacheIndex-1]!='\n')
  {
    logCache[logCacheIndex++]='\n';
    logCache[logCacheIndex]='\0';
  }

  //Send
  infoLog->printf("%s",logCache);
  logCacheIndex=0;   //reset cache
}

void Logger::log(int num,bool cr)
{
  //now, add number
  String line="";
  line = line + num;

  //Add last bit
  log(line.c_str(),cr);
}

void Logger::log(long num,bool cr)
{
  //now, add number
  String line="";
  line = line + num;

  //Add last bit
  log(line.c_str(),cr);
}

void Logger::log(float flt,bool cr)
{
  //now, add number
  String line="";
  line = line + flt;

  //Add last bit
  log(line.c_str(),cr);
}

void Logger::log(String str,bool cr)
{
  //get the char pointer
  log(str.c_str(),cr);
}

void Logger::log(const char input[],bool cr)
{
 if(logCacheIndex+sizeof(input) >= MAXLOGSIZE)
  return;
  
 int i=0;
 while(input[i])  //goes until it hits a null
 {
  logCache[logCacheIndex]=input[i];
  i++;
  logCacheIndex++;
 }

 //CR?
 if(cr)
  logCache[logCacheIndex++]='\n';
  
 logCache[logCacheIndex]='\0';
}
