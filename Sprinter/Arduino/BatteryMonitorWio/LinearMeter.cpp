#include "LinearMeter.h"


// #########################################################################
//  Draw a linear meter on the screen
// #########################################################################
void LinearMeter::drawMeter(TFT_eSPI _tft,char* label, int _vmin, int _vmax, int _scale, int _minorTicks, int _majorTicks, int _x, int _y, int _height, int _width)
{
    //Set initial values
    tft=_tft;
    x=_x;
    y=_y;
    vmin=_vmin;
    vmax=_vmax;
    scale=_scale;
    minorTicks=_minorTicks;
    height=_height;
    width=_width;
    oldValue=-1;

    //Draw meter itself
    tft.drawRect(x, y, _width, height, TFT_GREY);
    tft.fillRect(x + METER_BORDER, y + METER_CAP_HEIGTH, _width - (METER_BORDER*2), height - (METER_CAP_HEIGTH*2), TFT_WHITE);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawCentreString(label, x + _width / 2, y + 2, 2);

    int usableHeigth=height-(METER_CAP_HEIGTH*2);
    int tickSpacing=usableHeigth/(minorTicks+1);
    int adjustment=(usableHeigth-(tickSpacing*(minorTicks+2)))/2;  // to handle rounding errors
    for (int i = 1; i < minorTicks-0; i++) {
        tft.drawFastHLine(x + (_width*.55), y + (METER_CAP_HEIGTH+adjustment+tickSpacing+(tickSpacing*i)), (_width*.16), TFT_BLACK);
    }

    int majorTickSpacing=(minorTicks/(_majorTicks+1))*tickSpacing;
    for (int i = 0; i <_majorTicks+2; i++) {
        tft.drawFastHLine(x + (_width*.55), y + (METER_CAP_HEIGTH+adjustment+tickSpacing+(majorTickSpacing*i)), (_width*.25), TFT_BLACK);
    }

    //tft.fillTriangle(x + 3, y + 127, x + 3 + 16, y + 127, x + 3, y + 127 - 5, TFT_RED);
    //tft.fillTriangle(x + 3, y + 127, x + 3 + 16, y + 127, x + 3, y + 127 + 5, TFT_RED);

    tft.drawCentreString("-", x + _width / 2, y + height - 18, 2);
}

// #########################################################################
//  Adjust linear meter pointer positions
// #########################################################################
void LinearMeter::updatePointer(float value, int digits, int dec)
{
    int mappedValue=map(value,vmin,vmax,0,scale);

    int usableHeigth=height-(METER_CAP_HEIGTH*2);
    int tickSpacing=usableHeigth/(minorTicks+1);
    int adjustment=(usableHeigth-(tickSpacing*(minorTicks+2)))/2;  // to handle rounding errors
  
    int dy;
    int dx = METER_BORDER+x;

    //draw value
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    char buf[8]; dtostrf(value, digits, dec, buf);
    tft.drawCentreString(buf, x + width / 2, y + height - 18, 2);

    if (value < 0) {
        value = 0;    // Limit value to emulate needle end stops
    }
    if (value > scale) {
        value = scale;
    }

    float factor = (usableHeigth-(tickSpacing*2))/(float)scale;
    int yValue = value*factor;

    while (!(yValue == oldValue)) 
    {
        dy = y + height-(METER_CAP_HEIGTH*2) + tickSpacing + adjustment - oldValue; 
        if (oldValue > yValue) 
        {
            tft.drawLine(dx, dy - 5, dx + POINTER_WIDTH, dy, TFT_WHITE);
            oldValue--;
            tft.drawLine(dx, dy + 6, dx + POINTER_WIDTH, dy + 1, TFT_RED);
        }
        else 
        {
            tft.drawLine(dx, dy + 5, dx + POINTER_WIDTH, dy, TFT_WHITE);
            oldValue++;
            tft.drawLine(dx, dy - 6, dx + POINTER_WIDTH, dy - 1, TFT_RED);
        }
    }
}
