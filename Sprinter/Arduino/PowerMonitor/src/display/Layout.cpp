#include "Layout.h"

LGFX lcd;

//Every ten seconds for 8 hours
SparkLine<uint16_t> nightSparkAh(48, [&](const uint16_t x0, const uint16_t y0, const uint16_t x1, const uint16_t y1) { 
    lcd.drawLine(x0, y0, x1, y1,WATER_RANGE);
});
//Every 20 seconds for 24 hours  (if there's too many elements, it'll miss peaks because of scaling)
SparkLine<uint16_t> daySparkAh(72, [&](const uint16_t x0, const uint16_t y0, const uint16_t x1, const uint16_t y1) { 
    lcd.drawLine(x0, y0, x1, y1,BATTERY_FILL);
});  

void Layout::init()
{
    //Setup screen
    screen.init();
	screen.setBrightness(STND_BRIGHTNESS);
}

void Layout::drawInitialScreen()
{
    //Static labels
    lcd.setTextColor(TFT_WHITE);
    lcd.drawString("Sprinkle Data",(lcd.width()/2)-(lcd.textWidth("Sprinkle Data")), 5, 4);	

    //Init circular meters
	int cntrOffsetY=20;
    int cx=(lcd.width()/2);
    int cy=(lcd.height()/2)+cntrOffsetY;
    centerOutMeter.initMeter(&lcd,0,20,cx,cy,90,RED2GREEN); 
    centerInMeter.initMeter(&lcd,0,20,cx,cy,70,GREEN2RED); 

	//Draw online indicator and datetime
	setWifiIndicator(false);
	setBLEIndicator(TFT_DARKGREY);
	dateTime.drawText(3,3,"n/a",2,TFT_WHITE);

    //Bitmap based meters
    //(char* label,int vmin, int vmax, int scale, BitmapConfig *bitmapConfig);
    int lx=5; int ly=80; int imgWidth=66; int imgHeight=160;

	socConfig.bmArray=batteryBitmap;
	socConfig.x=lx;  socConfig.y=ly;  socConfig.width=imgWidth;  socConfig.height=imgHeight;  
	socConfig.color=BATTERY_ORANGE;
	socFill.color=BATTERY_FILL;
	socFill.height=80;
	socFill.width=57;
	socFill.start=16;
	waterFill.redraw=true;
    socMeter.drawMeter(&lcd,"SoC", 10, 100, 100, &socConfig, &socFill);

	waterConfig.bmArray=waterBottleBitmap;
	waterConfig.x=lcd.width()-imgWidth-lx;  waterConfig.y=ly;  waterConfig.width=imgWidth;  waterConfig.height=imgHeight;  
	waterConfig.color=WATER_BLUE;
	waterFill.color=WATER_FILL;
	waterFill.rangeColor=WATER_RANGE;	
	waterFill.height=90;
	waterFill.width=60;
	waterFill.start=28;
	waterFill.redraw=true;
    waterMeter.drawMeter(&lcd,"Water", 0, 100, 100, &waterConfig, &waterFill);

	//show moon
	moonConfig.x=socConfig.x+socConfig.width+20+8;
	moonConfig.y=socConfig.y+10;
	moonConfig.bmArray=moonBitmap;
	moonConfig.width=30;
	moonConfig.height=26;
	moonConfig.color=WATER_RANGE;
	lcd.drawBitmap(moonConfig.x, moonConfig.y, moonConfig.bmArray,  moonConfig.width,  moonConfig.height,  moonConfig.color);

	//show calendar
	calendarConfig.x=waterConfig.x-50-8;
	calendarConfig.y=waterConfig.y+10;
	calendarConfig.bmArray=calendarBitmap;
	calendarConfig.width=32;
	calendarConfig.height=32;
	calendarConfig.color=BATTERY_FILL;
	lcd.drawBitmap(calendarConfig.x, calendarConfig.y, calendarConfig.bmArray,  calendarConfig.width,  calendarConfig.height,  calendarConfig.color);	

	//Text for night/day
	nightAh.drawRightText(moonConfig.x+10,moonConfig.y-25,1.4,1,"Ah",2,WATER_RANGE);
	dayAh.drawRightText(calendarConfig.x-25,calendarConfig.y-25,17,0,"Ah",2,BATTERY_FILL);

	//Battery icons w/ annotations
	int batVanOffset=30;   //higher number moves each icon towards the middle
	lcd.drawBitmap(lx+imgWidth+batVanOffset, lcd.height()-67, batteryIconBitmap,  50,  32,  TFT_LIGHTGRAY);
	cmosIndicator.drawCircle(lx+imgWidth+batVanOffset+15,lcd.height()-47,5,TFT_BLACK,TFT_LIGHTGREY);  //c-mos indicator
	dmosIndicator.drawCircle(lx+imgWidth+batVanOffset+35,lcd.height()-47,5,TFT_BLACK,TFT_LIGHTGREY);  //d-mos indicator
	batteryAmps.drawCenterText(lx+imgWidth+batVanOffset+25, lcd.height()-80,11.5,1,"A",2,TFT_LIGHTGRAY);
	batteryTemp.drawCenterText(lx+imgWidth+batVanOffset+25,lcd.height()-25,24.5,0,"C",2,TFT_WHITE);

	//config battery heater bitmap
	heaterConfig.x=lx+imgWidth+batVanOffset;
	heaterConfig.y=lcd.height()-37;
	heaterConfig.bmArray=heaterBitmap;
	heaterConfig.width=50;
	heaterConfig.height=15;
	heaterConfig.color=TFT_RED;

	//Draw van
	lcd.drawBitmap(lcd.width()-(lx+imgWidth+batVanOffset+25), lcd.height()-85, sunBitmap,  40,  40,  BATTERY_FILL);
	solarAmps.drawCenterText(lcd.width()-(lx+imgWidth+batVanOffset+35), lcd.height()-83,15,0,"A",2,BATTERY_FILL);
	lcd.drawBitmap(lcd.width()-(lx+imgWidth+batVanOffset+50), lcd.height()-70, vanBitmap,  40,  40,  TFT_LIGHTGREY);
	alternaterAmps.drawCenterText(lcd.width()-(lx+imgWidth+batVanOffset), lcd.height()-45,12,0,"A",2,TFT_DARKGREY);
	chargerTemp.drawCenterText(lcd.width()-(lx+imgWidth+batVanOffset+20),lcd.height()-25,22.5,0,"C",2,TFT_WHITE);
}

void Layout::setWifiIndicator(bool online)
{
	if(online)
		lcd.drawBitmap(lcd.width()-25, 3, wifiBitmap,  22,  22,  TFT_WHITE);
	else
		lcd.drawBitmap(lcd.width()-25, 3, wifiBitmap,  22,  22,  TFT_DARKGREY);
}

void Layout::setBLEIndicator(int color)
{
	lcd.drawBitmap(lcd.width()-60, 3,bleBitmap,  23,  23,  color);
}

void Layout::updateLCD(ESP32Time *rtc)
{
	//datetime
	dateTime.updateText((rtc->getTime("%m/%d %H:%M:%S")).c_str());

	//update center meter
    centerOutMeter.drawMeter(displayData.chargeAmps);
    centerInMeter.drawMeter(displayData.drawAmps);
    centerInMeter.drawText("A",displayData.chargeAmps-displayData.drawAmps);

	//update bitmap meters
    socMeter.updateLevel(displayData.stateOfCharge,2,0);
    waterMeter.updateLevel(displayData.stateOfWater,displayData.rangeForWater,2,0);	

    //update text values
	lcd.setTextFont(4);
	lcd.setTextSize(1);
    volts.drawText((lcd.width()/2)-(lcd.textWidth("99.9V")/2),centerInMeter.getY()+70,displayData.currentVolts,1,"V",4,TFT_WHITE);

	//(int x,int y,float value,int dec,const char *label,int font);
	//(BitmapConfig *bmCfg,int offset,float value,int dec,const char *label,int font)
	soc.drawBitmapTextCenter(socMeter.getBitmapConfig(),displayData.stateOfCharge,0,"%",2,TFT_WHITE,-1);
    battHoursLeft.drawBitmapTextBottom(socMeter.getBitmapConfig(),10,displayData.batteryHoursRem,1," Hrs",2,socMeter.getBitmapConfig()->color);
    waterDaysLeft.drawBitmapTextBottom(waterMeter.getBitmapConfig(),10,displayData.waterDaysRem,1," Dys",2,waterMeter.getBitmapConfig()->color);
    hertz.drawRightText(lcd.width()-10,lcd.height()-15,displayData.currentHertz,0,"Hz",2,TFT_WHITE);

	batteryTemp.updateText(displayData.batteryTemperature);
	batteryAmps.updateText(displayData.batteryAmpValue);
	chargerTemp.updateText(displayData.chargerTemperature);
	solarAmps.updateText(displayData.solarAmpValue);
	alternaterAmps.updateText(displayData.alternaterAmpValue);

	//Sparklines
	int slLen=70; int slHeight=20;
	int slX=moonConfig.x+35-(slLen/2)+(moonConfig.width/2);
	int slY=moonConfig.y-30;
	lcd.fillRect(slX, slY, slLen+2, slHeight,TFT_BLACK);
    nightSparkAh.draw(slX, slY, slLen, slHeight);

	slX=calendarConfig.x-(slLen/2)+(calendarConfig.width/2);
	slY=calendarConfig.y-30;
	lcd.fillRect(slX, slY, slLen+2, slHeight,TFT_BLACK);
    daySparkAh.draw(slX, slY, slLen, slHeight);	

	//Battery heater
	if(displayData.heater)  { lcd.drawBitmap(heaterConfig.x,heaterConfig.y,heaterConfig.bmArray,heaterConfig.width,heaterConfig.height,heaterConfig.color);}
	else		{ lcd.drawBitmap(heaterConfig.x,heaterConfig.y,heaterConfig.bmArray,heaterConfig.width,heaterConfig.height,TFT_BLACK);}

	//Update C and D MOS
	if(displayData.cmos) {	cmosIndicator.updateCircle(TFT_GREEN);}
	else 	 {	cmosIndicator.updateCircle(TFT_LIGHTGREY);}

	if(displayData.dmos) {	dmosIndicator.updateCircle(TFT_GREEN);}
	else	 {	dmosIndicator.updateCircle(TFT_LIGHTGREY); }
}

void Layout::addToDayAhSpark(uint16_t value)
{
	daySparkAh.add(value);
}

void Layout::addToNightAhSpark(uint16_t value)
{
	nightSparkAh.add(value);
}