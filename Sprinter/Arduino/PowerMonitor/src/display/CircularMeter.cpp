#include "CircularMeter.h"

// #########################################################################
//  Draw the meter on the screen, returns x coord of righthand side
// #########################################################################
void CircularMeter::initMeter(LGFX *_lcd,int _vmin,int _vmax,int _x,int _y,int _r,byte _scheme)
{
  //Set initial values
  lcd=_lcd;
  vmin=_vmin;
  vmax=_vmax;
  x=_x;
  y=_y;
  r=_r;
  scheme=_scheme;
}

/*
 * drawMeter() - Render the circular arc meter at current value
 * 
 * Draws a segmented arc where filled segments represent the current value.
 * Uses triangle-based rendering for smooth appearance.
 * 
 * RENDERING APPROACH:
 * The arc is divided into segments (CIR_METER_SEG_INC degrees each).
 * For each segment:
 * 1. Calculate inner and outer edge coordinates using cos/sin
 * 2. If segment is below current value: Fill with color from scheme
 * 3. If segment is above current value: Fill with grey (empty)
 * 
 * COLOR SCHEMES:
 * 0-2: Solid colors (red, green, blue)
 * 3: Full rainbow spectrum (blue to red)
 * 4: GREEN2RED - Value increases with temperature (green->yellow->red)
 * 5: RED2GREEN - Value increases as level drops (red->yellow->green, for battery)
 * 
 * PARAMETERS:
 * @param value - Current value to display (mapped to arc position)
 * 
 * The arc spans from -CIR_METER_ANGLE to +CIR_METER_ANGLE degrees,
 * with 0 degrees pointing up (12 o'clock position).
 */
void CircularMeter::drawMeter(int value)
{
  int text_colour = 0; // To hold the text colour
  int v = map(value, vmin, vmax, -CIR_METER_ANGLE, CIR_METER_ANGLE); // Map the value to an angle v

  // Draw colour blocks every inc degrees
  for (int i = -CIR_METER_ANGLE; i < CIR_METER_ANGLE; i += CIR_METER_SEG_INC) 
  {
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
      lcd->fillTriangle(x0, y0, x1, y1, x2, y2, colour);
      lcd->fillTriangle(x1, y1, x2, y2, x3, y3, colour);
      //text_colour = colour; // Save the last colour drawn
      }
      else // Fill in blank segments
      {
      lcd->fillTriangle(x0, y0, x1, y1, x2, y2, TFT_GREY);
      lcd->fillTriangle(x1, y1, x2, y2, x3, y3, TFT_GREY);
      }
  }
}

void CircularMeter::drawText(const char* label,int value)
{
  //Draw value and label
  char buf[8]; 
  sprintf(buf, "%d", value);
  lcd->fillRect(x-30,y-30,50,35,TFT_BLACK);
  lcd->setTextColor(TFT_WHITE);
  lcd->drawCentreString(buf, x - 5, y - 20, 4);
  lcd->drawCentreString(label, x, y + 5, 2);
}

int CircularMeter::getY()
{
  return y;
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
