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
}