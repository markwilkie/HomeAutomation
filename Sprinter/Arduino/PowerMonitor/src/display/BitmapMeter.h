#ifndef BITMAPMETER_H
#define BITMAPMETER_H

#include <LovyanGFX.hpp>
#include "LGFX_ST32-SC01Plus.hpp"
#include "bitmaps.h"

#define BATTERY_ORANGE 0xF641
#define WATER_BLUE 0x961E
#define BATTERY_FILL 0xeeac   //0xdde6
#define WATER_FILL 0x5cbb
#define WATER_RANGE 0xdfdf

struct BitmapConfig
{
    const unsigned char* bmArray;
    int x;
    int y;
    int width;
    int height;
    int color;
};

struct FillConfig
{
    int start;
    int height;
    int width;
    int color;
    int rangeColor=0;  //if 0, not used - used for two part gauges
    bool redraw=false;  //redraw after fill - like for lines etc
};

class BitmapMeter 
{
    public:
        void drawMeter(LGFX *_lcd,char* label,int vmin, int vmax, int scale, BitmapConfig *bitmapConfig, FillConfig *fillConfig);
        void updateLevel(float value, int digits, int dec);
        void updateLevel(float value, float rangeValue, int digits, int dec);

        BitmapConfig *getBitmapConfig();

    private:
        LGFX *lcd;
        BitmapConfig *bmcfg;
        FillConfig *fillcfg;
        char *label;
        int vmin,vmax;
        int scale;
        int oldValue=-1;
};

#endif