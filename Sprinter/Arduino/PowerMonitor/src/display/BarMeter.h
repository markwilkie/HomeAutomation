#ifndef BARMETER_H
#define BARMETER_H

#include <LovyanGFX.hpp>

// Bar meter displays a vertical gauge with color-coded blocks
// 100% at top, 0% at bottom
class BarMeter
{
public:
    BarMeter();
    
    // Draw the initial meter with outline and blocks
    // x, y: top-left position
    // width, height: dimensions of the meter
    // outlineColor: color for the outer border
    // outlineWidth: thickness of the border
    void drawMeter(LGFX_Device* lcd, int x, int y, int width, int height, 
                   uint16_t outlineColor, int outlineWidth);
    
    // Update the meter to show a specific level (0-100)
    // level: percentage to display (0-100)
    void updateLevel(LGFX_Device* lcd, float level);
    
private:
    // Meter position and dimensions
    int _x;
    int _y;
    int _width;
    int _height;
    int _outlineWidth;
    uint16_t _outlineColor;
    
    // Block configuration
    static const int BLOCK_COUNT = 20;
    
    // Get color for a given percentage (0-100)
    uint16_t getColorForPercent(float percent);
    
    // Draw all blocks based on current level
    void drawBlocks(LGFX_Device* lcd, float level);
};

#endif
