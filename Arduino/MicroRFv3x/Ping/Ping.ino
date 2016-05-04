#include <NewPing.h>

NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);

char Ping()
{
   int uS = sonar.ping_median(10);
   int cm = uS / US_ROUNDTRIP_CM;

   VERBOSE_PRINT("CM: "); 
   VERBOSE_PRINTLN(cm);

   if(cm==0)
     return 'A';
   else
     return 'P';
}

