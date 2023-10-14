#include "BitmapMeter.h"

void BitmapMeter::drawMeter(LGFX *_lcd,char* _label,int _vmin, int _vmax, int _scale, BitmapConfig *_bmCfg, FillConfig *_fillCfg)
{
    //Set initial values
    lcd=_lcd;
    bmcfg=_bmCfg;
    fillcfg=_fillCfg;
    label=_label;
    vmin=_vmin;
    vmax=_vmax;
    scale=_scale;
    oldValue=-1;

    //Draw meter itself
	lcd->drawBitmap(bmcfg->x, bmcfg->y,  bmcfg->bmArray,  bmcfg->width,  bmcfg->height,  bmcfg->color);
    lcd->setTextColor(bmcfg->color);
    
    lcd->drawCentreString(label, bmcfg->x+(bmcfg->width/2), bmcfg->y-20, 2);
    lcd->drawCentreString("--", bmcfg->x+(bmcfg->width/2), (bmcfg->y+bmcfg->height)+20, 2);
}

void BitmapMeter::updateLevel(float value, int digits, int dec)
{
    updateLevel(value,0,digits,dec);
}

void BitmapMeter::updateLevel(float value, float rangeValue, int digits, int dec)
{
    //Calc rectangle size for main fill
    int lineWidth=((bmcfg->width - fillcfg->width)/2);
    int topLine=value*(fillcfg->height/(float)scale);

    //Fill rectangle
    int x=bmcfg->x + lineWidth;
    int y=bmcfg->y + (bmcfg->height - topLine - fillcfg->start);
    int yBlank=bmcfg->y + bmcfg->height - fillcfg->start - fillcfg->height; 
    lcd->fillRect(x, yBlank, fillcfg->width+1, fillcfg->height, TFT_BLACK);  //blank
    lcd->fillRect(x, y, fillcfg->width+1, topLine+1, fillcfg->color);

    //Range fill?
    if(fillcfg->rangeColor!=0 && rangeValue>0)
    {
        int rangeTop=rangeValue*(fillcfg->height/(float)scale);
        y=bmcfg->y + (bmcfg->height - rangeTop  - fillcfg->start)-1;        
        lcd->fillRect(x, y, fillcfg->width+1, rangeTop - topLine+1, fillcfg->rangeColor);
    }

    //Redraw?
    if(fillcfg->redraw)
    {
        lcd->drawBitmap(bmcfg->x, bmcfg->y,  bmcfg->bmArray,  bmcfg->width,  bmcfg->height,  bmcfg->color);
    }
}

BitmapConfig *BitmapMeter::getBitmapConfig() { return bmcfg; }

