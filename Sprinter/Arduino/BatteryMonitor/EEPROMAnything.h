#include <EEPROM.h>
#include <Arduino.h>  // for type definitions

//EEPROM setup values
#define EEPROM_PRECADC_ADDR 0
#define EEPROM_VOLTAGE_ADDR 1024
#define EEPROM_ADCFACTOR_ADDR 2048

template <class T> int EEPROM_writeAnything(int ee, const T& value)
{
    const byte* p = (const byte*)(const void*)&value;
    unsigned int i;
    for (i = 0; i < sizeof(value); i++)
          EEPROM.write(ee++, *p++);
    return i;
}

template <class T> int EEPROM_readAnything(int ee, T& value)
{
    byte* p = (byte*)(void*)&value;
    unsigned int i;
    for (i = 0; i < sizeof(value); i++)
          *p++ = EEPROM.read(ee++);
    return i;
}
