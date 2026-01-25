#ifndef LOGGER_H
#define LOGGER_H

/*
 * Logger - Unified logging system with serial output and remote Papertrail
 * 
 * This logger provides formatted output to both Serial (for USB debugging)
 * and Papertrail (for remote monitoring when van is in the field).
 * 
 * FEATURES:
 * - printf-style formatting with log levels
 * - Log caching when WiFi is unavailable
 * - Remote logging via Papertrail UDP syslog
 * - Serial output for local debugging
 * 
 * LOG LEVELS:
 * - ERROR (0):   Critical errors that need attention
 * - WARNING (1): Important events, potential issues
 * - INFO (2):    General operational information
 * - VERBOSE (3): Detailed debugging information
 * 
 * USAGE:
 *   logger.log(INFO, "Value: %d, Temp: %.1f", value, temp);
 *   logger.log(ERROR, "Connection failed to %s", deviceName);
 *   logger.sendLogs(wifi.isConnected());  // Flush to Papertrail
 * 
 * CACHING:
 * - Logs are cached in RTC_DATA_ATTR memory (survives deep sleep)
 * - Cache is flushed when sendLogs() is called with WiFi connected
 * - If cache fills, oldest logs are lost
 */

#include <Arduino.h>
#include "PapertrailLogger.h"

#define SERIALLOGGER    // If defined, logs also print to Serial

// ============================================================================
// PAPERTRAIL CONFIGURATION - Remote logging service
// ============================================================================
#define PAPERTRAIL_HOST       "logs4.papertrailapp.com"
#define PAPERTRAIL_PORT       54449
#define PAPERTRAIL_SYSTEMNAME "batterymonitor"

#define MAXLOGSIZE 2048   // Log cache size (bytes) - survives WiFi outages

extern RTC_DATA_ATTR char logCache[];   // Preserved across deep sleep
extern RTC_DATA_ATTR int logCacheIndex;

// Printf implementation limits
#define SERIAL_PRINTF_MAX_BUFF      512
#define F_PRECISION                 2

// Log Levels
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
