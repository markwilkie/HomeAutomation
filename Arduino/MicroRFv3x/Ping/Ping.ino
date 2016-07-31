#include <NewPing.h>

NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);

char Ping()
{
   int uS = sonar.ping_median(10);
   int cm = uS / US_ROUNDTRIP_CM;

   VERBOSE_PRINT("Raw: "); 
   VERBOSE_PRINT(uS); 
   VERBOSE_PRINT(" "); 
   VERBOSE_PRINT("CM: "); 
   VERBOSE_PRINTLN(cm);

   if(cm<=MIN_THRESHOLD)
     return 'A';
   else
     return 'P';
}

