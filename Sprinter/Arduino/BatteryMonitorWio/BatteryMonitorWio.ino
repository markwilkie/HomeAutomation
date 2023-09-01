#include <TFT_eSPI.h> // Hardware-specific library
#include <SPI.h>
#include <EasyTransfer.h>  //https://github.com/madsci1016/Arduino-EasyTransfer

#include "Screen.h"
#include "SerialPayload.h"
#include "LinearMeter.h"
#include "CircularMeter.h"

//Serial protocol for coms with master arduino
EasyTransfer scrSerial;
SERIAL_PAYLOAD_STRUCTURE scrPayload;

//Screen lib
TFT_eSPI tft = TFT_eSPI();
LCDBackLight backLight;

//Screen
Screen screen;

//Meters
CircularMeter centerOutMeter;
CircularMeter centerInMeter;
LinearMeter socMeter;
LinearMeter waterMeter;

//Dynamic text
Text battHoursLeft;
Text waterDaysLeft;
Text volts;
Text hertz;

//Globals
bool simulatedData=false;
int loopCntr=0;
long hzTimer=millis();
int hz=0;

void setup(void) 
{
    //debug
    Serial.begin(115200);

    //Setup serial for coms with master
    Serial1.begin(115200);
    scrSerial.begin(details(scrPayload), &Serial1);

    //Setup screen
    screen.init();

    //Init circular meters
    int cx=SCREEN_WIDTH/2;
    int cy=(SCREEN_HEIGHT/2)+10;
    centerOutMeter.initMeter(0,20,cx,cy,90,RED2GREEN); 
    centerInMeter.initMeter(0,20,cx,cy,70,GREEN2RED); 

    //Linear meters
    //(label,vmin,vmax,scale,ticks,majorTicks,x,y,height,width);
    int lx=10; int ly=30; int width=36;
    socMeter.drawMeter("SoC", 50, 100, 100, 9, 5, lx, ly, 150, width);
    waterMeter.drawMeter("Wtr", 0, 100, 100, 5, 3, SCREEN_WIDTH-width-lx, ly, 150, width);

    //Static labels
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Sprinkle Data",80, 5, 4);
    //tft.drawString("Rem",10, 200, 2);
    //tft.drawString("Rem",265, 200, 2);

    //init
    hzTimer=millis();
}

void loop() 
{
   //Screen housekeeping (brightness, buttons, etc)
  screen.houseKeeping();

  //Load simulated values
  if(simulatedData)
  {
    loadSimulateValues();
    delay(500);    
  }

  //check and see if a data packet has come in. 
  if(scrSerial.receiveData() || simulatedData)
  {
    //Calc hz
    loopCntr++;
    if(millis()>(hzTimer+1000))
    {
      hz=loopCntr;
      loopCntr=0;
      hzTimer=millis();
    }    

    //update center meter
    centerOutMeter.drawMeter(scrPayload.chargeAh);
    centerInMeter.drawMeter(scrPayload.drawAh);
    centerInMeter.drawText("Ah",scrPayload.chargeAh-scrPayload.drawAh);

    //update linear meters
    socMeter.updatePointer(scrPayload.stateOfCharge,2,0);
    waterMeter.updatePointer(scrPayload.stateOfWater,2,0);

    //update text values
    volts.drawText(125,200,scrPayload.volts,1,"V",4);
    battHoursLeft.drawText(10,190,scrPayload.batteryHoursRem,1," Hrs",2);
    waterDaysLeft.drawRightText(SCREEN_WIDTH-10,190,scrPayload.waterDaysRem,1," Dys",2);
    hertz.drawRightText(SCREEN_WIDTH-10,220,hz,0,"Hz",2);
  }
}

void loadSimulateValues()
{
  scrPayload.stateOfCharge=random(50)+50;  // 50-->100
  scrPayload.stateOfWater=random(3);
  if(scrPayload.stateOfWater==0)  scrPayload.stateOfWater=20;
  if(scrPayload.stateOfWater==1)  scrPayload.stateOfWater=50;
  if(scrPayload.stateOfWater==2)  scrPayload.stateOfWater=90;
  scrPayload.volts=((double)random(40)+100.0)/10.0;  // 10.0 --> 14.0
  scrPayload.chargeAh=((double)random(200))/10.0;  // 0 --> 20.0
  scrPayload.drawAh=((double)random(200))/10.0;  // 0 --> 20.0
  scrPayload.batteryHoursRem=((double)random(99))/10.0;   // 0 --> 99
  scrPayload.waterDaysRem=((double)random(10))/10.0;   // 0 --> 10
}


