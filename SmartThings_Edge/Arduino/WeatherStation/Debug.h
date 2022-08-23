#define ERRORDEF
#define INFODEF
#define VERBOSEDEF

#define WIFILOGGER

#if defined(ERRORDEF) && defined(WIFILOGGER)
  #define ERRORPRINT(...)   logger.log(__VA_ARGS__,false);Serial.print(__VA_ARGS__)
  #define ERRORPRINTLN(...)   logger.log(__VA_ARGS__,true);Serial.println(__VA_ARGS__)
#elif ERRORDEF
  #define ERRORPRINT(...)   Serial.print(__VA_ARGS__)
  #define ERRORPRINTLN(...)   Serial.println(__VA_ARGS__)
#else
  #define ERRORPRINT(...)
  #define ERRORPRINTHEX(...)
  #define ERRORPRINTDEC(...)
  #define ERRORPRINTLN(...)
#endif

#if defined(INFODEF) && defined(WIFILOGGER)
  #define INFOPRINT(...)   logger.log(__VA_ARGS__,false);Serial.print(__VA_ARGS__)
  #define INFOPRINTLN(...)   logger.log(__VA_ARGS__,true);Serial.println(__VA_ARGS__)
#elif defined(INFODEF)
  #define INFOPRINT(...)   Serial.print(__VA_ARGS__)
  #define INFOPRINTLN(...)   Serial.println(__VA_ARGS__)
#else
  #define INFOPRINT(...)
  #define INFOPRINTLN(...)
#endif

#if defined(VERBOSEDEF) && defined(WIFILOGGER)
  #define VERBOSEPRINT(...)   logger.log(__VA_ARGS__,false);Serial.print(__VA_ARGS__)
  #define VERBOSEPRINTLN(...)   logger.log(__VA_ARGS__,true);Serial.println(__VA_ARGS__)
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
