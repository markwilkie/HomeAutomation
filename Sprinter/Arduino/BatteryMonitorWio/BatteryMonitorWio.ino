#include <TFT_eSPI.h> // Hardware-specific library
#include <SPI.h>
#include <EasyTransfer.h>  //https://github.com/madsci1016/Arduino-EasyTransfer

#include "SerialPayload.h"
#include "LinearMeter.h"
#include "CircularMeter.h"

//Serial protocol for coms with master arduino
EasyTransfer scrSerial;
SERIAL_PAYLOAD_STRUCTURE scrPayload;

//Screen lib
TFT_eSPI tft = TFT_eSPI();

//Screen defines
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

//Meters
CircularMeter centerOutMeter;
CircularMeter centerInMeter;
LinearMeter voltageMeter;
LinearMeter waterMeter;

void setup(void) 
{
  //debug
  Serial.begin(115200);

  //Setup serial for coms with master
  Serial1.begin(115200);
  scrSerial.begin(details(scrPayload), &Serial1);

  //Setup screen
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  //Init meters
  centerOutMeter.initMeter(tft,0,15,SCREEN_WIDTH/2,SCREEN_HEIGHT/2,90,RED2GREEN); 
  centerInMeter.initMeter(tft,0,25,SCREEN_WIDTH/2,SCREEN_HEIGHT/2,70,GREEN2RED); 
  voltageMeter.drawMeter(tft,"V", 10, 15, 15, 4, 1, 20, 50, 150, 36);
  //waterMeter.drawMeter(tft,"Wtr", 0, 100, 100, 10, 3, 260, 50, 150, 36);
  waterMeter.drawMeter(tft,"Wtr", 10, 15, 100, 10, 2, 260, 50, 150, 36);
}

void loop() 
{
  //check and see if a data packet has come in. 
  if(scrSerial.receiveData())
  {
    //update center meter
    centerOutMeter.drawMeter(scrPayload.solarAh);
    centerInMeter.drawMeter(scrPayload.drawAh);

    //update linear meters
    voltageMeter.updatePointer(scrPayload.volts,2,1);
    waterMeter.updatePointer(scrPayload.volts,3,0);   //REMEMBER TO CHANGE SCALE BACK
  }
}


