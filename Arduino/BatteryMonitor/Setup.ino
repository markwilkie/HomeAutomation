// globals for serial/setup handling
char mode='d'; //default mode
char charRead;

void printSetupCommands()
{
  Serial.println("=====================");
  Serial.println("Commands:");
  Serial.println("  '?' --> print this message");
  Serial.println("  'p' --> print current status");
  Serial.println("  'c', then '0-3' to calibrate a specific sensor");
  Serial.println("");
  
  Serial.println(" Modes:");
  Serial.println("  'd' --> default mode (no mode)");
  Serial.println("  'c' --> waiting for 0-3 to calibrate");
  Serial.println("");
}


//All commands are one char
void readChar() 
{
    if (Serial.available() > 0) 
    {
        //Read the char we received
        charRead = Serial.read();
  
        //Read top level commands
        if(mode=='d')
        {
          if(charRead=='p')
            printStatus();
          if(charRead=='?')
            printSetupCommands();
          if(charRead=='c')
          {
            Serial.println("=====================");
            Serial.println("Type ADC number: (0-low pwr, 1-solar, 2-inverter, 3-starter)");
            mode='c';     
          }   
        }
        else
        {
          //Check modes 

          //calibration mode
          if(mode=='c')
          {
            int adcNum=((int)charRead)-48;
            Serial.print("Calibrating ADC# "); Serial.println(adcNum);            
            if(adcNum>=0 && adcNum<=3)
              calibrateADC(adcNum);
          } 
  
          //reset mode now that we're done
          mode='d';        
        }
    }
}


