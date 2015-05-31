//#define ERROR
//#define VERBOSE

#ifdef ERROR
  #define ERROR_PRINT(x)     Serial.print (x)
  #define ERROR_PRINTDEC(x)     Serial.print (x, DEC)
  #define ERROR_PRINTHEX(x)     Serial.print (x, HEX)
  #define ERROR_PRINTLN(x)  Serial.println (x)
  #define ERROR_ARRAY(x,l)    serialWriteDebug (x,l)
#else
  #define ERROR_PRINT(x)
  #define ERROR_PRINTHEX(x)
  #define ERROR_PRINTDEC(x)
  #define ERROR_PRINTLN(x)
  #define ERROR_ARRAY(x,l) 
#endif

#ifdef VERBOSE
  #define VERBOSE_PRINT(x)     Serial.print (x)
  #define VERBOSE_PRINTDEC(x)     Serial.print (x, DEC)
  #define VERBOSE_PRINTHEX(x)     Serial.print (x, HEX)
  #define VERBOSE_PRINTHEXLN(x)     Serial.println (x, HEX)
  #define VERBOSE_PRINTLN(x)  Serial.println (x)
  #define VERBOSE_ARRAY(x,l)    serialWriteDebug (x,l)
  #define VERBOSE_PRINTMILLIS(s)  PrintMillis(s)
#else
  #define VERBOSE_PRINT(x)
  #define VERBOSE_PRINTHEX(x)
  #define VERBOSE_PRINTHEXLN(x)
  #define VERBOSE_PRINTDEC(x)
  #define VERBOSE_PRINTLN(x)
  #define VERBOSE_ARRAY(x,l)
  #define VERBOSE_PRINTMILLIS(s)
#endif
