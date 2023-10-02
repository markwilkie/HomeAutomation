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

void Text::drawText(int x,int y,float value,int dec,const char*label,int font,int color)
{
    drawText(x,y,value,dec,label,font,color,TFT_BLACK,false,false);
}

void Text::drawRightText(int x,int y,float value,int dec,const char*label,int font,int color)
{
    drawText(x,y,value,dec,label,font,color,TFT_BLACK,true,false);
}

void Text::drawCenterText(int x,int y,float value,int dec,const char*label,int font,int color)
{
    drawText(x,y,value,dec,label,font,color,TFT_BLACK,false,true);
}

void Text::drawBitmapTextBottom(BitmapConfig *bmCfg,int offset,float value,int dec,const char*label,int font,int color)
{
    int x=bmCfg->x+(bmCfg->width/2);
    int y=bmCfg->y+bmCfg->height+offset;
    drawText(x,y,value,dec,label,font,color,TFT_BLACK,false,true);
}

void Text::drawBitmapTextTop(BitmapConfig *bmCfg,int offset,float value,int dec,const char*label,int font,int color)
{
    int x=bmCfg->x+(bmCfg->width/2);
    int y=bmCfg->y-offset;
    drawText(x,y,value,dec,label,font,color,TFT_BLACK,false,true);
}

void Text::drawBitmapTextCenter(BitmapConfig *bmCfg,float value,int dec,const char*label,int font,int color,int bgColor)
{
    int x=bmCfg->x + (bmCfg->width/2);
    int y=bmCfg->y + (bmCfg->height/2);
    drawText(x,y,value,dec,label,font,color,bgColor,false,true);
}

void Text::drawText(int x,int y,float value,int dec,const char*label,int font,int color,int bgColor,bool rightFlag,bool centerFlag)
{
    lcd.setTextFont(font);
    lcd.setTextSize(1);

    //Blank out last text (if we've got a background color)
    if(bgColor>=0)
    {
        if(lastRightFlag)
            lcd.fillRect(lastX-lastLen,lastY,lastX,lastHeight,bgColor);
        else if(lastCenterFlag)
            lcd.fillRect(lastX-(lastLen/2),lastY,lastLen,lastHeight,bgColor);
        else 
            lcd.fillRect(lastX,lastY,lastLen,lastHeight,bgColor);
    }

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

    //Ok print text
    lcd.setTextColor(color);
    if(rightFlag)
        lcd.drawRightString(buf, x, y, font);
    else if(centerFlag)
        lcd.drawCenterString(buf, x, y, font);
    else
        lcd.drawString(buf, x, y, font);    

    //Set last
    lastX=x; lastY=y; lastLen=textWidth; lastHeight=textHeight;
    lastRightFlag=rightFlag;
    lastCenterFlag=centerFlag;            
}