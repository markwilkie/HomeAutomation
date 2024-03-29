// globals for serial/setup handling
char charRead;

void printSetupCommands()
{
  Serial.println("=====================");
  Serial.println("Commands:");
  Serial.println("  '?' --> print this message");
  Serial.println("  'p' --> print current status");
  Serial.println("  'z', then '0-3' to zero calibrate a specific ADC sensor.  E.g. 'c2'   (0-low pwr, 1-solar, 2-inverter, 3-starter)");
  Serial.println("  'c', then '0-3', then current amp reading to calibrate preceded by an 'a'.  E.g. 'c0a10.6' to cal adc 0 at 10.6A");
  Serial.println("  'v', then current voltage to calibrate.  E.g. 'v12.4'");
  Serial.println("");  
}


//All commands are one char
void readChar() 
{

  if(Serial.available() > 0) 
  {
    //Read char, then if recognized, parse
    charRead = Serial.read();

    switch(charRead)
    {
      case 'p':
      {
        printStatus();
        break;
      }
      case '?':
      {
        printSetupCommands();
        break;
      }
      case 'z':
      {
        int adcNum = Serial.parseInt();
        if(adcNum>=0 && adcNum<=3)
        {
          Serial.print("Calibrating ADC: ");
          Serial.println(adcNum); 
          precADCList.calibrateADC(adcNum);  
        }
        else
        {
          Serial.print("ERROR: Invalid ADC Num: ");
          Serial.println(adcNum);          
        }
        break;            
      }
      case 'c':
      {
        int adcNum = Serial.parseInt();
        if(adcNum>=0 && adcNum<=3)
        {
          Serial.read();   //reads 'a' 
          long milliAmps = Serial.parseFloat()*1000;  

          Serial.print("Calibrating ADC ");
          Serial.print(adcNum); 
          Serial.print(" for ");
          Serial.print(milliAmps);
          Serial.print("ma");

          precADCList.calibrateADC(adcNum,milliAmps);  
        }
        else
        {
          Serial.print("ERROR: Invalid ADC Num: ");
          Serial.println(adcNum);          
        }
        break;            
      }      
      case 'v':
      {
        float currentVoltage = Serial.parseFloat();   
        if(currentVoltage>10 && currentVoltage<18)
        {
          battery.calibrateVoltage(currentVoltage);   
        }
        else
        {
          Serial.print("ERROR: Invalid Voltage: ");
          Serial.println(currentVoltage);            
        }
        break;
      }
      default:
      {
        Serial.print("ERROR: unrecognized input: ");
        Serial.println(charRead);
        break;
      }
    }
  }

  //Dump any extra chars
  while(Serial.available())
  {
  }
}
