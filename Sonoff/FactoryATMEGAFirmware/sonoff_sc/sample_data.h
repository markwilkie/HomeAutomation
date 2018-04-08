#ifndef   _SAMPLE_DATA_H_
#define   _SAMPLE_DATA_H_

#if ARDUINO >= 100
#include "Arduino.h"       // for delayMicroseconds, digitalPinToBitMask, etc
#else
#include "WProgram.h"      // for delayMicroseconds
#include "pins_arduino.h"  // for digitalPinToBitMask, etc
#endif
#include <inttypes.h>


void initDevice(void);
void getTempHumi(void);
void getDust(void);
void getAdcSensorValue(void);

int readAirSensor(void);
int readTemperature(void);
int readHumidity(void);

#endif
