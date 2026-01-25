#ifndef CIRCULARMETER_H
#define CIRCULARMETER_H

/*
 * CircularMeter - Arc-style gauge for displaying charge/draw amps
 * 
 * Renders a semicircular arc meter that sweeps based on value.
 * Used in center of main screen to show current power flow.
 * 
 * TWO METERS OVERLAID:
 * - Outer meter (centerOutMeter): Shows charging amps (GREEN2RED scale)
 * - Inner meter (centerInMeter): Shows draw amps (RED2GREEN scale)
 * 
 * COLOR SCHEMES:
 * - RED2GREEN: Low values red, high values green (good for "more is better")
 * - GREEN2RED: Low values green, high values red (good for warnings)
 * - Single colors: RED2RED, GREEN2GREEN, BLUE2BLUE for uniform color
 * 
 * USAGE:
 *   CircularMeter meter;
 *   meter.initMeter(&lcd, 0, 20, centerX, centerY, 90, GREEN2RED);
 *   meter.drawMeter(currentAmps);
 *   meter.drawText("A", netAmps);  // Label in center
 */

#include <LovyanGFX.hpp>
#include "LGFX_ST32-SC01Plus.hpp"

// Meter colour schemes - determines gradient direction
#define RED2RED 0       // Solid red
#define GREEN2GREEN 1   // Solid green
#define BLUE2BLUE 2     // Solid blue
#define BLUE2RED 3      // Cool to hot
#define GREEN2RED 4     // Good to bad (for draw/consumption)
#define RED2GREEN 5     // Bad to good (for charging)

#define TFT_GREY 0x5AEB // Dark grey 16 bit colour for inactive segments

// Arc geometry defaults
#define CIR_METER_RAD_WIDTH 15  // Width of the arc stroke
#define CIR_METER_ANGLE 150     // Half sweep angle (300° total sweep)
#define CIR_METER_SEG 5         // Segment width in degrees
#define CIR_METER_SEG_INC 5     // Draw every N degrees (5 = smooth, 10 = segmented)

class CircularMeter 
{
    private:
        LGFX *lcd;
        int x,y,r;
        int vmin, vmax;
        byte scheme;

        unsigned int rainbow(byte value);
        float sineWave(int phase) ;

    public:
        void initMeter(LGFX *_lcd,int _vmin, int _vmax, int _x, int _y, int _r, byte _scheme);
        void drawMeter(int value);
        void drawText(const char* label,int value);
        int getY();
};

#endif