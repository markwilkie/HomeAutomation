#include "Layout.h"
#include "PowerLogger.h"

LGFX lcd;

//Every ten minutes for 9 hours
SparkLine<float> nightSparkAh(NIGHT_AH_DUR/NIGHT_AH_INT, [&](const int x0, const int y0, const int x1, const int y1) { 
    lcd.drawLine(x0, y0, x1, y1,WATER_RANGE);
});
//Every 20 minutes for 24 hours  (if there's too many elements, it'll miss peaks because of scaling)
SparkLine<float> daySparkAh(DAY_AH_DUR/DAY_AH_INT, [&](const int x0, const int y0, const int x1, const int y1) { 
    lcd.drawLine(x0, y0, x1, y1,BATTERY_FILL);
});  

void Layout::init()
{
    //Setup screen
    screen.init();
}

void Layout::drawInitialScreen()
{
    // Clear the screen first
    lcd.fillScreen(TFT_BLACK);
    
    // Reset text size to default
    lcd.setTextSize(1);
    
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
	setBLEIndicator(TFT_DARKGRAY);
	dateTime.drawText(3,3,"n/a",2,TFT_WHITE);

    //Bitmap based meters
    //(char* label,int vmin, int vmax, int scale, BitmapConfig *bitmapConfig);
    int lx=5; int ly=80; int imgWidth=30; int imgHeight=160;

	// Battery 1 bar meter (30x160 vertical bar)
	int battery1X = lx;
	int battery1Y = ly;
	battery1Config.x = battery1X;
	battery1Config.y = battery1Y;
	battery1Config.width = 30;
	battery1Config.height = 160;
	battery1Config.color = TFT_ORANGE;  // Outline color
	battery1Meter.drawMeter(&lcd, battery1X, battery1Y, 30, 160, TFT_WHITE, 3);
	battery1Title.drawText(battery1X + 15 - (lcd.textWidth("Bat1") / 2), battery1Y - 30, "Bat1", 1, TFT_WHITE);
	soc.drawCenterText(battery1X + 15, battery1Y - 15, 0, 0, "%", 1, TFT_WHITE);

	// Battery 2 bar meter (30x160 vertical bar, 3 pixels right of battery 1)
	int battery2X = battery1X + 30 + 3;
	int battery2Y = ly;
	battery2Config.x = battery2X;
	battery2Config.y = battery2Y;
	battery2Config.width = 30;
	battery2Config.height = 160;
	battery2Config.color = TFT_ORANGE;  // Outline color
	battery2Meter.drawMeter(&lcd, battery2X, battery2Y, 30, 160, TFT_WHITE, 3);
	battery2Title.drawText(battery2X + 15 - (lcd.textWidth("Bat2") / 2), battery2Y - 30, "Bat2", 1, TFT_WHITE);
	soc2.drawCenterText(battery2X + 15, battery2Y - 15, 0, 0, "%", 1, TFT_WHITE);

	// Water bar meter (30x160 vertical bar)
	int waterX = lcd.width() - 30 - lx;
	int waterY = ly;
	waterConfig.x = waterX;
	waterConfig.y = waterY;
	waterConfig.width = 30;
	waterConfig.height = 160;
	waterConfig.color = TFT_YELLOW;  // Outline color
	waterMeter.drawMeter(&lcd, waterX, waterY, 30, 160, TFT_WHITE, 3);
	waterTitle.drawText(waterX + 15 - (lcd.textWidth("Water") / 2), waterY - 30, "Water", 1, TFT_WHITE);
	waterPercent.drawCenterText(waterX + 15, waterY - 15, 0, 0, "%", 1, TFT_WHITE);

	// Gas bar meter (30x160 vertical bar, 5 pixels left of water meter)
	int gasX = waterX - 30 - 5;
	int gasY = ly;
	gasConfig.x = gasX;
	gasConfig.y = gasY;
	gasConfig.width = 30;
	gasConfig.height = 160;
	gasConfig.color = TFT_YELLOW;  // Outline color
	gasMeter.drawMeter(&lcd, gasX, gasY, 30, 160, TFT_WHITE, 3);
	gasTitle.drawText(gasX + 15 - (lcd.textWidth("Gas") / 2), gasY - 30, "Gas", 1, TFT_WHITE);
	gasPercent.drawCenterText(gasX + 15, gasY - 15, 0, 0, "%", 1, TFT_WHITE);

	//show moon
	moonConfig.x=battery2Config.x+battery2Config.width+20+8;
	moonConfig.y=battery2Config.y+10;
	moonConfig.bmArray=moonBitmap;
	moonConfig.width=30;
	moonConfig.height=26;
	moonConfig.color=WATER_RANGE;
	lcd.drawBitmap(moonConfig.x, moonConfig.y, moonConfig.bmArray,  moonConfig.width,  moonConfig.height,  moonConfig.color);

	//show calendar (position relative to gas meter, which is leftmost)
	calendarConfig.x=gasConfig.x-50-8;
	calendarConfig.y=gasConfig.y+10;
	calendarConfig.bmArray=calendarBitmap;
	calendarConfig.width=32;
	calendarConfig.height=32;
	calendarConfig.color=BATTERY_FILL;
	lcd.drawBitmap(calendarConfig.x, calendarConfig.y, calendarConfig.bmArray,  calendarConfig.width,  calendarConfig.height,  calendarConfig.color);	

	//Text for night/day
	nightAh.drawRightText(moonConfig.x+10,moonConfig.y-25,0,0,"Ah",2,WATER_RANGE);
	dayAh.drawRightText(calendarConfig.x-25,calendarConfig.y-25,0,0,"Ah",2,BATTERY_FILL);

	//Battery icons w/ annotations
	int batVanOffset=30;   //higher number moves each icon towards the middle
	int totalBatteryWidth = 63;  // Two 30px meters + 3px gap
	lcd.drawBitmap(lx+totalBatteryWidth+batVanOffset, lcd.height()-67, batteryIconBitmap,  50,  32,  TFT_LIGHTGRAY);
	cmosIndicator.drawCircle(lx+totalBatteryWidth+batVanOffset+15,lcd.height()-47,5,TFT_BLACK,TFT_LIGHTGREY);  //c-mos indicator
	dmosIndicator.drawCircle(lx+totalBatteryWidth+batVanOffset+35,lcd.height()-47,5,TFT_BLACK,TFT_LIGHTGREY);  //d-mos indicator
	batteryAmps.drawCenterText(lx+totalBatteryWidth+batVanOffset+25, lcd.height()-80,0,0,"A",2,TFT_LIGHTGRAY);
	batteryTemp.drawCenterText(lx+totalBatteryWidth+batVanOffset+25,lcd.height()-25,0,0,"F",2,TFT_WHITE);

	//config battery heater bitmap
	heaterConfig.x=lx+totalBatteryWidth+batVanOffset;
	heaterConfig.y=lcd.height()-37;
	heaterConfig.bmArray=heaterBitmap;
	heaterConfig.width=50;
	heaterConfig.height=15;
	heaterConfig.color=TFT_RED;

	//Draw van
	lcd.drawBitmap(lcd.width()-(lx+totalBatteryWidth+batVanOffset+25), lcd.height()-85, sunBitmap,  40,  40,  BATTERY_FILL);
	solarAmps.drawCenterText(lcd.width()-(lx+totalBatteryWidth+batVanOffset+35), lcd.height()-83,15,0,"A",2,BATTERY_FILL);
	lcd.drawBitmap(lcd.width()-(lx+totalBatteryWidth+batVanOffset+50), lcd.height()-70, vanBitmap,  40,  40,  TFT_LIGHTGREY);
	alternaterAmps.drawCenterText(lcd.width()-(lx+totalBatteryWidth+batVanOffset), lcd.height()-45,0,0,"A",2,TFT_LIGHTGREY);
	chargerTemp.drawCenterText(lcd.width()-(lx+totalBatteryWidth+batVanOffset+20),lcd.height()-25,0,0,"F",2,TFT_WHITE);
}

void Layout::setWifiIndicator(bool online)
{
	if(online)
		lcd.drawBitmap(lcd.width()-25, 3, wifiBitmap,  22,  22,  TFT_WHITE);
	else
		lcd.drawBitmap(lcd.width()-25, 3, wifiBitmap,  22,  22,  TFT_BLACK);
}

void Layout::setBLEIndicator(int color)
{
	lcd.drawBitmap(lcd.width()-60, 3,bleBitmap,  23,  23,  color);
}

bool Layout::isBLERegion(int x,int y)
{
	if(x>lcd.width()-100 && y<100)
		return true;

	return false;
}

bool Layout::isWaterRegion(int x,int y)
{
	// Check if touch is within water meter bounds
	// waterConfig has x, y, width, height
	if(x >= waterConfig.x && x <= waterConfig.x + waterConfig.width &&
	   y >= waterConfig.y && y <= waterConfig.y + waterConfig.height)
		return true;

	return false;
}

void Layout::updateBitmaps()
{
	//update bar meters
    battery1Meter.updateLevel(&lcd, displayData.stateOfCharge);
    battery2Meter.updateLevel(&lcd, displayData.stateOfCharge2);
    waterMeter.updateLevel(&lcd, displayData.stateOfWater);
    gasMeter.updateLevel(&lcd, displayData.stateOfGas);
}

void Layout::updateLCD(ESP32Time *rtc)
{
	//datetime
	dateTime.updateText((rtc->getTime("%m/%d %H:%M:%S")).c_str());

	//update center meter
    centerOutMeter.drawMeter(displayData.chargeAmps);
    centerInMeter.drawMeter(displayData.drawAmps);
    centerInMeter.drawText("A",displayData.chargeAmps-displayData.drawAmps);

    //update text values
	lcd.setTextFont(4);
	lcd.setTextSize(1);
    volts.drawText((lcd.width()/2)-(lcd.textWidth("99.9V")/2),centerInMeter.getY()+70,displayData.currentVolts,1,"V",4,TFT_WHITE);

	//(int x,int y,float value,int dec,const char *label,int font);
	//(BitmapConfig *bmCfg,int offset,float value,int dec,const char *label,int font)
	// Battery 1 SoC and hours left
	soc.updateText(displayData.stateOfCharge);
    battVolts.drawCenterText(battery1Config.x + 15, battery1Config.y + battery1Config.height + 5, displayData.batteryVolts, 1, "V", 1, TFT_WHITE);
    battHoursLeft.drawCenterText(battery1Config.x + 15, battery1Config.y + battery1Config.height + 20, displayData.batteryHoursRem, 1, "H", 1, TFT_WHITE);
    // Battery 2 SoC and hours left
	soc2.updateText(displayData.stateOfCharge2);
    battVolts2.drawCenterText(battery2Config.x + 15, battery2Config.y + battery2Config.height + 5, displayData.batteryVolts2, 1, "V", 1, TFT_WHITE);
    battHoursLeft2.drawCenterText(battery2Config.x + 15, battery2Config.y + battery2Config.height + 20, displayData.batteryHoursRem2, 1, "H", 1, TFT_WHITE);
    // Water days left - centered below water meter, number only
    waterDaysLeft.drawCenterText(waterConfig.x + 15, waterConfig.y + waterConfig.height + 5, displayData.waterDaysRem, 1, "D", 1, TFT_WHITE);
    // Gas days left - centered below gas meter, number only
    gasDaysLeft.drawCenterText(gasConfig.x + 15, gasConfig.y + gasConfig.height + 5, displayData.gasDaysRem, 1, "D", 1, TFT_WHITE);
    waterPercent.updateText(displayData.stateOfWater);
    gasPercent.updateText(displayData.stateOfGas);
    hertz.drawRightText(lcd.width()-10,lcd.height()-15,displayData.currentHertz,0,"Hz",2,TFT_WHITE);

	batteryTemp.updateText(cTof(displayData.batteryTemperature));
	batteryAmps.updateText(displayData.batteryAmpValue);
	chargerTemp.updateText(cTof(displayData.chargerTemperature));
	solarAmps.updateText(displayData.solarAmpValue);
	alternaterAmps.updateText(displayData.alternaterAmpValue);

	//Sparklines
	int slLen=70; int slHeight=20;
	int slX=moonConfig.x+35-(slLen/2)+(moonConfig.width/2);
	int slY=moonConfig.y-30;
	lcd.fillRect(slX, slY, slLen+2, slHeight,TFT_BLACK);
    nightSparkAh.draw(slX, slY, slLen, slHeight);
	nightAh.updateText((int)(nightSparkAh.findAvg()*(double)(nightSparkAh.getElements()/(3600.0/NIGHT_AH_INT))));  

	slX=calendarConfig.x-(slLen/2)+(calendarConfig.width/2);
	slY=calendarConfig.y-30;
	lcd.fillRect(slX, slY, slLen+2, slHeight,TFT_BLACK);
    daySparkAh.draw(slX, slY, slLen, slHeight);	
	dayAh.updateText((int)(daySparkAh.findAvg()*(double)(daySparkAh.getElements()/(3600.0/DAY_AH_INT))));  

	//Battery heater
	if(displayData.heater)  { lcd.drawBitmap(heaterConfig.x,heaterConfig.y,heaterConfig.bmArray,heaterConfig.width,heaterConfig.height,heaterConfig.color);}
	else		{ lcd.drawBitmap(heaterConfig.x,heaterConfig.y,heaterConfig.bmArray,heaterConfig.width,heaterConfig.height,TFT_BLACK);}

	//Update C and D MOS
	if(displayData.cmos) {	cmosIndicator.updateCircle(TFT_GREEN);}
	else 	 {	cmosIndicator.updateCircle(TFT_LIGHTGREY);}

	if(displayData.dmos) {	dmosIndicator.updateCircle(TFT_GREEN);}
	else	 {	dmosIndicator.updateCircle(TFT_LIGHTGREY); }
}

float Layout::cTof(float c)
{
	return (c * 9.0 / 5.0) + 32.0;
}

void Layout::showWaterDetail()
{
	// Clear the screen
	lcd.fillScreen(TFT_BLACK);
	
	// Show large water percentage
	lcd.setTextColor(WATER_BLUE);
	lcd.setTextSize(4);
	lcd.setCursor(lcd.width()/2 - 80, lcd.height()/2 - 80);
	lcd.printf("Water Level");
	
	lcd.setTextColor(WATER_FILL);
	lcd.setTextSize(8);
	lcd.setCursor(lcd.width()/2 - 80, lcd.height()/2 - 20);
	lcd.printf("%d%%", (int)displayData.stateOfWater);
	
	// Show days remaining
	lcd.setTextColor(TFT_WHITE);
	lcd.setTextSize(3);
	lcd.setCursor(lcd.width()/2 - 100, lcd.height()/2 + 80);
	lcd.printf("%.1f days left", displayData.waterDaysRem);
	
	// Show touch prompt
	lcd.setTextColor(TFT_DARKGREY);
	lcd.setTextSize(2);
	lcd.setCursor(lcd.width()/2 - 80, lcd.height() - 40);
	lcd.print("Touch to return");
}

SparkLine<float> *Layout::getDaySparkPtr()
{
	return &daySparkAh;
}

SparkLine<float> *Layout::getNightSparkPtr()
{
	return &nightSparkAh;
}