#include <TFT_eSPI.h> // Hardware-specific library
#include <SPI.h>
#include <EasyTransfer.h>  //https://github.com/madsci1016/Arduino-EasyTransfer

#include "Screen.h"
#include "SerialPayload.h"
#include "LinearMeter.h"
#include "CircularMeter.h"

//#define SIMULATED_VALUES

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
  //(TFT_eSPI,label,vmin,vmax,scale,ticks,majorTicks,x,y,height,width);
  int lx=10; int ly=60; int width=36;
  socMeter.drawMeter("SoC", 50, 100, 100, 9, 5, lx, ly, 150, width);
  waterMeter.drawMeter("Wtr", 0, 100, 100, 5, 3, SCREEN_WIDTH-width-lx, ly, 150, width);
}

void loop() 
{
  //Screen housekeeping (brightness, buttons, etc)
  screen.houseKeeping();

  //Load simulated values
  bool simulatedValuesFlag=false;
  #ifdef SIMULATED_VALUES
  simulatedValuesFlag=true;
  simulateValues();
  delay(1000);
  #endif

  //check and see if a data packet has come in. 
  if(scrSerial.receiveData() || simulatedValuesFlag)
  {
    //update center meter
    centerOutMeter.drawMeter(scrPayload.chargeAh);
    centerInMeter.drawMeter(scrPayload.drawAh);
    centerInMeter.drawText("Ah",scrPayload.chargeAh-scrPayload.drawAh);

    //update linear meters
    socMeter.updatePointer(scrPayload.stateOfCharge,2,0);
    waterMeter.updatePointer(scrPayload.stateOfWater,2,0);
  }
}

void simulateValues()
{
  scrPayload.stateOfCharge=random(50)+50;  // 50-->100
  scrPayload.stateOfWater=random(100);
  scrPayload.volts=((double)random(40)+100.0)/10.0;  // 10.0 --> 14.0
  scrPayload.chargeAh=((double)random(150))/10.0;  // 0 --> 15.0
  scrPayload.batteryHoursRem=((double)random(99))/10.0;   // 0 --> 99
  scrPayload.waterDaysRem=((double)random(10))/10.0;   // 0 --> 10
}


