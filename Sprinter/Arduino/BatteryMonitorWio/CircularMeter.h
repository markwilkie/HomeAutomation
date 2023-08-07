#ifndef CIRCULARMETER_H
#define CIRCULARMETER_H

#include <TFT_eSPI.h>

// Meter colour schemes
#define RED2RED 0
#define GREEN2GREEN 1
#define BLUE2BLUE 2
#define BLUE2RED 3
#define GREEN2RED 4
#define RED2GREEN 5

#define TFT_GREY 0x5AEB // Dark grey 16 bit colour

//defaults
#define CIR_METER_RAD_WIDTH 15
#define CIR_METER_ANGLE 150 // Half the sweep angle of meter (300 degrees)
#define CIR_METER_SEG 5     // If segments are 5 degrees wide = 60 segments for 300 degrees
#define CIR_METER_SEG_INC 5 // Draw segments every 5 degrees, increase to 10 for segmented ring

extern TFT_eSPI tft;

class CircularMeter 
{
    private:
        int x,y,r;
        int vmin, vmax;
        byte scheme;

        unsigned int rainbow(byte value);
        float sineWave(int phase) ;

    public:
        void initMeter(int _vmin, int _vmax, int _x, int _y, int _r, byte _scheme);
        void drawMeter(int value);
        void drawText(const char* label,int value);
};

#endif