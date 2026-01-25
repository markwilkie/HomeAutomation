#ifndef SCREEN_H
#define SCREEN_H

/*
 * Screen - Low-level screen and touch driver wrapper
 * 
 * Wraps LovyanGFX for the WT32-SC01 Plus display (480x320 capacitive touch).
 * Handles touch detection, brightness control, and touch callbacks.
 * 
 * BRIGHTNESS LEVELS:
 * - FULL: Maximum brightness (255) - used rarely, high power draw
 * - STND: Standard brightness (178) - normal use after touch
 * - DIM:  Dim brightness (1) - idle state to save power
 * 
 * TOUCH HANDLING:
 * - Short touch: Triggers touchCallback after release
 * - Long touch: Triggers longTouchCallback after LONG_TOUCH_TIME held
 * - Any touch: Increases brightness to STND_BRIGHTNESS
 * - Auto-dim: Returns to DIM_BRIGHTNESS after SCREEN_BRIGHT_TIME
 * 
 * USAGE:
 *   screen.init();
 *   screen.addTouchCallback(myTouchHandler);
 *   screen.addLongTouchCallback(myLongTouchHandler);
 *   screen.poll();  // Call in loop() to process touch events
 */

#include <LovyanGFX.hpp>
#include "LGFX_ST32-SC01Plus.hpp"
#include "BitmapMeter.h"

// ============================================================================
// BRIGHTNESS CONFIGURATION
// ============================================================================
#define FULL_BRIGHTNESS 255   // Maximum (0-255) - high power draw
#define STND_BRIGHTNESS 178   // Standard use after touch
#define DIM_BRIGHTNESS 1      // Idle state to save power

// ============================================================================
// TOUCH TIMING
// ============================================================================
#define LONG_TOUCH_TIME 1000      // ms to hold for long touch event
#define SCREEN_BRIGHT_TIME 60000  // ms before auto-dimming (60 seconds)

extern LGFX lcd;           // Global LCD instance (used by Layout)
extern bool simulatedData; // Flag for testing with fake data

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
        void updateText(float value, int color);  //update with new color
        void updateText(int value, int color);  //update with new color

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