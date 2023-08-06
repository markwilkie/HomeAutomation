#include "CircularMeter.h"

// #########################################################################
//  Draw the meter on the screen, returns x coord of righthand side
// #########################################################################
void CircularMeter::initMeter(TFT_eSPI _tft,int _vmin,int _vmax,int _x,int _y,int _r,byte _scheme)
{
    //Set initial values
    tft=_tft;
    vmin=_vmin;
    vmax=_vmax;
    x=_x;
    y=_y;
    r=_r;
    scheme=_scheme;
}

void CircularMeter::drawMeter(int value)
{
    int text_colour = 0; // To hold the text colour
    int v = map(value, vmin, vmax, -CIR_METER_ANGLE, CIR_METER_ANGLE); // Map the value to an angle v

    // Draw colour blocks every inc degrees
    for (int i = -CIR_METER_ANGLE; i < CIR_METER_ANGLE; i += CIR_METER_SEG_INC) {

        // Choose colour from scheme
        int colour = 0;
        switch (scheme) {
        case 0: colour = TFT_RED; break; // Fixed colour
        case 1: colour = TFT_GREEN; break; // Fixed colour
        case 2: colour = TFT_BLUE; break; // Fixed colour
        case 3: colour = rainbow(map(i, -CIR_METER_ANGLE, CIR_METER_ANGLE, 0, 127)); break; // Full spectrum blue to red
        case 4: colour = rainbow(map(i, -CIR_METER_ANGLE, CIR_METER_ANGLE, 63, 127)); break; // Green to red (high temperature etc)
        case 5: colour = rainbow(map(i, -CIR_METER_ANGLE, CIR_METER_ANGLE, 127, 63)); break; // Red to green (low battery etc)
        default: colour = TFT_BLUE; break; // Fixed colour
        }

        // Calculate pair of coordinates for segment start
        float sx = cos((i - 90) * 0.0174532925);
        float sy = sin((i - 90) * 0.0174532925);
        uint16_t x0 = sx * (r - CIR_METER_RAD_WIDTH) + x;
        uint16_t y0 = sy * (r - CIR_METER_RAD_WIDTH) + y;
        uint16_t x1 = sx * r + x;
        uint16_t y1 = sy * r + y;

        // Calculate pair of coordinates for segment end
        float sx2 = cos((i + CIR_METER_SEG - 90) * 0.0174532925);
        float sy2 = sin((i + CIR_METER_SEG - 90) * 0.0174532925);
        int x2 = sx2 * (r - CIR_METER_RAD_WIDTH) + x;
        int y2 = sy2 * (r - CIR_METER_RAD_WIDTH) + y;
        int x3 = sx2 * r + x;
        int y3 = sy2 * r + y;

        if (i < v) { // Fill in coloured segments with 2 triangles
        tft.fillTriangle(x0, y0, x1, y1, x2, y2, colour);
        tft.fillTriangle(x1, y1, x2, y2, x3, y3, colour);
        //text_colour = colour; // Save the last colour drawn
        }
        else // Fill in blank segments
        {
        tft.fillTriangle(x0, y0, x1, y1, x2, y2, TFT_GREY);
        tft.fillTriangle(x1, y1, x2, y2, x3, y3, TFT_GREY);
        }
    }

/*
  // Convert value to a string
  char buf[10];
  byte len = 4; if (value > 999) len = 5;
  dtostrf(value, len, 0, buf);

  // Set the text colour to default
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  // Uncomment next line to set the text colour to the last segment value!
  //tft.setTextColor(text_colour, TFT_BLACK);
  
  // Print value, if the meter is large then use big font 6, othewise use 4
  if (r > 84) tft.drawCentreString(buf, x - 5, y - 20, 6); // Value in middle
  else tft.drawCentreString(buf, x - 5, y - 20, 4); // Value in middle

  // Print units, if the meter is large then use big font 4, othewise use 2
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (r > 84) tft.drawCentreString(units, x, y + 30, 4); // Units display
  else tft.drawCentreString(units, x, y + 5, 2); // Units display

  */
}


// #########################################################################
// Return a 16 bit rainbow colour
// #########################################################################
unsigned int CircularMeter::rainbow(byte value)
{
  // Value is expected to be in range 0-127
  // The value is converted to a spectrum colour from 0 = blue through to 127 = red

  byte red = 0; // Red is the top 5 bits of a 16 bit colour value
  byte green = 0;// Green is the middle 6 bits
  byte blue = 0; // Blue is the bottom 5 bits

  byte quadrant = value / 32;

  if (quadrant == 0) {
    blue = 31;
    green = 2 * (value % 32);
    red = 0;
  }
  if (quadrant == 1) {
    blue = 31 - (value % 32);
    green = 63;
    red = 0;
  }
  if (quadrant == 2) {
    blue = 0;
    green = 63;
    red = value % 32;
  }
  if (quadrant == 3) {
    blue = 0;
    green = 63 - 2 * (value % 32);
    red = 31;
  }
  return (red << 11) + (green << 5) + blue;
}

// #########################################################################
// Return a value in range -1 to +1 for a given phase angle in degrees
// #########################################################################
float CircularMeter::sineWave(int phase) 
{
  return sin(phase * 0.0174532925);
}
