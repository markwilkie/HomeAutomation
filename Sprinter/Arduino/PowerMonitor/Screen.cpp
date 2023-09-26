#include "Screen.h"

void Screen::init()
{
    //Setup screen    
    lcd.begin();
    lcd.setRotation(3);
    lcd.fillScreen(TFT_BLACK);

    //Set 
    setBrightness(STND_BRIGHTNESS);
}

void Screen::houseKeeping()
{
    //Check if we need to dim the screen back down
    if(currentBrightness > DIM_BRIGHTNESS && millis()-time > SCREEN_BRIGHT_TIME)
    {
        setBrightness(DIM_BRIGHTNESS);
    }
}

void Screen::setBrightness(int level)
{
    currentBrightness=level;
    lcd.setBrightness(level);
    time=millis();
}

///////////////////////
//
// Text class
//
////////////////

void Text::drawText(int x,int y,float value,int dec,const char*label,int font)
{
    drawText(x,y,value,dec,label,font,false);
}

void Text::drawRightText(int x,int y,float value,int dec,const char*label,int font)
{
    drawText(x,y,value,dec,label,font,true);
}

void Text::drawText(int x,int y,float value,int dec,const char*label,int font,bool rightFlag)
{
    //Blank out last text
    if(lastRightFlag)
        lcd.fillRect(lastX-lastLen,lastY,lastX,lastHeight,TFT_BLACK);
    else
        lcd.fillRect(lastX,lastY,lastLen,lastHeight,TFT_BLACK);

    //round value and get it into a string
    char buf[20]; 
    if(value>0)
    {
        char valStr[10];
        dtostrf(value,2,dec,valStr);     

        //build string w/ label and display it
        sprintf(buf, "%s%s", valStr,label);
    }
    else
    {
        //If zero, just add dashes as it's not useful data
        sprintf(buf, "--%s",label);
    }

    //Determin length and heigth
    int textWidth=lcd.textWidth(buf);
    int textHeight=lcd.fontHeight(font);

    //Set last
    lastX=x; lastY=y; lastLen=textWidth; lastHeight=textHeight;
    lastRightFlag=rightFlag;

    //Ok print text
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    if(rightFlag)
        lcd.drawRightString(buf, x, y, font);
    else
        lcd.drawString(buf, x, y, font);
}