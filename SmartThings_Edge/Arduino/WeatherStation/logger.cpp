#include "logger.h"
#include "weatherwifi.h"

RTC_DATA_ATTR char logCache[MAXLOGSIZE];
RTC_DATA_ATTR int logCacheIndex=0;

extern WeatherWifi weatherWifi;

void Logger::init()
{
  //errorLog = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Error, "\033[0;31m", PAPERTRAIL_SYSTEMNAME, " ");
  //warningLog = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Warning, "\033[0;33m", PAPERTRAIL_SYSTEMNAME, " ");
  //noticeLog = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Notice, "\033[0;36m", PAPERTRAIL_SYSTEMNAME, " ");
  //debugLog= new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Debug, "\033[0;32m",PAPERTRAIL_SYSTEMNAME, " ");
  infoLog = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Info, "\033[0;34m", PAPERTRAIL_SYSTEMNAME, " ");
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

/**
 * --------------------------------------------------------------
 * Perform simple printing of formatted data
 * Supported conversion specifiers: 
 *      d, i     signed int
 *      u        unsigned int
 *      ld, li   signed long
 *      lu       unsigned long
 *      f        double
 *      c        char
 *      s        string
 *      %        '%'
 * Usage: %[conversion specifier]
 * Note: This function does not support these format specifiers: 
 *      [flag][min width][precision][length modifier]
 * 
 * https://medium.com/@kslooi/print-formatted-data-in-arduino-serial-aaea9ca840e3
 * --------------------------------------------------------------
 */
bool Logger::log(int logLevel,const char *fmt, ...) 
{ 
  /* buffer for storing the formatted data */
  char buf[SERIAL_PRINTF_MAX_BUFF];
  char *pbuf = buf;
  bool bufferOverflow=false;
  int len=0;
  char *svar;
  String astr;

  //Let's make sure we're not out of bounds
  if(strlen(fmt)>SERIAL_PRINTF_MAX_BUFF)
  {
    memcpy(buf,fmt,SERIAL_PRINTF_MAX_BUFF-1);
    pbuf+=SERIAL_PRINTF_MAX_BUFF-1;
    bufferOverflow=true;
  }  
  
  /* pointer to the variable arguments list */
  va_list pargs;
  
  /* Initialise pargs to point to the first optional argument */
  va_start(pargs, fmt);
  /* Iterate through the formatted string to replace all 
  conversion specifiers with the respective values */
  while(*fmt && !bufferOverflow) 
  {
    if(*fmt == '%') 
    {
      switch(*(++fmt)) 
      {
        case 'd': 
        case 'i': 
          if((pbuf-buf+10)>=SERIAL_PRINTF_MAX_BUFF){
            bufferOverflow=true; }
          else{
            pbuf += sprintf(pbuf, "%d", va_arg(pargs, int));}
          break;
        case 'u': 
          if((pbuf-buf+10)>=SERIAL_PRINTF_MAX_BUFF){
            bufferOverflow=true; }
          else{
            pbuf += sprintf(pbuf, "%u", va_arg(pargs, unsigned int));}
          break;
        case 'l': 
          switch(*(++fmt)) 
          {
            case 'd': 
            case 'i': 
              if((pbuf-buf+10)>=SERIAL_PRINTF_MAX_BUFF){
                bufferOverflow=true; }
              else{
                pbuf += sprintf(pbuf, "%ld", va_arg(pargs, long));}
              break;
            case 'u': 
              if((pbuf-buf+10)>=SERIAL_PRINTF_MAX_BUFF){
                bufferOverflow=true; }
              else{
                pbuf += sprintf( pbuf, "%lu", va_arg(pargs, unsigned long));}           
              break;
          }
          break;
        case 'f': 
          if((pbuf-buf+15)>=SERIAL_PRINTF_MAX_BUFF){
            bufferOverflow=true; }
          else{
            pbuf += strlen(dtostrf( va_arg(pargs, double), 1, F_PRECISION, pbuf));}        
          break;       
        case 'c':
          *(pbuf++) = (char)va_arg(pargs, int);
          break;
        case 's': 
          svar=va_arg(pargs, char *);
          len=strlen(svar);
          if((pbuf-buf+len)>=SERIAL_PRINTF_MAX_BUFF){
            bufferOverflow=true; }
          else{           
            pbuf += sprintf(pbuf, "%s", svar);}         
          break;
        case 'S': 
          astr=va_arg(pargs, String);
          len=strlen(astr.c_str());
          if((pbuf-buf+len)>=SERIAL_PRINTF_MAX_BUFF){
            bufferOverflow=true; }
          else{           
            pbuf += sprintf(pbuf, "%s", astr.c_str());}         
          break;
        case '%':
          *(pbuf++) = '%';
          break;
        default:
          break;
      }
    }
    else 
    {
      *(pbuf++) = *fmt;
    }

    fmt++;
  }
  
  *pbuf = '\0';
  
  va_end(pargs);

  if(bufferOverflow)
  {
    ERRORPRINTLN("Buffer overflow in logs - truncating.");
  }

  if(logLevel==INFO) {
    INFOPRINTLN(buf); }
  else if(logLevel==WARNING) {
    WARNPRINTLN(buf); }
  else if(logLevel==ERROR) {
    ERRORPRINTLN(buf); }
  else {
    VERBOSEPRINTLN(buf); }
  
  //Serial.print(buf);
  return !bufferOverflow;
}