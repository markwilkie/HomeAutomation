// Quick test: reads differential pressure sensor (Sensata P1J) at I2C 0x28
// Prints raw count, pascals, and calculated mph every 200ms to Serial

#include <Wire.h>
#include <math.h>

#define PRESSURE_I2C_ADDRESS 0x28
#define MIN_COUNT  1638   // 0%
#define MAX_COUNT  14746  // 100%
#define PASCAL_RANGE 1250
#define AIR_DENSITY  1.225
#define MS_2_MPH     2.23694

void setup()
{
  Serial.begin(115200);
  Wire.begin();  // default SDA=21, SCL=22
  delay(500);
  Serial.println("Pitot differential pressure sensor test");
  Serial.println("count,pascals,mph");
}

void loop()
{
  Wire.requestFrom(PRESSURE_I2C_ADDRESS, 2);
  if (Wire.available() != 2)
  {
    Serial.println("ERROR: I2C read failed (no bytes)");
    delay(500);
    return;
  }

  int count = Wire.read() * 256;
  count    += Wire.read();

  if (count & 0xC000)
  {
    Serial.printf("ERROR: status bits set (0x%04X)\n", count);
    delay(500);
    return;
  }

  // Convert to pascals
  float pascals = map(count, MIN_COUNT, MAX_COUNT, 0, PASCAL_RANGE * 10) / 10.0;
  if (pascals < 0) pascals = 0;

  // Convert to mph (Bernoulli)
  float mph = sqrt((2.0 * pascals) / AIR_DENSITY) * MS_2_MPH;

  Serial.printf("count=%d\tPa=%.1f\tmph=%.1f\n", count, pascals, mph);
  delay(200);
}
