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

/*
 * resetDisplay() - Full display hardware reset for brownout recovery
 * 
 * When the ESP32 experiences a brownout (voltage dip), the display
 * can end up in a corrupted state with garbage pixels or wrong settings.
 * 
 * This function performs a complete reset sequence:
 * 1. lcd.init() - Hardware reset and full display initialization
 * 2. setRotation(3) - Re-apply landscape orientation
 * 3. fillScreen(BLACK) - Clear any garbage pixels
 * 4. setBrightness() - Restore brightness level
 * 5. display() - Enable display output
 * 6. powerSave(false) - Ensure display is not in power save mode
 * 
 * Triggered by long-press on center screen region.
 */
void Screen::resetDisplay()
{
  lcd.init();                      // Hardware reset + full init
  lcd.setRotation(3);              // Re-apply rotation after reset
  lcd.fillScreen(TFT_BLACK);       // Clear any garbage from brownout
  lcd.setBrightness(STND_BRIGHTNESS);
  lcd.display();                   // Enable display output
  lcd.powerSave(false);            // Disable power save mode
}

/*
 * poll() - Main screen polling handler, call from Arduino loop()
 * 
 * Handles two responsibilities:
 * 1. Screen brightness management (auto-dim after timeout)
 * 2. Touch detection and callback dispatch
 * 
 * TOUCH STATE MACHINE:
 * The touch detection uses a "wait for finger lift" pattern:
 * 
 * State: waitForFingerLift=false (idle)
 *   -> Touch detected: Set waitForFingerLift=true, record startPressTime
 * 
 * State: waitForFingerLift=true (finger down)
 *   -> Touch still detected: Do nothing, wait
 *   -> Touch released (finger up):
 *        - If duration < LONG_TOUCH_TIME: Call touchCallback (short tap)
 *        - If duration >= LONG_TOUCH_TIME: Call longTouchCallback (long press)
 *        - Reset waitForFingerLift=false
 * 
 * This pattern ensures callbacks fire on RELEASE, not on initial touch,
 * and allows distinguishing between short and long presses.
 */
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

/*
 * setBrightness() - Auto-manage screen brightness based on activity
 * 
 * Called every poll() to handle automatic dimming:
 * 
 * BRIGHTNESS LEVELS:
 * - STND_BRIGHTNESS: Normal viewing brightness (set on touch)
 * - DIM_BRIGHTNESS: Power-saving dim level (after timeout)
 * 
 * LOGIC:
 * - If screen is dim AND touch detected -> Brighten to STND_BRIGHTNESS
 * - If screen is bright AND timeout exceeded -> Dim to DIM_BRIGHTNESS
 * 
 * SCREEN_BRIGHT_TIME controls how long screen stays bright after last touch.
 * This saves power during long periods of no interaction.
 */
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

/*
 * Text::drawText() - Render text with automatic background clearing
 * 
 * The core text rendering function that handles:
 * 1. Building display string from value + label
 * 2. Clearing the previous text area (prevents ghosting)
 * 3. Drawing new text with proper alignment
 * 4. Saving state for subsequent updateText() calls
 * 
 * PARAMETERS:
 * @param x, y - Position (interpretation depends on flags)
 * @param value - Numeric value to display
 * @param dec - Decimal places for formatting
 * @param label - Unit suffix ("A", "V", "%", etc.)
 * @param font - LovyanGFX font number (2=small, 4=medium)
 * @param color - Text foreground color
 * @param bgColor - Background color for clearing (TFT_BLACK typically)
 * @param rightFlag - If true, x is RIGHT edge of text
 * @param centerFlag - If true, x is CENTER of text
 * 
 * ZERO HANDLING:
 * Values near zero (|value| < 0.05) display as "--" + label
 * since near-zero readings are usually noise, not useful data.
 */
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