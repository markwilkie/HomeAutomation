// globals for serial/setup handling
char charRead;

void printSetupCommands()
{
  Serial.println("=====================");
  Serial.println("Commands:");
  Serial.println("  '?' --> print this message");
  Serial.println("  'p' --> print current status");
  Serial.println("  'c', then '1-4' to calibrate a specific ADC sensor.  E.g. 'c2'   (1-low pwr, 2-solar, 3-inverter, 4-starter)");
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
      case 'c':
      {
        int adcNum = Serial.parseInt();
        if(adcNum>0 && adcNum<5)
        {
          adcNum--;
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
}


