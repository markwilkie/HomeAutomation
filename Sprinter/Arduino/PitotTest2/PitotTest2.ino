//
// Calculating airspeed using a pitot and MPXV7002DP
//
//For every 1V increase, there's an increase of 1 KPa per datasheet
//Or, 1241 per volt on a 12bit ADC at 3.3v
//....
//Pa = ((ADC/1241)*1000) where Pa is Pascals and ADC is the adc reading raw
//
//Density of Air at sealevel at 20c is 1.204 kg/m^3
//therefore...
//Velocity (m/s) = sqrt(2*(Pa/1.204))
//and...
//MPH = Velocity*2.23694
//
//Put together:
//MPH = (sqrt(2*(((ADC/1241)*1000)/1.204)))*2.23694
//
//or simplified to:
//MPH = 2.83977*sqrt(ADC/Density)   - raw ADC reading and density of air in kg/m^3
//
//~2.5V == 0mph
//~3.3V == 82mph
//~3.7v == 100mph
//
//We could cut the voltage in half with a voltage divider.  Since we're baselining "zero", no adjustments to the calculations should be necessary.  (need to work out exactly how we're going to calibrate zero......button on display??)
//
//Regarding calculating air density, 3K ft only makes about 5MPH difference, so it may not be worth it.  However, we do have air pressure available so.....
//

//Pin used for analog reads
#define PITOT_ADC_PIN 4
#define AIR_DENSITY 1.204

float g_reference_pressure;
float g_air_pressure;
float g_airspeed_mph = 0;


void setup()
{
  // Enable serial debug
  Serial.begin(115200);
  Serial.print("\n*** Airspeed starting\n");
 
  Serial.println("getting reference pressure");
  g_reference_pressure = analogRead(D2);  
  for (int i=1;i<=100;i++)
  {
    g_reference_pressure = (analogRead(D2))*0.25 + g_reference_pressure*0.75;
    delay(20);
  }
  
  g_air_pressure = g_reference_pressure;
}

void loop()
{  
  g_air_pressure = analogRead(D2)*0.25 + g_air_pressure*0.75;
  float pressure_diff = (g_air_pressure >= g_reference_pressure) ? (g_air_pressure - g_reference_pressure) : 0.0;  
  g_airspeed_mph = 2.83977*sqrt(pressure_diff/AIR_DENSITY);

  Serial.print(g_reference_pressure);
  Serial.print(" ");
  Serial.print(pressure_diff);
  Serial.print(" ");
  Serial.println(g_airspeed_mph);
  delay(500);
}