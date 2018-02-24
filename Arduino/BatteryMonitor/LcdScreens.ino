#include "LcdScreens.h"

//Setup
void setupLCD()
{
  lcd.begin(16,2);  //16 x 2 lcd
  lcd.print("Starting...");

  //create the custom chars
  lcd.createChar(0, bulk);
  lcd.createChar(1, floatToBulk);
  lcd.createChar(2, notToBulk);
  lcd.createChar(3, upDown);
  lcd.createChar(4, bulkToFloat);
  lcd.createChar(5, bulkToNot);
  lcd.createChar(6, multUpDown);

  //turn backlight on
  backlightOn();;
}

void startLCD()
{
  currentScreen=MAIN;
  printCurrentScreen();
}

//Screen 0 (main)
void mainScreen()
{ 
  numOfCursors=-1;  //no cursors for this screen
  lcd.noCursor();

  //Remember last values so we're only converting and updating on change
  static double lastStateOfCharge=-999;
  static double lastVolts=-999;
  static long lastCurrent=-999;
  double long lastHoursRem=-999;  

  //Do this only when coming in from another screen
  if(lastScreen!=MAIN)
  {
    //Init new screen
    lcd.clear();
    Serial.println("Loading main screen");
    lastScreen=MAIN;
    currentCursor=-1;

    //Write labels only once so we're not flashing on refresh
    lcdPrint("SoC>",0,0);
    lcdPrint("V>",9,0);    
    lcdPrint("Ah>",0,1);    
    lcdPrint("Hr>",9,1);  

    //Reset 
    lastStateOfCharge=-999;
    lastVolts=-999;
    lastCurrent=-999;
    lastHoursRem=-999;          
  }
  
  //build line 1
  //  0123456789012345
  //  SoC>99.9 V>13.1 
  // 
  double stateOfCharge=battery.getSoC();
  if(stateOfCharge!=lastStateOfCharge)
  { 
    if(stateOfCharge<0){
      lcdPrint("--   ",4,0);
    }
    else if(stateOfCharge>100){
      lcdPrint("*100 ",4,0);
    }
    else {
      dtostrf(stateOfCharge, 3, 1, lcdScratch); 
      lcdPrint("     ",4,0);
      lcdPrint(lcdScratch,4,0);
    }
  }
  double volts=battery.getVolts();
  if(volts!=lastVolts)
  {
    dtostrf(volts, 3, 1, lcdScratch); 
    lcdPrint(lcdScratch,11,0);
  }

  //build line 2
  //  0123456789012345
  //  Ah>-99.9 Hr>99.9

  long current=precADCList.getCurrent();
  if(current!=lastCurrent)
  {
    dtostrf((current*.001), 3, 1, lcdScratch);
    lcdPrint("      ",3,1);
    lcdPrint(lcdScratch,3,1);
  }

  double hoursRem=battery.getHoursRemaining(current);
  if(hoursRem!=lastHoursRem)
  {
    if(hoursRem>99 || hoursRem<0) {
      lcdPrint("--  ",12,1);
    }
    else {
      dtostrf(hoursRem, 3, 1, lcdScratch);   
      lcdPrint("    ",12,1);
      lcdPrint(lcdScratch,12,1);  
    }
  }

  //Set last to new, current values for next time around
  lastStateOfCharge=stateOfCharge;
  lastVolts=volts;
  lastCurrent=current;
  lastHoursRem=hoursRem;  
}

// screen 1
void sumScreen()
{
  numOfCursors=2; //Both ADC and time buckets for this screen
  lcd.noCursor();

  //Remember last values so we're only converting and updating on change
  static int lastCurrentADC=-999;  
  static int lastCurrentTimeBucket=-999;
  static long lastBucketTotal=-999;
  static long lastCurrentTimeTotal=-999;
  static long lastCurrentTimeChargeTotal=-999;

  //Only do this if coming from another screen
  if(lastScreen!=SUM)
  {
    lcd.clear(); 
    Serial.println("Loading Sum Screen");
    lastScreen=SUM;
    currentCursor=0;

    //Print static labels
    lcdPrint("\366",0,0); //sigma symbol (
    lcdPrint("    >",1,0);    
    lcdPrint(">",9,0);
    lcdPrint("(+)",0,1);
    lcdPrint("(-)",8,1);     

    //Reset last values
    lastCurrentADC=-999;  
    lastCurrentTimeBucket=-999;
    lastBucketTotal=-999;
    lastCurrentTimeTotal=-999;
    lastCurrentTimeChargeTotal=-999;  

    //Set current time bucket to '1' as we don't care about "now"
    currentTimeBucket=1;
  }

  //build line 1
  //  * Pnl>Now>-99.9
  //  0123456789012345
  if(currentADC!=lastCurrentADC){
    lcdPrint(precADCList.getADC(currentADC)->label,2,0);
  }
  if(currentTimeBucket!=lastCurrentTimeBucket) {
    lcdPrint(bucketLabels[currentTimeBucket],6,0);
  }
  long bucketTotal=getADCTimeSumBucket(currentADC,currentTimeBucket);
  if(bucketTotal!=lastBucketTotal)
  {
    if(bucketTotal>10) //to add decimal or not
      dtostrf(bucketTotal*.001, 5, 0, lcdScratch);
    else
      dtostrf(bucketTotal*.001, 3, 1, lcdScratch);
    lcdPrint("      ",10,0);
    lcdPrint(lcdScratch,10,0);
  }

  //build line 2
  //(+)99.9 (-)99.9
  //0123456789012345
  long currentTimeChargeTotal=getTimeChargeSumBucket(currentTimeBucket);    
  if(currentTimeChargeTotal!=lastCurrentTimeChargeTotal)
  { 
    dtostrf((currentTimeChargeTotal*.001), 3, 1, lcdScratch);
    lcdPrint("     ",3,1);
    lcdPrint(lcdScratch,3,1);
  }
  long currentTimeTotal=getTimeSumBucket(currentTimeBucket); 
  if(currentTimeTotal!=lastCurrentTimeTotal || currentTimeChargeTotal!=lastCurrentTimeChargeTotal)
  {
    dtostrf(((currentTimeTotal-currentTimeChargeTotal)*-.001), 3, 1, lcdScratch);
    lcdPrint("     ",11,1); 
    lcdPrint(lcdScratch,11,1);  
  }
  
  //Set cursor
  if(currentCursor==0)
    lcd.setCursor(2,0);
  if(currentCursor==1)
    lcd.setCursor(6,0);
  lcd.cursor(); //turn cursor on    

  //Set last for next time
  lastCurrentADC=currentADC;
  lastCurrentTimeBucket=currentTimeBucket;
  lastBucketTotal=bucketTotal;
  lastCurrentTimeTotal=currentTimeTotal;
  lastCurrentTimeChargeTotal=currentTimeChargeTotal;  
}

// screen 2
void netChargeScreen()
{
  numOfCursors=2; //Both ADC and time buckets for this screen
  lcd.noCursor();

  //Remember last values so we're only converting and updating on change
  static int lastCurrentADC=-999;  
  static int lastCurrentTimeBucket=-999;
  static long lastBucketTotal=-999;
  static long lastCurrentTimeTotal=-999;
  static long lastCurrentTimeChargeTotal=-999;

  //Only do this if coming from another screen
  if(lastScreen!=NET_CHARGE)
  {
    lcd.clear(); 
    Serial.println("Loading Net Charge Screen");
    lastScreen=NET_CHARGE;
    currentCursor=0;

    //Print static labels
    lcdPrint("\344",0,0); //mean symbol 
    lcdPrint("    >",1,0);      
    lcdPrint(">",9,0);
    lcdPrint("(+)",0,1);
    lcdPrint("(-)",8,1);     

    //Reset last values
    lastCurrentADC=-999;  
    lastCurrentTimeBucket=-999;
    lastBucketTotal=-999;
    lastCurrentTimeTotal=-999;
    lastCurrentTimeChargeTotal=-999;     
  }

  //build line 1
  //  * Pnl>Now>-99.9
  //  0123456789012345
  if(currentADC!=lastCurrentADC){
    lcdPrint(precADCList.getADC(currentADC)->label,2,0);
  }
  if(currentTimeBucket!=lastCurrentTimeBucket) {
    lcdPrint(bucketLabels[currentTimeBucket],6,0);
  }
  long bucketTotal=getADCTimeAvgBucket(currentADC,currentTimeBucket);
  if(bucketTotal!=lastBucketTotal)
  {
    dtostrf(bucketTotal*.001, 3, 1, lcdScratch);
    lcdPrint("     ",10,0);
    lcdPrint(lcdScratch,10,0);
  }

  //build line 2
  //(+)99.9 (-)99.9
  //0123456789012345
  long currentTimeChargeTotal=getTimeChargeAvgBucket(currentTimeBucket);    
  if(currentTimeChargeTotal!=lastCurrentTimeChargeTotal)
  { 
    dtostrf((currentTimeChargeTotal*.001), 3, 1, lcdScratch);
    lcdPrint("     ",3,1);
    lcdPrint(lcdScratch,3,1);
  }
  long currentTimeTotal=getTimeAvgBucket(currentTimeBucket); 
  if(currentTimeTotal!=lastCurrentTimeTotal || currentTimeChargeTotal!=lastCurrentTimeChargeTotal)
  {
    dtostrf(((currentTimeTotal-currentTimeChargeTotal)*-.001), 3, 1, lcdScratch);
    lcdPrint("     ",11,1); 
    lcdPrint(lcdScratch,11,1);  
  }
  
  //Set cursor
  if(currentCursor==0)
    lcd.setCursor(2,0);
  if(currentCursor==1)
    lcd.setCursor(6,0);
  lcd.cursor(); //turn cursor on    

  //Set last for next time
  lastCurrentADC=currentADC;
  lastBucketTotal=bucketTotal;
  lastCurrentTimeBucket=currentTimeBucket;
  lastCurrentTimeTotal=currentTimeTotal;
  lastCurrentTimeChargeTotal=currentTimeChargeTotal;  
}

//Screen 4
void statusScreen()
{ 
  numOfCursors=-1;  //no cursors for this screen
  lcd.noCursor();

  //Only do this if coming from a different screen
  if(lastScreen!=STATUS)
  {
    lcd.clear();      
    Serial.println("Loading status screen");
    lastScreen=STATUS;
    currentCursor=-1;

    //print static labels
    lcdPrint("Rst>",0,0);
    lcdPrint("T>",10,0);
    lcdPrint("Hz>",0,1);
    lcdPrint("Up>",5,1); 
  }

  //build line 1
  //Rst>99999 T>99.9
  //0123456789012345
  lcdPrint(buildTimeLabel(currentTime-battery.getSoCReset(),lcdScratch),4,0);
  dtostrf(temperatureRead(), 3, 1, lcdScratch); 
  lcdPrint(lcdScratch,12,0);

  //build line 2
  //Hz>9 Up>99999 
  //0123456789012345
  ltoa(hz, lcdScratch, 10);
  lcdPrint(lcdScratch,3,1);
  lcdPrint(buildTimeLabel(startTime,lcdScratch),8,1);
}

double getADCTimeAvgBucket(int adcNum,int index)
{
  double retValue=0.0;
  
  if(index==0)
    retValue=precADCList.getADC(adcNum)->getCurrent();
  if(index==1)
    retValue=precADCList.getADC(adcNum)->getLastMinuteAvg();
  if(index==2)
    retValue=precADCList.getADC(adcNum)->getLastHourAvg();
  if(index==3)
    retValue=precADCList.getADC(adcNum)->getLastDayAvg();
  if(index==4)
    retValue=precADCList.getADC(adcNum)->getLastMonthAvg();  

  return retValue;
}

double getADCTimeSumBucket(int adcNum,int index)
{
  double retValue=0.0;

  if(index==0)
    retValue=precADCList.getCurrent();  
  if(index==1)
    retValue=precADCList.getADC(adcNum)->getLastMinuteSum();
  if(index==2)
    retValue=precADCList.getADC(adcNum)->getLastHourSum();
  if(index==3)
    retValue=precADCList.getADC(adcNum)->getLastDaySum();
  if(index==4)
    retValue=precADCList.getADC(adcNum)->getLastMonthSum();  

  return retValue;
}

double getTimeAvgBucket(int index)
{
  double retValue=0.0;
  
  if(index==0)
    retValue=precADCList.getCurrent();
  if(index==1)
    retValue=precADCList.getLastMinuteAvg();
  if(index==2)
    retValue=precADCList.getLastHourAvg();
  if(index==3)
    retValue=precADCList.getLastDayAvg();
  if(index==4)
    retValue=precADCList.getLastMonthAvg();  

  return retValue;
}

double getTimeSumBucket(int index)
{
  double retValue=0.0;

  if(index==0)
    retValue=precADCList.getCurrent();  
  if(index==1)
    retValue=precADCList.getLastMinuteSum();
  if(index==2)
    retValue=precADCList.getLastHourSum();
  if(index==3)
    retValue=precADCList.getLastDaySum();
  if(index==4)
    retValue=precADCList.getLastMonthSum();  

  return retValue;
}

double getTimeChargeAvgBucket(int index)
{
  double retValue=0.0;

  if(index==0)
    retValue=precADCList.getCurrentCharge();
  if(index==1)
    retValue=precADCList.getLastMinuteChargeAvg();
  if(index==2)
    retValue=precADCList.getLastHourChargeAvg();
  if(index==3)
    retValue=precADCList.getLastDayChargeAvg();
  if(index==4)
    retValue=precADCList.getLastMonthChargeAvg();  

  return retValue;
}

double getTimeChargeSumBucket(int index)
{
  double retValue=0.0;

  if(index==0)
    retValue=precADCList.getCurrentCharge();  
  if(index==1)
    retValue=precADCList.getLastMinuteChargeSum();
  if(index==2)
    retValue=precADCList.getLastHourChargeSum();
  if(index==3)
    retValue=precADCList.getLastDayChargeSum();
  if(index==4)
    retValue=precADCList.getLastMonthChargeSum();  

  return retValue;
}

//Called multiple times a second
void handleButtonPresses()
{
  //read buttons
  buttonPressed=lcdReadButton();
  if(buttonPressed==0)
  {
    readChar();  //maybe the user is using the serial interface
    //return;
  }
  
  switch (buttonPressed) 
  {
  case LEFT:
  case RIGHT:
    changeScreen();
    break;
  case DOWN:
  case UP:
    if(currentCursor==ADC_CURS)
      changeADC();
    if(currentCursor==TIME_BUCKET_CURS)
      changeTimeBucket();
    break;
  case SELECT:
    changeCursor();
    break;
  }

  //Screens are already printed every second in the main loop
}

void changeScreen()
{
  //Reset cursor
  currentCursor=-1;
  
  //Lef or right?
  int incr=1;
  if(buttonPressed==LEFT)
    incr=-1;

  //Set current screen
  currentScreen=currentScreen+incr;
  if(currentScreen>=TOTAL_SCREENS)
    currentScreen=0;
  if(currentScreen<0)
    currentScreen=(TOTAL_SCREENS-1);

  Serial.print("Screen Cycled by ");Serial.print(incr);Serial.print(" resulting in ");Serial.println(currentScreen);    
}

void changeCursor()
{ 
  if(numOfCursors > 0)
  {
    currentCursor++;
    if(currentCursor>=numOfCursors)
      currentCursor=0;
  }

  Serial.print("Cursor Cycle: "); Serial.println(currentCursor);  
}

//cycle through ADCs based on up/down button
void changeADC()
{ 
  //Up or down?
  int incr=1;
  if(buttonPressed==UP)
    incr=-1;

  //Set current ADC
  currentADC=currentADC+incr;
  if(currentADC>=ADC_COUNT)
    currentADC=0;
  if(currentADC<0)
    currentADC=(ADC_COUNT-1);

  Serial.print("ADC Cycle: "); Serial.println(currentADC);    

  //Ok, refrest current screen now that we've updated the current ADC
  printCurrentScreen();
}

void changeTimeBucket()
{   
  //Up or down?
  int incr=1;
  if(buttonPressed==UP)
    incr=-1;

  //Set current ADC
  currentTimeBucket=currentTimeBucket+incr;
  if(currentTimeBucket>=TIME_BUCKET_COUNT)  
    currentTimeBucket=0;
  if(currentTimeBucket<0)
    currentTimeBucket=(TIME_BUCKET_COUNT-1);

  Serial.print("Bucket Cycle: "); Serial.println(currentTimeBucket);    

  //Ok, refrest current screen now that we've updated the current ADC
  printCurrentScreen();
}


//prints the current screen to LCD
void printCurrentScreen()
{
  //Serial.println("Refreshing LCD");
  switch(currentScreen)
  {
    case MAIN:
      mainScreen();
      break;
    case SUM:
      sumScreen();
      break;      
    case NET_CHARGE:
      netChargeScreen();
      break;
    case STATUS:
      statusScreen();
      break;      
  }
}

void lcdPrint(char *stringToPrint)
{
  lcd.clear();
  lcdPrint(stringToPrint,0,0);
}

void lcdPrint(char *stringToPrint,int col,int line)
{
  lcd.setCursor(col,line);
  lcd.print(stringToPrint);
}

// return values: 0=none, 1=up, 2=down, 3=right, 4=left, 5=select
int lcdReadButton()
{
  int retval=0;

  //Read buttons
  uint8_t buttons = lcd.readButtons();
  
  if (buttons & BUTTON_UP) {
    retval=UP;
  }
  if (buttons & BUTTON_DOWN) {
    retval=DOWN;
  }
  if (buttons & BUTTON_LEFT) {
    retval=LEFT;
  }
  if (buttons & BUTTON_RIGHT) {
    retval=RIGHT;
  }
  if (buttons & BUTTON_SELECT) {
    retval=SELECT;
  }  

  //Turn backlight on if any key was pressed
  if(retval!=0)
  {
    //If the backlight is off, swallow the button keypress
    if(!backlight)
      retval=0;

    //Regardless, make sure backlight stays on
    backlightOn();
  }

  return retval;
}

void backlightOff()
{
  backlight=false;
  lcd.setBacklight(OFF);
}

void backlightOn()
{
  backlight=true;
  backlightOnTime=currentTime;
  lcd.setBacklight(WHITE);
}

void toggleBacklight()
{
  Serial.println("Toggling backlight");
    
  if(backlight)
    lcd.setBacklight(OFF);
  else
    lcd.setBacklight(WHITE);

  backlight=!backlight;
}

