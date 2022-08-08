
#include "PMS5003.h"

PMS5003 pmsSensor;
 
void setup() {
  // our debugging output
  Serial.begin(115200);
}
  
void loop() 
{
  if (pmsSensor.readPMSData()) 
  {
    // reading data was successful!

    Serial.println();
    Serial.println("---------------------------------------");
    Serial.println("Concentration Units (standard)");
    Serial.print("PM 1.0: "); Serial.print(pmsSensor.getpm10_standard());
    Serial.print("\t\tPM 2.5: "); Serial.print(pmsSensor.getpm25_standard());
    Serial.print("\t\tPM 10: "); Serial.println(pmsSensor.getpm100_standard());
    Serial.println("---------------------------------------");
    Serial.println("Concentration Units (environmental)");
    Serial.print("PM 1.0: "); Serial.print(pmsSensor.getpm10_env());
    Serial.print("\t\tPM 2.5: "); Serial.print(pmsSensor.getpm25_env());
    Serial.print("\t\tPM 10: "); Serial.println(pmsSensor.getpm100_env());
    Serial.println("---------------------------------------");
    Serial.print("Particles > 0.3um / 0.1L air:"); Serial.println(pmsSensor.getParticles03um());
    Serial.print("Particles > 0.5um / 0.1L air:"); Serial.println(pmsSensor.getParticles05um());
    Serial.print("Particles > 1.0um / 0.1L air:"); Serial.println(pmsSensor.getParticles10um());
    Serial.print("Particles > 2.5um / 0.1L air:"); Serial.println(pmsSensor.getParticles25um());
    Serial.print("Particles > 5.0um / 0.1L air:"); Serial.println(pmsSensor.getParticles50um());
    Serial.print("Particles > 10.0 um / 0.1L air:"); Serial.println(pmsSensor.getParticles100um());
    Serial.println("---------------------------------------");
  }

  delay(5000);
}
 
