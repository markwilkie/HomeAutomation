#ifndef SCREEN_H
#define SCREEN_H

#include <LovyanGFX.hpp>
#include "LGFX_ST32-SC01Plus.hpp"
#include "BitmapMeter.h"

#define FULL_BRIGHTNESS 255   //(0-255)
#define STND_BRIGHTNESS 178   //factor of max for standard
#define DIM_BRIGHTNESS 1      //factor of max for dim  (how it normally sits)

#define LONG_TOUCH_TIME 1000

#define SCREEN_BRIGHT_TIME 60000

extern LGFX lcd;
extern bool simulatedData;

typedef void (*touchCallBackTemplate)( int x,int y );
typedef void (*longTouchCallBackTemplate)(  int x,int y  );

class Screen 
{
    private:
        touchCallBackTemplate touchCallBack;
        longTouchCallBackTemplate longTouchCallBack;
        unsigned long brightTime;
        int currentBrightness;

    public:
        void init();
        void poll();
        void setBrightness(int level);
        void setBrightness(); 
        void resetDisplay();
        bool isTouched();
        void addTouchCallback(touchCallBackTemplate);
        void addLongTouchCallback(longTouchCallBackTemplate);

        int32_t touchX,touchY;       
};

class Text 
{
    public:
        void drawText(int x,int y,float value,int dec,const char *label,int font,int color);
        void drawText(int x,int y,char *text,int font,int color);
        void drawRightText(int x,int y,float value,int dec,const char *label,int font,int color);
        void drawCenterText(int x,int y,float value,int dec,const char *label,int font,int color);

        void updateText(const char *text);  //update using same parameters as last time
        void updateText(float value);  //update using same parameters as last time
        void updateText(int value);  //update using same parameters as last time

        void drawBitmapTextBottom(BitmapConfig *bmCfg,int offset,float value,int dec,const char *label,int font,int color);
        void drawBitmapTextTop(BitmapConfig *bmCfg,int offset, float value,int dec,const char *label,int font,int color);
        void drawBitmapTextCenter(BitmapConfig *bmCfg, float value,int dec,const char *label,int font,int color,int bgColor);

    private:
        int lastX, lastY, lastLen, lastHeight;
        int lastFont;
        int lastColor;
        int lastBgColor;
        int lastDec;
        const char *lastLabel;
        bool lastRightFlag;
        bool lastCenterFlag;

        void drawText(int x,int y,float value,int dec,const char *label,int font,int color,int bgColor,bool rightFlag,bool centerFlag); 
        void drawText(int x,int y,const char*buf,int font,int color,int bgColor,bool rightFlag,bool centerFlag);
};


class Primitive
{
    public:
        void drawCircle(int x,int y,int r,int color,int fillColor);
        void updateCircle(int fillColor);

    private:
        int lastX, lastY;
        int lastR;
        int lastColor,lastFillColor;
};

#endif