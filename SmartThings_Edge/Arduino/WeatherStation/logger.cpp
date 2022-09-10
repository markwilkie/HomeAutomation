#include "logger.h"

RTC_DATA_ATTR char logCache[MAXLOGSIZE];
RTC_DATA_ATTR int logCacheIndex=0;

Logger::Logger()
{
  //errorLog = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Error, "\033[0;31m", PAPERTRAIL_SYSTEMNAME, " ");
  //warningLog = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Warning, "\033[0;33m", PAPERTRAIL_SYSTEMNAME, " ");
  //noticeLog = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Notice, "\033[0;36m", PAPERTRAIL_SYSTEMNAME, " ");
  //debugLog= new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Debug, "\033[0;32m",PAPERTRAIL_SYSTEMNAME, " ");
  infoLog = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Info, "\033[0;34m", PAPERTRAIL_SYSTEMNAME, " ");
}

void Logger::sendLogs(bool wifiConnected)
{
  //just return if there's nothing to send
  if(logCacheIndex<1)
    return;

  //make sure there's a <cr> at the end
  if(logCache[logCacheIndex-1]!='\n')
  {
    logCache[logCacheIndex++]='\n';
    logCache[logCacheIndex]='\0';
  }

  //poor error handling means that the paper trail blows up and the cpu reboots if there's no wifi
  if(wifiConnected)    
  {
    infoLog->printf("%s",logCache);
    logCacheIndex=0;   //reset cache
  }
  else
  {
    #ifndef WIFILOGGER
      Serial.print(logCache);
      logCacheIndex=0;   //reset cache only if WIFI is defined
    #endif
  }

  //reset cache

}

void Logger::log(int num,bool cr)
{
  //now, add number
  char buf[11];
  sprintf(buf,"%d",num);

  //Add last bit
  log(buf,cr);
}

void Logger::log(long num,bool cr)
{
  //now, add number
  char buf[11];
  sprintf(buf,"%ld",num);

  //Add last bit
  log(buf,cr);
}

void Logger::log(unsigned long num,bool cr)
{
  //now, add number
  char buf[11];
  sprintf(buf,"%lu",num);

  //Add last bit
  log(buf,cr);
}

void Logger::log(float flt,bool cr)
{
  /* 4 is mininum width, 2 is precision; float value is copied onto str_temp*/
  char buf[15];
  dtostrf(flt, 3, 2, buf);

  //Add last bit
  log(buf,cr);
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
    log(ANSI_COLOR_RED,false); 
    log("ERROR: ",false);
    log("Buffer overflow in logs - truncating.",false);
    log(ANSI_COLOR_RESET,true)    ; 
  }

  if(logLevel==INFO) 
  {
    log(ANSI_COLOR_CYAN,false); 
    log("INFO: ",false);
    log(buf,false);
    log(ANSI_COLOR_RESET,true)    ; 
  }
  else if(logLevel==WARNING)  
  {
    log(ANSI_COLOR_YELLOW,false); 
    log("WARNING: ",false);
    log(buf,false);
    log(ANSI_COLOR_RESET,true)    ; 
  }
  else if(logLevel==ERROR)
  {
    log(ANSI_COLOR_RED,false); 
    log("ERROR: ",false);
    log(buf,false);
    log(ANSI_COLOR_RESET,true);
  }
  else 
  {
    log(buf,true); 
  }

  //Serial.print(buf);
  return !bufferOverflow;
}
