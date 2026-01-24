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

void Screen::addTouchCallback(touchCallBackTemplate cbTemplate)
{
    touchCallBack=cbTemplate;
}

void Screen::addLongTouchCallback(longTouchCallBackTemplate cbTemplate)
{
	longTouchCallBack=cbTemplate;
}

void Screen::resetDisplay()
{
  lcd.init();                      // Hardware reset + full init
  lcd.setRotation(3);              // Re-apply rotation after reset
  lcd.fillScreen(TFT_BLACK);       // Clear any garbage from brownout
  lcd.setBrightness(STND_BRIGHTNESS);
  lcd.display();                   // Enable display output
  lcd.powerSave(false);            // Disable power save mode
}

void Screen::poll()
{
    //Brightness
    setBrightness();

    //Poll for touch
    static bool waitForFingerLift=false;
    static long startPressTime;
    if(isTouched())
    {
		if(!waitForFingerLift) 
		{
			waitForFingerLift=true;
            startPressTime=millis();
        }
    }
	else
	{
        //Meaning....have has the finger just lifted
        if(waitForFingerLift)
        {
            //Check for touch type
            if(millis()-startPressTime < LONG_TOUCH_TIME)
            {
                touchCallBack(touchX,touchY);    
            }
            else
            {
                longTouchCallBack(touchX,touchY);
            }            
        }        
		waitForFingerLift=false;
	}      
}

bool Screen::isTouched()
{
    //https://gist.github.com/sukesh-ak/610508bc84779a26efdcf969bf51a2d1#file-wt32-sc01-plus_esp32-s3-ino

    int32_t x=-1,y=-1;
    lcd.getTouch(&x, &y);
    if(x>=0 && y>=0)
    {
        touchX=x;
        touchY=y;
        return true;
    }
    else
    {
        return false;
    }
}

void Screen::setBrightness()
{
    //Check if we need to dim the screen back down or brighten up because of a touch
    if(currentBrightness < STND_BRIGHTNESS && isTouched())
    {
        setBrightness(STND_BRIGHTNESS);
    }
    else if(currentBrightness > DIM_BRIGHTNESS && millis()-brightTime > SCREEN_BRIGHT_TIME)
    {
        setBrightness(DIM_BRIGHTNESS);
    }
}

void Screen::setBrightness(int level)
{
    currentBrightness=level;
    lcd.setBrightness(level);
    brightTime=millis();
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

void Text::drawText(int x,int y,char*text,int font,int color)
{
    drawText(x,y,text,font,color,TFT_BLACK,false,false);
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

void Text::updateText(float value)
{
    drawText(lastX,lastY,value,lastDec,lastLabel,lastFont,lastColor,lastBgColor,lastRightFlag,lastCenterFlag);
}

void Text::updateText(int value)
{
    drawText(lastX,lastY,value,lastDec,lastLabel,lastFont,lastColor,lastBgColor,lastRightFlag,lastCenterFlag);
}

void Text::updateText(float value, int color)
{
    lastColor = color;
    drawText(lastX,lastY,value,lastDec,lastLabel,lastFont,lastColor,lastBgColor,lastRightFlag,lastCenterFlag);
}

void Text::updateText(int value, int color)
{
    lastColor = color;
    drawText(lastX,lastY,value,lastDec,lastLabel,lastFont,lastColor,lastBgColor,lastRightFlag,lastCenterFlag);
}

void Text::updateText(const char *text)
{
    drawText(lastX,lastY,text,lastFont,lastColor,lastBgColor,lastRightFlag,lastCenterFlag);
}

void Text::drawText(int x,int y,float value,int dec,const char*label,int font,int color,int bgColor,bool rightFlag,bool centerFlag)
{
    lcd.setTextFont(font);
    lcd.setTextSize(1);

    //round value and get it into a string
    char buf[20]; 
    if(value>=.05 || value<=-.05)
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

    lastDec=dec;
    lastLabel=label;    
    drawText(x,y,buf,font,color,bgColor,rightFlag,centerFlag);
}

void Text::drawText(int x,int y,const char*buf,int font,int color,int bgColor,bool rightFlag,bool centerFlag)
{
    //Determin length and heigth
    int textWidth=lcd.textWidth(buf);
    int textHeight=lcd.fontHeight(font);

    //Blank out last text (assuming a valid color)
    if(bgColor>=0)
    {
        if(lastRightFlag)
            lcd.fillRect(lastX-lastLen,lastY,lastLen,lastHeight,bgColor);
        else if(lastCenterFlag)
            lcd.fillRect(lastX-(lastLen/2),lastY,lastLen,lastHeight,bgColor);
        else 
            lcd.fillRect(lastX,lastY,lastLen,lastHeight,bgColor);
    }

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
    lastRightFlag=rightFlag; lastCenterFlag=centerFlag; 
    lastFont=font;  lastColor=color;  lastBgColor=bgColor;  
}

void Primitive::drawCircle(int x,int y,int r,int color,int fillColor)
{
    lastX=x;
    lastY=y;
    lastR=r;
    lastColor=color;
    lastFillColor=fillColor;

    //Draw circle, then fill it
    lcd.drawCircle(x,y,r,color);
    lcd.fillCircle(x,y,r-1,fillColor);
}

void Primitive::updateCircle(int fillColor)
{
    lcd.fillCircle(lastX,lastY,lastR-1,fillColor);
}