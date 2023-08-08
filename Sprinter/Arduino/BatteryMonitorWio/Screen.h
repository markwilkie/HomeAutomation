#ifndef SCREEN_H
#define SCREEN_H

#include <TFT_eSPI.h>
#include "LCDBackLight.hpp"

//Defines that are available to use  (this is reference only)
//WIO_5S_UP;
//WIO_5S_DOWN;
//WIO_5S_LEFT;
//WIO_5S_RIGHT;
//WIO_5S_PRESS;
//BUTTON_3;
//BUTTON_2;

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define STND_BACKLIGHT .70   //factor of max for standard
#define DIM_BACKLIGHT .05   //factor of max for dim  (how it normally sits)

#define SCREEN_BRIGHT_TIME 10000

extern TFT_eSPI tft;
extern LCDBackLight backLight;
extern bool simulatedData;

class Screen 
{
    private:
        unsigned long time;
        int stndBackLight;
        int dimBackLight;
        int currentBackLight;

        void setBrightness(int level);
        void handleButtonPresses();

    public:
        void init();
        void houseKeeping();
        void drawText(int x,int y,float value,int dec,const char *label,int fontSize);        
};

#endif