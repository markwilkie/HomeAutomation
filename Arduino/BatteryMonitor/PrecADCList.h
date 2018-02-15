#include "PrecADC.h"
#include <EEPROM.h>
#include "EEPROMAnything.h"

class PrecADCList 
{
private:
  //Get precision ADC's going for each circuit
  PrecADC lowPowerPanel = PrecADC(0,GAIN_TWOTHIRDS, 2500, .066, "Aux",-1); //(66mv/A)
  PrecADC solarPanel = PrecADC(1,GAIN_TWOTHIRDS, 2500, .066, "Slr",1);
  PrecADC inverter = PrecADC(2,GAIN_TWOTHIRDS, 2500, .023, "Inv",1); //.025 or 25mv/A (another project says .023)
  PrecADC starterBattery = PrecADC(3,GAIN_TWOTHIRDS, 2500, .023, "Str",1);

  //array to hold all four in a known order
  #define ADC_COUNT 4
  PrecADC *adcList[ADC_COUNT] = {&lowPowerPanel,&solarPanel,&inverter,&starterBattery};

public:
 PrecADCList(); 
 void begin(); //starts stuff up and inits buffer
 void read(); //read all buffers
 void add();  //calls add on each buffer  
 void calibrateADC(int adcNum);  //assumes no amp flow, then calibrates to zero using offset
 PrecADC *getADC(int adcNum); 

 //return values in milli amps as a sum of the avg for all buffers
 long getCurrent();  
 long getLastMinuteAvg();  
 long getLastHourAvg(); 
 long getLastDayAvg();  
 long getLastMonthAvg(); 

 //return values in milli amps as a sum of avg for all buffers - charge
 long getCurrentCharge();  
 long getLastMinuteChargeAvg();  
 long getLastHourChargeAvg(); 
 long getLastDayChargeAvg();  
 long getLastMonthChargeAvg();  

 //return values in milli amps as a sum of all buffers
 long getLastMinuteSum();  
 long getLastHourSum(); 
 long getLastDaySum();  
 long getLastMonthSum(); 

 //return values in milli amps as a sum of all buffers - charge
 long getLastMinuteChargeSum();  
 long getLastHourChargeSum(); 
 long getLastDayChargeSum();  
 long getLastMonthChargeSum();   

 void printStatus();
};
