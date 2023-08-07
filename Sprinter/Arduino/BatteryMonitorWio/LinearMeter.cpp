#include "LinearMeter.h"


// #########################################################################
//  Draw a linear meter on the screen
// #########################################################################
void LinearMeter::drawMeter(char* label, int _vmin, int _vmax, int _scale, int _minorTicks, int _majorTicks, int _x, int _y, int _height, int _width)
{
    //Set initial values
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
    tft.fillRect(x + METER_BORDER, y + METER_CAP_HEIGHT, _width - (METER_BORDER*2), height - (METER_CAP_HEIGHT*2), TFT_WHITE);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawCentreString(label, x + _width / 2, y + 2, 2);

    int usableHeight=height-(METER_CAP_HEIGHT*2)-POINTER_HEIGHT;
    int tickStart=METER_CAP_HEIGHT+(POINTER_HEIGHT/2);
    int tickSpacing=usableHeight/(minorTicks-1);    

    //Draw minor ticks
    for (int i = 0; i < minorTicks; i++) {
        tft.drawFastHLine(x + (width*.55), y + tickStart+(tickSpacing*i), (width*.16), TFT_BLACK);
    }

    //Draw major ticks
    int ticksPerMajor=(minorTicks-1)/(_majorTicks-1);
    for (int i = 0; i <_majorTicks; i++) {
        tft.drawFastHLine(x + (width*.55), y + tickStart+(ticksPerMajor*i*tickSpacing), (width*.25), TFT_BLACK);
    }    

    tft.drawCentreString("-", x + _width / 2, y + height - 18, 2);
}

// #########################################################################
//  Adjust linear meter pointer positions
// #########################################################################
void LinearMeter::updatePointer(float value, int digits, int dec)
{
    int mappedValue=map(value,vmin,vmax,0,scale);

    int usableHeight=height-(METER_CAP_HEIGHT*2)-POINTER_HEIGHT;
    int pointerStart=y + (height-METER_CAP_HEIGHT-(POINTER_HEIGHT/2)) - 2;
  
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

    float factor = usableHeight/(float)scale;
    int yValue = value*factor;

    while (!(yValue == oldValue)) 
    {
        dy = pointerStart - oldValue;   //187 + 100 - old_value;
        if (oldValue > yValue) 
        {
            tft.drawLine(dx, dy - (POINTER_HEIGHT/2), dx + POINTER_WIDTH, dy, TFT_WHITE);
            oldValue--;
            tft.drawLine(dx, dy + (POINTER_HEIGHT/2) + 1, dx + POINTER_WIDTH, dy + 1, TFT_RED);
        }
        else 
        {
            tft.drawLine(dx, dy + (POINTER_HEIGHT/2), dx + POINTER_WIDTH, dy, TFT_WHITE);
            oldValue++;
            tft.drawLine(dx, dy - (POINTER_HEIGHT/2) + 1, dx + POINTER_WIDTH, dy - 1, TFT_RED);
        }
    }
}
