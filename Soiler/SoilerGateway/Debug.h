#define ERRORDEF
#define WARNDEF
#define INFODEF
#define VERBOSEDEF

#define WIFILOGGER

//Colors
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

//Defines for actual printing
#if defined(ERRORDEF) && defined(WIFILOGGER)
  #define ERRORPRINT(...)   myLogger.log(ANSI_COLOR_RED,false);myLogger.log("ERROR: ",false);myLogger.log(__VA_ARGS__,false);myLogger.log(ANSI_COLOR_RESET,false)
  #define ERRORPRINTLN(...)   myLogger.log(ANSI_COLOR_RED,false);myLogger.log("ERROR: ",false);myLogger.log(__VA_ARGS__,false);myLogger.log(ANSI_COLOR_RESET,true) 
#elif defined(ERRORDEF)
  #define ERRORPRINT(...)   Serial.print("ERROR: ");Serial.print(__VA_ARGS__)
  #define ERRORPRINTLN(...)   Serial.print("ERROR: ");Serial.println(__VA_ARGS__)
#else
  #define ERRORPRINT(...)
  #define ERRORPRINTHEX(...)
  #define ERRORPRINTDEC(...)
  #define ERRORPRINTLN(...)
#endif

#if defined(WARNDEF) && defined(WIFILOGGER)
  #define WARNPRINT(...)   myLogger.log(ANSI_COLOR_YELLOW,false);myLogger.log("WARNING: ",false);myLogger.log(__VA_ARGS__,false);myLogger.log(ANSI_COLOR_RESET,false)
  #define WARNPRINTLN(...)   myLogger.log(ANSI_COLOR_YELLOW,false);myLogger.log("WARNING: ",false);myLogger.log(__VA_ARGS__,false);myLogger.log(ANSI_COLOR_RESET,true) 
#elif defined(WARNDEF)
  #define WARNPRINT(...)   Serial.print("WARNING: ");Serial.print(__VA_ARGS__)
  #define WARNPRINTLN(...)   Serial.print("WARNING: ");Serial.println(__VA_ARGS__)
#else
  #define WARNPRINT(...)
  #define WARNPRINTLN(...)
#endif

#if defined(INFODEF) && defined(WIFILOGGER)
  #define INFOPRINT(...)   myLogger.log(ANSI_COLOR_CYAN,false);myLogger.log("INFO: ",false);myLogger.log(__VA_ARGS__,false);myLogger.log(ANSI_COLOR_RESET,false)
  #define INFOPRINTLN(...)   myLogger.log(ANSI_COLOR_CYAN,false);myLogger.log("INFO: ",false);myLogger.log(__VA_ARGS__,false);myLogger.log(ANSI_COLOR_RESET,true) 
#elif defined(INFODEF)
  #define INFOPRINT(...)   Serial.print("INFO: ");Serial.print(__VA_ARGS__)
  #define INFOPRINTLN(...)   Serial.print("INFO: ");(__VA_ARGS__)
#else
  #define INFOPRINT(...)
  #define INFOPRINTLN(...)
#endif

#if defined(VERBOSEDEF) && defined(WIFILOGGER)
  #define VERBOSEPRINT(...)   myLogger.log(__VA_ARGS__,false)
  #define VERBOSEPRINTLN(...)   myLogger.log(__VA_ARGS__,true)
#elif defined(VERBOSEDEF)
  #define VERBOSEPRINT(...)   Serial.print(__VA_ARGS__)
  #define VERBOSEPRINTLN(...)   Serial.println(__VA_ARGS__)
#else
  #define VERBOSEPRINT(...)
  #define VERBOSEPRINTHEX(...)
  #define VERBOSEPRINTHEXLN(...)
  #define VERBOSEPRINTDEC(...)
  #define VERBOSEPRINTLN(...)
#endif
