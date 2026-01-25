#include "BarMeter.h"

BarMeter::BarMeter()
    : _x(0), _y(0), _width(30), _height(160), _outlineWidth(3), _outlineColor(TFT_YELLOW)
{
}

void BarMeter::drawMeter(LGFX_Device* lcd, int x, int y, int width, int height, 
                         uint16_t outlineColor, int outlineWidth)
{
    _x = x;
    _y = y;
    _width = width;
    _height = height;
    _outlineColor = outlineColor;
    _outlineWidth = outlineWidth;
    
    // Draw outline
    for(int i = 0; i < outlineWidth; i++)
    {
        lcd->drawRect(_x + i, _y + i, _width - (2 * i), _height - (2 * i), outlineColor);
    }
    
    // Draw blocks at 100%
    drawBlocks(lcd, 100.0);
}

void BarMeter::updateLevel(LGFX_Device* lcd, float level, uint16_t colorOverride)
{
    // Clamp level to 0-100
    if(level < 0) level = 0;
    if(level > 100) level = 100;
    
    drawBlocks(lcd, level, colorOverride);
}

uint16_t BarMeter::getColorForPercent(float percent)
{
    // Color bands by percentage
    if(percent < 35)
        return TFT_RED;
    else if(percent < 50)
        return TFT_ORANGE;
    else if(percent < 70)
        return TFT_YELLOW;
    else
        return TFT_GREEN;
}

/*
 * drawBlocks() - Render the bar meter fill blocks
 * 
 * Draws BLOCK_COUNT vertical segments to visualize the level.
 * Blocks are filled from BOTTOM (0%) to TOP (100%).
 * 
 * COLOR LOGIC:
 * - If colorOverride is provided (non-zero): Use it for all lit blocks
 *   (Used for stale devices to show grey fill)
 * - Otherwise: Color based on percentage level
 *   - < 35%: TFT_RED (critical)
 *   - < 50%: TFT_ORANGE (warning)
 *   - < 70%: TFT_YELLOW (attention)
 *   - >= 70%: TFT_GREEN (good)
 * 
 * BLOCK CALCULATION:
 * Uses cumulative rounding to evenly distribute pixels across blocks.
 * Each block's center percentage is calculated to determine if it should
 * be lit based on the current level.
 * 
 * @param lcd - Display device pointer
 * @param level - Current level 0-100%
 * @param colorOverride - If non-zero, use this color for all lit blocks
 */
void BarMeter::drawBlocks(LGFX_Device* lcd, float level, uint16_t colorOverride)
{
    // Inner area for blocks
    int inner_left = _x + _outlineWidth;
    int inner_top = _y + _outlineWidth;
    int inner_right = _x + _width - _outlineWidth - 1;
    int inner_bottom = _y + _height - _outlineWidth - 1;
    int inner_height = inner_bottom - inner_top + 1;
    
    // Get color for all blocks - use override if provided, otherwise normal color coding
    uint16_t levelColor = (colorOverride != 0) ? colorOverride : getColorForPercent(level);
    
    // Partition height across BLOCK_COUNT using cumulative rounding
    float acc = 0.0;
    int prev_y = inner_top;
    
    for(int i = 0; i < BLOCK_COUNT; i++)
    {
        acc += (float)inner_height / (float)BLOCK_COUNT;
        int y2 = inner_top + (int)(acc + 0.5) - 1;
        
        // 100% at the TOP block; 0% at the bottom
        float pct_center = (1.0 - ((i + 0.5) / (float)BLOCK_COUNT)) * 100.0;
        
        // Determine if this block should be lit based on level
        uint16_t color;
        if(pct_center <= level)
        {
            // Block is lit - use same color for all lit blocks
            color = levelColor;
        }
        else
        {
            // Block is off - use dark color
            color = TFT_BLACK;
        }
        
        // Draw the block
        lcd->fillRect(inner_left, prev_y, inner_right - inner_left + 1, y2 - prev_y + 1, color);
        
        prev_y = y2 + 1;
    }
}
