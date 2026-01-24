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

bool Layout::isBatteryIconRegion(int x,int y)
{
	// Battery icon is at lx+totalBatteryWidth+batVanOffset, lcd.height()-67
	// Icon is 50x32 pixels
	int lx=5;
	int batVanOffset=30;
	int totalBatteryWidth = 63;
	int iconX = lx+totalBatteryWidth+batVanOffset;
	int iconY = lcd.height()-67;
	
	if(x >= iconX && x <= iconX + 50 &&
	   y >= iconY && y <= iconY + 32)
		return true;

	return false;
}

bool Layout::isVanRegion(int x, int y)
{
	// Van icon is at lcd.width()-(lx+totalBatteryWidth+batVanOffset+50), lcd.height()-70
	// Using a larger touch area for the whole van/solar region
	int lx=5;
	int batVanOffset=30;
	int totalBatteryWidth = 63;
	int iconX = lcd.width()-(lx+totalBatteryWidth+batVanOffset+60);
	int iconY = lcd.height()-100;
	int regionWidth = 80;
	int regionHeight = 90;
	
	if(x >= iconX && x <= iconX + regionWidth &&
	   y >= iconY && y <= iconY + regionHeight)
		return true;

	return false;
}

bool Layout::isCenterRegion(int x, int y)
{
	// Center region is roughly middle third of screen
	int centerX = lcd.width() / 2;
	int centerY = lcd.height() / 2;
	int regionSize = 80;  // 80 pixel radius from center
	
	if(x >= centerX - regionSize && x <= centerX + regionSize &&
	   y >= centerY - regionSize && y <= centerY + regionSize)
		return true;
	
	return false;
}

void Layout::updateBitmaps()
{
	//update bar meters - use grey fill for stale devices (offline will be 0 so black anyway)
    uint16_t sok1Fill = (displayData.sok1Status == DEVICE_STALE) ? TFT_DARKGREY : 0;
    uint16_t sok2Fill = (displayData.sok2Status == DEVICE_STALE) ? TFT_DARKGREY : 0;
    battery1Meter.updateLevel(&lcd, displayData.stateOfCharge, sok1Fill);
    battery2Meter.updateLevel(&lcd, displayData.stateOfCharge2, sok2Fill);
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
	// Battery 1 SoC and hours left - color based on device status
	uint16_t sok1Color = getStatusColor(displayData.sok1Status);
	soc.updateText(displayData.stateOfCharge, sok1Color);
    battVolts.drawCenterText(battery1Config.x + 15, battery1Config.y + battery1Config.height + 5, displayData.batteryVolts, 1, "V", 1, sok1Color);
    battHoursLeft.drawCenterText(battery1Config.x + 15, battery1Config.y + battery1Config.height + 20, displayData.batteryHoursRem, 1, "H", 1, sok1Color);
    // Battery 2 SoC and hours left - color based on device status
	uint16_t sok2Color = getStatusColor(displayData.sok2Status);
	soc2.updateText(displayData.stateOfCharge2, sok2Color);
    battVolts2.drawCenterText(battery2Config.x + 15, battery2Config.y + battery2Config.height + 5, displayData.batteryVolts2, 1, "V", 1, sok2Color);
    battHoursLeft2.drawCenterText(battery2Config.x + 15, battery2Config.y + battery2Config.height + 20, displayData.batteryHoursRem2, 1, "H", 1, sok2Color);
    // Water days left - centered below water meter, number only
    waterDaysLeft.drawCenterText(waterConfig.x + 15, waterConfig.y + waterConfig.height + 5, displayData.waterDaysRem, 1, "D", 1, TFT_WHITE);
    // Gas days left - centered below gas meter, number only
    gasDaysLeft.drawCenterText(gasConfig.x + 15, gasConfig.y + gasConfig.height + 5, displayData.gasDaysRem, 1, "D", 1, TFT_WHITE);
    waterPercent.updateText(displayData.stateOfWater);
    gasPercent.updateText(displayData.stateOfGas);
    hertz.drawRightText(lcd.width()-10,lcd.height()-15,displayData.currentHertz,0,"Hz",2,TFT_WHITE);

	// BT2 values - color based on device status
	uint16_t bt2Color = getStatusColor(displayData.bt2Status);
	batteryTemp.updateText(cTof(displayData.batteryTemperature), bt2Color);
	batteryAmps.updateText(displayData.batteryAmpValue, bt2Color);
	chargerTemp.updateText(cTof(displayData.chargerTemperature), bt2Color);
	solarAmps.updateText(displayData.solarAmpValue, bt2Color);
	alternaterAmps.updateText(displayData.alternaterAmpValue, bt2Color);

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

	// Draw battery mode label if not combined
	int lx=5;
	int batVanOffset=30;
	int totalBatteryWidth = 63;
	int iconX = lx+totalBatteryWidth+batVanOffset;
	int iconY = lcd.height()-67;
	
	// Clear label area first
	lcd.fillRect(iconX+52, iconY+8, 15, 16, TFT_BLACK);
	
	if(displayData.batteryMode == BATTERY_SOK1)
	{
		lcd.setTextColor(TFT_WHITE);
		lcd.setTextFont(2);
		lcd.drawString("1", iconX+54, iconY+8);
	}
	else if(displayData.batteryMode == BATTERY_SOK2)
	{
		lcd.setTextColor(TFT_WHITE);
		lcd.setTextFont(2);
		lcd.drawString("2", iconX+54, iconY+8);
	}	
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

uint16_t Layout::getStatusColor(DeviceStatus status)
{
    switch(status) {
        case DEVICE_OFFLINE:
            return TFT_RED;
        case DEVICE_STALE:
            return TFT_DARKGREY;
        case DEVICE_ONLINE:
        default:
            return TFT_WHITE;
    }
}

void Layout::showBT2Detail(BT2Reader* bt2Reader)
{
	// Clear the screen
	lcd.fillScreen(TFT_BLACK);
	
	// Title
	lcd.setTextColor(TFT_YELLOW);
	lcd.setTextSize(2);
	lcd.setCursor(10, 5);
	lcd.print("BT2 Charge Controller");
	
	if(!bt2Reader || !bt2Reader->isConnected()) {
		lcd.setTextColor(TFT_RED);
		lcd.setTextSize(3);
		lcd.setCursor(lcd.width()/2 - 80, lcd.height()/2 - 20);
		lcd.print("Not Connected");
		
		lcd.setTextColor(TFT_DARKGREY);
		lcd.setTextSize(2);
		lcd.setCursor(lcd.width()/2 - 80, lcd.height() - 30);
		lcd.print("Touch to return");
		return;
	}
	
	// Two-column layout: Left column for Solar/Alt, Right column for Battery/Stats
	// Screen is 480x320
	int leftCol = 10;
	int rightCol = 250;
	int lineHeight = 18;
	int y = 30;
	
	lcd.setTextSize(1);  // Smaller text for data density
	
	// === LEFT COLUMN: Solar & Alternator ===
	lcd.setTextColor(BATTERY_FILL);
	lcd.setCursor(leftCol, y);
	lcd.print("-- SOLAR --");
	y += lineHeight;
	
	lcd.setCursor(leftCol, y);
	lcd.printf("Voltage:  %.1fV", bt2Reader->printRegister(RENOGY_SOLAR_VOLTAGE) / 10.0);
	y += lineHeight;
	
	lcd.setCursor(leftCol, y);
	lcd.printf("Current:  %.2fA", bt2Reader->printRegister(RENOGY_SOLAR_CURRENT) / 100.0);
	y += lineHeight;
	
	lcd.setCursor(leftCol, y);
	lcd.printf("Power:    %dW", bt2Reader->printRegister(RENOGY_SOLAR_POWER));
	y += lineHeight + 5;
	
	lcd.setTextColor(TFT_LIGHTGREY);
	lcd.setCursor(leftCol, y);
	lcd.print("-- ALTERNATOR --");
	y += lineHeight;
	
	lcd.setCursor(leftCol, y);
	lcd.printf("Voltage:  %.1fV", bt2Reader->printRegister(RENOGY_ALTERNATOR_VOLTAGE) / 10.0);
	y += lineHeight;
	
	lcd.setCursor(leftCol, y);
	lcd.printf("Current:  %.2fA", bt2Reader->printRegister(RENOGY_ALTERNATOR_CURRENT) / 100.0);
	y += lineHeight;
	
	lcd.setCursor(leftCol, y);
	lcd.printf("Power:    %dW", bt2Reader->printRegister(RENOGY_ALTERNATOR_POWER));
	y += lineHeight + 5;
	
	lcd.setTextColor(TFT_WHITE);
	lcd.setCursor(leftCol, y);
	lcd.print("-- TODAY --");
	y += lineHeight;
	
	lcd.setCursor(leftCol, y);
	lcd.printf("Amp Hours:   %d Ah", bt2Reader->printRegister(RENOGY_TODAY_AMP_HOURS));
	y += lineHeight;
	
	lcd.setCursor(leftCol, y);
	lcd.printf("Watt Hours:  %d Wh", bt2Reader->printRegister(RENOGY_TODAY_POWER));
	y += lineHeight;
	
	lcd.setCursor(leftCol, y);
	lcd.printf("Peak Curr:   %.2fA", bt2Reader->printRegister(RENOGY_TODAY_HIGHEST_CURRENT) / 100.0);
	y += lineHeight;
	
	lcd.setCursor(leftCol, y);
	lcd.printf("Peak Power:  %dW", bt2Reader->printRegister(RENOGY_TODAY_HIGHEST_POWER));
	
	// === RIGHT COLUMN: Battery ===
	y = 30;
	lcd.setTextColor(TFT_CYAN);
	lcd.setCursor(rightCol, y);
	lcd.print("-- BATTERY --");
	y += lineHeight;
	
	lcd.setCursor(rightCol, y);
	lcd.printf("SOC:      %d%%", bt2Reader->printRegister(RENOGY_AUX_BATT_SOC));
	y += lineHeight;
	
	lcd.setCursor(rightCol, y);
	lcd.printf("Voltage:  %.1fV", bt2Reader->printRegister(RENOGY_AUX_BATT_VOLTAGE) / 10.0);
	y += lineHeight;
	
	lcd.setCursor(rightCol, y);
	lcd.printf("Max Chrg: %.2fA", bt2Reader->printRegister(RENOGY_MAX_CHARGE_CURRENT) / 100.0);
	y += lineHeight;
	
	lcd.setCursor(rightCol, y);
	lcd.printf("Ctrl Tmp: %.0fF", cTof(bt2Reader->getTemperature()));
	y += lineHeight;
	
	lcd.setCursor(rightCol, y);
	lcd.printf("Capacity: %d Ah", bt2Reader->printRegister(RENOGY_AUX_BATT_CAPACITY));
	y += lineHeight + 5;
	
	lcd.setTextColor(TFT_ORANGE);
	lcd.setCursor(rightCol, y);
	lcd.print("-- LIMITS --");
	y += lineHeight;
	
	lcd.setCursor(rightCol, y);
	lcd.printf("Low V:    %.1fV", bt2Reader->printRegister(RENOGY_AUX_BATT_LOW_VOLTAGE) / 10.0);
	y += lineHeight;
	
	lcd.setCursor(rightCol, y);
	lcd.printf("High V:   %.1fV", bt2Reader->printRegister(RENOGY_AUX_BATT_HIGH_VOLTAGE) / 10.0);
	
	// Touch prompt at bottom
	lcd.setTextColor(TFT_DARKGREY);
	lcd.setCursor(lcd.width()/2 - 50, lcd.height() - 15);
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