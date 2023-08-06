#ifndef LINEARMETER_H
#define LINEARMETER_H

#include <TFT_eSPI.h>

#define TFT_GREY 0x5AEB

#define POINTER_WIDTH 16
#define POINTER_HEIGTH 20
#define METER_BORDER 3
#define METER_CAP_HEIGTH 19

class LinearMeter 
{
    private:
        TFT_eSPI tft;
        int x,y;
        int vmin,vmax;
        int scale;
        int minorTicks;
        int height,width;
        int oldValue=-1;

    public:
        void drawMeter(TFT_eSPI tft,char* label,int vmin, int vmax, int scale, int ticks, int majorTicks, int x, int y, int height, int width);
        void updatePointer(float value, int digits, int dec);
};

#endif