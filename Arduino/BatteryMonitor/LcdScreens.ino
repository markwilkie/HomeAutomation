#include "LcdScreens.h"

//Setup
void setupLCD()
{
  lcd.begin(16,2);  //16 x 2 lcd
  lcd.print("Starting...");
  backlightOn();;
}

void startLCD()
{
  loadBucketLabels();
  currentScreen=MAIN;
  printCurrentScreen();
}

void loadBucketLabels()
{
  bucketLabels[0]="Now";
  bucketLabels[1]="Min";
  bucketLabels[2]="Hr";
  bucketLabels[3]="Day";
  bucketLabels[4]="Mth";
}

//Screen 0 (main)
void mainScreen()
{ 
  numOfCursors=-1;  //no cursors for this screen
  lcd.clear();
    
  if(lastScreen!=0)
  {
    Serial.println("main screen");
    lastScreen=0;
    currentCursor=-1;
    lcd.noCursor();
  }

  //build line 1
  //  0123456789012345
  //  SoC>99.9 V>13.1 
  //  
  lcdPrint("SoC>",0,0);
  if(stateOfCharge<0){
    lcdPrint("--",4,0);
  }
  else {
    dtostrf(stateOfCharge, 3, 1, lcdScratch); 
    lcdPrint(lcdScratch,4,0);
  }

  lcdPrint("V>",9,0);
  dtostrf((batterymV*.001), 3, 1, lcdScratch); 
  lcdPrint(lcdScratch,11,0);

  //build line 2
  //  0123456789012345
  //  Ah>-99.9 Hr>99.9
  lcdPrint("Ah>",0,1);
  dtostrf((precADCList.getCurrent()*.001), 3, 1, lcdScratch);
  lcdPrint(lcdScratch,3,1);
  lcdPrint("Hr>",9,1);  
  double hoursRem=calcHoursRemaining(precADCList.getCurrent());
  if(hoursRem>99 || hoursRem<0)
  {
    lcdPrint("--",12,1);
  }
  else
  {
    dtostrf(hoursRem, 3, 1, lcdScratch);   
    lcdPrint(lcdScratch,12,1);  
  }
}

// screen 1
void netChargeScreen()
{
  numOfCursors=2; //Both ADC and time buckets for this screen
  lcd.clear();  

  if(lastScreen!=1)
  {
    Serial.println("Net Charge Screen");
    lastScreen=1;
    currentCursor=0;
  }

  //So we don't see the cursor moving
  lcd.noCursor();
  
  //build line 1
  //  Pnl>Now>-99.9
  //  0123456789012345
  lcdPrint(precADCList.getADC(currentADC)->label,0,0);
  lcdPrint(">",3,0);
  lcdPrint(bucketLabels[currentTimeBucket],4,0);
  lcdPrint(">",7,0);
  dtostrf((getADCTimeBucket(currentADC,currentTimeBucket)*.001), 3, 1, lcdScratch);
  lcdPrint(lcdScratch,8,0);

  //build line 2
  //(+)99.9 (-)99.9
  //0123456789012345
  lcdPrint("(+)",0,1);
  long currentTimeTotal=getTimeBucket(currentTimeBucket);  
  long currentTimeChargeTotal=getTimeChargeBucket(currentTimeBucket);
  dtostrf((currentTimeChargeTotal*.001), 3, 1, lcdScratch);
  lcdPrint(lcdScratch,3,1);
  lcdPrint("(-)",8,1);  
  dtostrf(((currentTimeTotal-currentTimeChargeTotal)*.001), 3, 1, lcdScratch);
  lcdPrint(lcdScratch,11,1);  
  
  //Set cursor
  if(currentCursor==0)
    lcd.setCursor(0,0);
  if(currentCursor==1)
    lcd.setCursor(4,0);
  lcd.cursor(); //turn cursor on    
}

//Screen 2
void statusScreen()
{ 
  numOfCursors=-1;  //no cursors for this screen
  lcd.clear();  

  if(lastScreen!=2)
  {
    Serial.println("status screen");
    lastScreen=2;
    currentCursor=-1;
    lcd.noCursor();
  }

  //build line 1
  //Rst>99999 T>99.9
  //0123456789012345

  lcdPrint("Rst>",0,0);
  lcdPrint(buildTimeLabel(currentTime-socReset,lcdScratch),4,0);
  lcdPrint("T>",10,0);
  dtostrf(temperatureRead(), 3, 1, lcdScratch); 
  lcdPrint(lcdScratch,12,0);

  //build line 2
  //Hz>9 Up>99999 
  //0123456789012345
  lcdPrint("Hz>",0,1);
  ltoa(hz, lcdScratch, 10);
  lcdPrint(lcdScratch,3,1);
  lcdPrint("Up>",5,1); 
  lcdPrint(buildTimeLabel(startTime,lcdScratch),8,1);
}

double getADCTimeBucket(int adcNum,int index)
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

double getTimeBucket(int index)
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

double getTimeChargeBucket(int index)
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
  Serial.println("Screen Cycle");

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
    backlightOn();
  }

  return retval;
}

void backlightOff()
{
  lcd.setBacklight(OFF);
}

void backlightOn()
{
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

