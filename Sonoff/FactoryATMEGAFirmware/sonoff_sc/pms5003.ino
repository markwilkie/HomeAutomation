
struct pms5003data {
  uint16_t framelen;
  uint16_t pm10_standard, pm25_standard, pm100_standard;
  uint16_t pm10_env, pm25_env, pm100_env;
  uint16_t particles_03um, particles_05um, particles_10um, particles_25um, particles_50um, particles_100um;
  uint16_t unused;
  uint16_t checksum;
};

struct pms5003data data;
    
int readAirSensor() {
  if (readPMSdata(&pmsSerial)) {
    // reading data was successful!
    //Serial.println();
    Serial.println("---------------------------------------");
    //Serial.println("Concentration Units (standard)");
    //Serial.print("PM 1.0: "); Serial.print(data.pm10_standard);
    //Serial.print("\t\tPM 2.5: "); Serial.print(data.pm25_standard);
    //Serial.print("\t\tPM 10: "); Serial.println(data.pm100_standard);
    //Serial.println("---------------------------------------");
    //Serial.println("Concentration Units (environmental)");
    //Serial.print("PM 1.0: "); Serial.print(data.pm10_env);
    Serial.print("\t\tPM 2.5: "); Serial.print(data.pm25_env);
    Serial.print("\t\tPM 10: "); Serial.println(data.pm100_env);
   // Serial.println("---------------------------------------");
    //Serial.print("Particles > 0.3um / 0.1L air:"); Serial.println(data.particles_03um);
    //Serial.print("Particles > 0.5um / 0.1L air:"); Serial.println(data.particles_05um);
    //Serial.print("Particles > 1.0um / 0.1L air:"); Serial.println(data.particles_10um);
    //Serial.print("Particles > 2.5um / 0.1L air:"); Serial.println(data.particles_25um);
    //Serial.print("Particles > 5.0um / 0.1L air:"); Serial.println(data.particles_50um);
    //Serial.print("Particles > 50 um / 0.1L air:"); Serial.println(data.particles_100um);
    //Serial.println("---------------------------------------");

    //Combine pm2.5 and pm10 into one value so we don't have to change the esp protocol
    int combValue=0;
    combValue=convert(data.pm25_env)*100;
    combValue=combValue+convert(data.pm100_env);
    Serial.print("\t\tComb: "); Serial.println(combValue);

    return combValue;
  }
}

int convert(int reading)
{
  int convertedValue=-1;

  //uses index from https://blissair.com/what-is-pm-2-5.htm

  if(reading>=0 && reading<=6)
    convertedValue=0;
  if(reading>=7 && reading<=12)
    convertedValue=1;
  if(reading>=13 && reading<=20)
    convertedValue=2;
  if(reading>=21 && reading<=27)
    convertedValue=3;    
  if(reading>=28 && reading<=35)
    convertedValue=4;
  if(reading>=36 && reading<=43)
    convertedValue=5;
  if(reading>=44 && reading<=55)
    convertedValue=6;    
  if(reading>=56 && reading<=106)
    convertedValue=7;
  if(reading>=107 && reading<=150)
    convertedValue=8;
  if(reading>=151 && reading<=200)
    convertedValue=9;    
  if(reading>=201)
    convertedValue=10;

  return convertedValue;
}

boolean readPMSdata(Stream *s) {
  if (! s->available()) {
    return false;
  }
  
  // Read a byte at a time until we get to the special '0x42' start-byte
  if (s->peek() != 0x42) {
    s->read();
    return false;
  }

  // Now read all 32 bytes
  if (s->available() < 32) {
    return false;
  }
    
  uint8_t buffer[32];    
  uint16_t sum = 0;
  s->readBytes(buffer, 32);

  // get checksum ready
  for (uint8_t i=0; i<30; i++) {
    sum += buffer[i];
  }

  /* debugging
  for (uint8_t i=2; i<32; i++) {
    Serial.print("0x"); Serial.print(buffer[i], HEX); Serial.print(", ");
  }
  Serial.println();
  */
  
  // The data comes in endian'd, this solves it so it works on all platforms
  uint16_t buffer_u16[15];
  for (uint8_t i=0; i<15; i++) {
    buffer_u16[i] = buffer[2 + i*2 + 1];
    buffer_u16[i] += (buffer[2 + i*2] << 8);
  }

  // put it into a nice struct :)
  memcpy((void *)&data, (void *)buffer_u16, 30);

  if (sum != data.checksum) {
    Serial.println("Checksum failure");
    return false;
  }
  // success!
  return true;
}

