#define ERRORDEF
#define INFODEF
//#define VERBOSEDEF

#ifdef ERRORDEF
  #define ERRORPRINT(...)   Serial.print(__VA_ARGS__)
  #define ERRORPRINTDEC(...)  Serial.print(__VA_ARGS__,DEC)
  #define ERRORPRINTHEX(...)  Serial.print(__VA_ARGS__,HEX)
  #define ERRORPRINTLN(...)   Serial.println(__VA_ARGS__)
#else
  #define ERRORPRINT(...)
  #define ERRORPRINTHEX(...)
  #define ERRORPRINTDEC(...)
  #define ERRORPRINTLN(...)
#endif

#ifdef INFODEF
  #define INFOPRINT(...)   Serial.print(__VA_ARGS__)
  #define INFOPRINTLN(...)   Serial.println(__VA_ARGS__)
#else
  #define INFOPRINT(...)
  #define INFOPRINTLN(...)
#endif

#ifdef VERBOSEDEF
  #define VERBOSEPRINT(...)       Serial.print (__VA_ARGS__)
  #define VERBOSEPRINTDEC(...)    Serial.print(__VA_ARGS__,DEC)
  #define VERBOSEPRINTHEX(...)    Serial.print(__VA_ARGS__,HEX)
  #define VERBOSEPRINTHEXLN(...)  Serial.println(__VA_ARGS__,HEX)
  #define VERBOSEPRINTLN(...)     Serial.println (__VA_ARGS__)
#else
  #define VERBOSEPRINT(...)
  #define VERBOSEPRINTHEX(...)
  #define VERBOSEPRINTHEXLN(...)
  #define VERBOSEPRINTDEC(...)
  #define VERBOSEPRINTLN(...)
#endif
