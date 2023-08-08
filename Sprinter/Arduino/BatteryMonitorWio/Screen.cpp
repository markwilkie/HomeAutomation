#include "Screen.h"

void Screen::init()
{
    //Init pins for buttons
    pinMode(WIO_5S_UP,INPUT);
    pinMode(WIO_5S_DOWN,INPUT);
    pinMode(WIO_5S_LEFT,INPUT);
    pinMode(WIO_5S_RIGHT,INPUT);
    pinMode(WIO_5S_PRESS,INPUT);
    pinMode(BUTTON_3,INPUT);
    pinMode(BUTTON_2,INPUT);
    pinMode(BUTTON_1,INPUT);

    //Setup screen    
    tft.begin();
    tft.setRotation(3);
    tft.fillScreen(TFT_BLACK);
    backLight.initialize();

    //Determine backlight ranges
    int maxBackLight=backLight.getMaxBrightness();
    stndBackLight=maxBackLight*STND_BACKLIGHT;
    dimBackLight=maxBackLight*DIM_BACKLIGHT;

    //Set 
    setBrightness(stndBackLight);
}

void Screen::houseKeeping()
{
    //Check if we need to dim the screen back down
    if(backLight.getBrightness() > dimBackLight && millis()-time > SCREEN_BRIGHT_TIME)
    {
        setBrightness(dimBackLight);
    }

    //Handle button presses
    handleButtonPresses();
}

void Screen::drawText(int x,int y,float value,int dec,const char*label,int fontSize)
{
    //round value and get it into a string
    char valStr[10];
    dtostrf(value,2,dec,valStr);

    //build string w/ label and display it
    char buf[20]; 
    sprintf(buf, "%s%s", valStr,label);
    int strLen=strlen(buf);
    tft.fillRect(x,y,(fontSize*3.4*strLen)+0,((fontSize*(3.1/(float)fontSize))*6.2)+0,TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(buf, x, y, fontSize);
}

void Screen::setBrightness(int level)
{
    backLight.setBrightness(level);
    time=millis();
}

void Screen::handleButtonPresses()
{
    //LOW is pressed
    if(!digitalRead(WIO_5S_UP))
        setBrightness(backLight.getBrightness()+10);

    if(!digitalRead(WIO_5S_DOWN))
        setBrightness(backLight.getBrightness()-10);

    if(!digitalRead(WIO_5S_LEFT))
        setBrightness(stndBackLight);

    if(!digitalRead(WIO_5S_RIGHT))
        setBrightness(stndBackLight);

    if(!digitalRead(WIO_5S_PRESS))
        setBrightness(dimBackLight);

    if(!digitalRead(BUTTON_3))
        setBrightness(backLight.getMaxBrightness());

    if(!digitalRead(BUTTON_2))   
        setBrightness(stndBackLight);

    if(!digitalRead(BUTTON_1))   
        setBrightness(dimBackLight);      

    //Simulated data     
    if(!digitalRead(BUTTON_1) && !digitalRead(BUTTON_3)) 
        simulatedData=true;
}