#include "PressureSensor.h"

PressureSensor sensor;

void setup()
{
  Serial.begin(115200);
  Serial.println(__FILE__);

  sensor.begin();
  delay(500);
  sensor.calibrate();
}


void loop()
{
  int state = sensor.readSpeed();
  if (state == I2C_OK)
  {
    Serial.print(" Pascal:\t");
    Serial.println(sensor.getPascal());
    Serial.print(" MPH:\t");
    Serial.println(sensor.getMPH());    
    Serial.println();
  }
  else Serial.println("error");

  delay(1000);
}


//  -- END OF FILE --
