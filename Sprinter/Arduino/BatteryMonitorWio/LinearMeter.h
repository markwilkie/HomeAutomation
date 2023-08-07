#ifndef LINEARMETER_H
#define LINEARMETER_H

#include <TFT_eSPI.h>

#define TFT_GREY 0x5AEB

#define POINTER_WIDTH 16
#define POINTER_HEIGHT 10
#define METER_BORDER 3
#define METER_CAP_HEIGHT 19

extern TFT_eSPI tft;

class LinearMeter 
{
    private:
        
        int x,y;
        int vmin,vmax;
        int scale;
        int minorTicks;
        int height,width;
        int oldValue=-1;

    public:
        void drawMeter(char* label,int vmin, int vmax, int scale, int ticks, int majorTicks, int x, int y, int height, int width);
        void updatePointer(float value, int digits, int dec);
};

#endif