#ifndef SCREEN_H
#define SCREEN_H

#include <LovyanGFX.hpp>
#include "LGFX_ST32-SC01Plus.hpp"

#define FULL_BRIGHTNESS 255   //(0-255)
#define STND_BRIGHTNESS 178   //factor of max for standard
#define DIM_BRIGHTNESS 12   //factor of max for dim  (how it normally sits)

#define SCREEN_BRIGHT_TIME 10000

extern LGFX lcd;
extern bool simulatedData;

class Screen 
{
    private:
        unsigned long time;
        int currentBrightness;

    public:
        void init();
        void setBrightness(int level);
        void houseKeeping();        
};

class Text 
{
    private:
        int lastX, lastY, lastLen, lastHeight;
        bool lastRightFlag;

        void drawText(int x,int y,float value,int dec,const char *label,int font,bool rightFlag);

    public:
        void drawText(int x,int y,float value,int dec,const char *label,int font);
        void drawRightText(int x,int y,float value,int dec,const char *label,int font);
};

#endif