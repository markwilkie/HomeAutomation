#ifndef BARMETER_H
#define BARMETER_H

/*
 * BarMeter - Vertical bar gauge for tank levels and battery SOC
 * 
 * Displays a vertical bar with 20 stacked blocks that fill based on percentage.
 * Color-coded from red (low) through yellow to green (full).
 * 
 * DISPLAY:
 * - 20 horizontal blocks stacked vertically
 * - 100% at top, 0% at bottom
 * - Each block = 5% of capacity
 * - Colors: Red (0-20%), Orange (21-40%), Yellow (41-60%), Lime (61-80%), Green (81-100%)
 * 
 * STALE DATA HANDLING:
 * - Pass colorOverride to show grey blocks when data is stale
 * - Grey indicates the display is showing last-known value
 * 
 * USAGE:
 *   BarMeter battery1Meter;
 *   battery1Meter.drawMeter(&lcd, x, y, 30, 160, TFT_WHITE, 3);
 *   battery1Meter.updateLevel(&lcd, 75.0);  // 75% full, normal colors
 *   battery1Meter.updateLevel(&lcd, 75.0, TFT_DARKGREY);  // Stale data
 */

#include <LovyanGFX.hpp>
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
    // colorOverride: optional color to use instead of normal color coding (0 = use normal colors)
    void updateLevel(LGFX_Device* lcd, float level, uint16_t colorOverride = 0);
    
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
    void drawBlocks(LGFX_Device* lcd, float level, uint16_t colorOverride = 0);
};

#endif
