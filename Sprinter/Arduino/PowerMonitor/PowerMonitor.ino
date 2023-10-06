#include <ArduinoBLE.h>
#include <LovyanGFX.hpp>
#include "LGFX_ST32-SC01Plus.hpp"
#include "BT2Reader.h"
#include "SOKReader.h"

#include "PowerMonitor.h"
#include "Screen.h"
#include "BitmapMeter.h"
#include "CircularMeter.h"

#define POLL_TIME_MS	500
#define SCR_UPDATE_TIME 2000
#define BT_TIMEOUT_MS	5000

//Screen lib
LGFX lcd; 

//Screen
Screen screen;

//Meters
CircularMeter centerOutMeter;
CircularMeter centerInMeter;
BitmapMeter socMeter;
BitmapMeter waterMeter;

BitmapConfig socConfig;
FillConfig socFill;
BitmapConfig waterConfig;
FillConfig waterFill;

BitmapConfig heaterConfig;
BitmapConfig calendarConfig;
BitmapConfig moonConfig;

Primitive cmosIndicator;
Primitive dmosIndicator;

//Dynamic text
Text soc;
Text battHoursLeft;
Text waterDaysLeft;
Text volts;
Text hertz;
Text batteryTemp;
Text batteryAmps;
Text chargerTemp;
Text solarAmps;
Text alternaterAmps;
Text nightAh;
Text dayAh;

//Objects to handle connection
//Handles reading from the BT2 Renogy device
BT2Reader bt2Reader;
SOKReader sokReader;
BTDevice *targetedDevices[] = {&bt2Reader,&sokReader};

//state
BLE_SEMAPHORE bleSemaphore;
long lastScrUpdatetime=0;
int renogyCmdSequenceToSend=0;
long lastCheckedTime=0;
long hertzTime=0;
int hertzCount=0;
int currentHz=0;
boolean tiktok=true;

//data
int stateOfCharge;
int stateOfWater;
int rangeForWater;
float currentVolts;
float chargeAmps;
float drawAmps;
float batteryHoursRem;
float waterDaysRem;
float batteryTemperature;
float chargerTemperature;
float batteryAmpValue;
float solarAmpValue;
float alternaterAmpValue;
bool cmos;
bool dmos;
bool heater;

void setup() 
{
	Serial.begin(115200);
	delay(3000);

    //Setup screen
    screen.init();
	screen.setBrightness(STND_BRIGHTNESS);

    //Static labels
    lcd.setTextColor(TFT_WHITE);
    lcd.drawString("Sprinkle Data",(lcd.width()/2)-(lcd.textWidth("Sprinkle Data")), 5, 4);	

    //Init circular meters
	int cntrOffsetY=20;
    int cx=(lcd.width()/2);
    int cy=(lcd.height()/2)+cntrOffsetY;
    centerOutMeter.initMeter(0,20,cx,cy,90,RED2GREEN); 
    centerInMeter.initMeter(0,20,cx,cy,70,GREEN2RED); 

	//Draw online indicator
	setOnlineIndicator(false);

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
    socMeter.drawMeter("SoC", 10, 100, 100, &socConfig, &socFill);

	waterConfig.bmArray=waterBottleBitmap;
	waterConfig.x=lcd.width()-imgWidth-lx;  waterConfig.y=ly;  waterConfig.width=imgWidth;  waterConfig.height=imgHeight;  
	waterConfig.color=WATER_BLUE;
	waterFill.color=WATER_FILL;
	waterFill.rangeColor=WATER_RANGE;	
	waterFill.height=90;
	waterFill.width=60;
	waterFill.start=28;
	waterFill.redraw=true;
    waterMeter.drawMeter("Water", 0, 100, 100, &waterConfig, &waterFill);

	//show moon
	moonConfig.x=socConfig.x+socConfig.width+20+8;
	moonConfig.y=socConfig.y+10;
	moonConfig.bmArray=moonBitmap;
	moonConfig.width=30;
	moonConfig.height=26;
	moonConfig.color=TFT_WHITE;
	lcd.drawBitmap(moonConfig.x, moonConfig.y, moonConfig.bmArray,  moonConfig.width,  moonConfig.height,  moonConfig.color);

	//show calendar
	calendarConfig.x=waterConfig.x-50-8;
	calendarConfig.y=waterConfig.y+10;
	calendarConfig.bmArray=calendarBitmap;
	calendarConfig.width=32;
	calendarConfig.height=32;
	calendarConfig.color=TFT_WHITE;
	lcd.drawBitmap(calendarConfig.x, calendarConfig.y, calendarConfig.bmArray,  calendarConfig.width,  calendarConfig.height,  calendarConfig.color);	

	//Text for night/day
	nightAh.drawCenterText(moonConfig.x+16,moonConfig.y+37,12.4,1,"Ah",2,TFT_WHITE);
	dayAh.drawCenterText(calendarConfig.x+16,calendarConfig.y+37,17.4,1,"Ah",2,TFT_WHITE);

	//sparklines
	int slLen=70; int slHeight=20;
	lcd.fillRect(moonConfig.x-(slLen/2)+(moonConfig.width/2),moonConfig.y-30,slLen,slHeight,TFT_CYAN);
	lcd.fillRect(calendarConfig.x-(slLen/2)+(calendarConfig.width/2),calendarConfig.y-30,slLen,slHeight,TFT_CYAN);

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

	//BLE
	Serial.println("ArduinoBLE (via ESP32-S3) connecting to Renogy BT-2 and SOK battery");
	Serial.println("-----------------------------------------------------\n");
	if (!BLE.begin()) 
	{
		Serial.println("starting Bluetooth® Low Energy module failed!");
		while (1) {delay(1000);};
	}

	//Set callback functions
	BLE.setEventHandler(BLEDiscovered, scanCallback);
	BLE.setEventHandler(BLEConnected, connectCallback);
	BLE.setEventHandler(BLEDisconnected, disconnectCallback);	

	//Setup Renogy BT2
	bt2Reader.setLoggingLevel(BT2READER_VERBOSE);

	// start scanning for peripherals
	delay(2000);
	Serial.println("About to start scanning");	
	BLE.scan(true);  //scan with dupliates
}

/** Scan callback method.  
 */
void scanCallback(BLEDevice peripheral) 
{
    // discovered a peripheral, print out address, local name, and advertised service
	Serial.printf("Found device '%s' at '%s' with uuuid '%s'\n",peripheral.localName().c_str(),peripheral.address().c_str(),peripheral.advertisedServiceUuid().c_str());

	//Checking with both device handlers to see if this is something they're interested in
	bt2Reader.scanCallback(&peripheral,&bleSemaphore);
	sokReader.scanCallback(&peripheral,&bleSemaphore);
}

/** Connect callback method.  Exact order of operations is up to the user.  if bt2Reader attempted a connection
 * it returns true (whether or not it succeeds).  Usually you can then skip any other code in this connectCallback
 * method, because it's unlikely to be relevant for other possible connections, saving CPU cycles
 */ 
void connectCallback(BLEDevice peripheral) 
{
	Serial.printf("Connect callback for '%s'\n",peripheral.address().c_str());
	if(memcmp(peripheral.address().c_str(),bt2Reader.getPeripheryAddress(),6)==0)
	{
		Serial.println("Renogy connect callback");
		if (bt2Reader.connectCallback(&peripheral,&bleSemaphore)) 
		{
			Serial.printf("Connected to BT device %s\n",peripheral.address().c_str());
			bt2Reader.sendStartupCommand(&bleSemaphore);
		}
	}

	if(memcmp(peripheral.address().c_str(),sokReader.getPeripheryAddress(),6)==0)
	{
		Serial.println("SOK connect callback");
		if (sokReader.connectCallback(&peripheral,&bleSemaphore)) 
		{
			Serial.printf("Connected to SOK device %s\n",peripheral.address().c_str());	
		}
	}	

	//Should I stop scanning now?
	boolean stopScanning=true;
	int numOfTargetedDevices=sizeof(targetedDevices)/(sizeof(targetedDevices[0]));
	for(int i=0;i<numOfTargetedDevices;i++)
	{
		if(!targetedDevices[i]->isConnected())
			stopScanning=false;
	}
	if(stopScanning)
	{
		setOnlineIndicator(true);
		Serial.println("All devices connected.  Showing online and stopping scan");
		BLE.stopScan();
	}
}

void disconnectCallback(BLEDevice peripheral) 
{
	bleSemaphore.waitingForConnection=false;
	bleSemaphore.waitingForResponse=false;

	if(memcmp(peripheral.address().c_str(),bt2Reader.getPeripheryAddress(),6)==0)
	{	
		renogyCmdSequenceToSend=0;  //need to start at the beginning since we disconnected
		bt2Reader.disconnectCallback(&peripheral);

		Serial.printf("Disconnected BT2 device %s",peripheral.address().c_str());	
	}

	if(memcmp(peripheral.address().c_str(),sokReader.getPeripheryAddress(),6)==0)
	{	
		Serial.printf("Disconnected SOK device %s",peripheral.address().c_str());
	}	

	setOnlineIndicator(false);
	Serial.println(" - Starting scan again");
	BLE.scan();
}

void mainNotifyCallback(BLEDevice peripheral, BLECharacteristic characteristic)
{
	Serial.printf("Characteristic notify from %s\n",peripheral.address().c_str());
	if(memcmp(peripheral.address().c_str(),bt2Reader.getPeripheryAddress(),6)==0)
	{		
		bt2Reader.notifyCallback(&peripheral,&characteristic,&bleSemaphore);
	}

	if(memcmp(peripheral.address().c_str(),sokReader.getPeripheryAddress(),6)==0)
	{		
		sokReader.notifyCallback(&peripheral,&characteristic,&bleSemaphore); 
	}	
}

void loop() 
{
	//required for the ArduinoBLE library
	BLE.poll();

	//check for BLE timeouts - and disconnect
	if(isTimedout())
	{
		if(bleSemaphore.btDevice->getBLEDevice())
			bleSemaphore.btDevice->getBLEDevice()->disconnect();
		bleSemaphore.waitingForConnection=false;
		bleSemaphore.waitingForResponse=false;
	}

	//load values
	if(millis()>lastScrUpdatetime+SCR_UPDATE_TIME)
	{
		lastScrUpdatetime=millis();
		loadSimulatedValues();
		//loadValues();
		updateLCD();

		//gist:  https://gist.github.com/lovyan03/e6e21d4e65919cec34eae403e099876c
		//touch test  (-1 when no touch.  size/id are always 0)
		//lgfx::v1::touch_point_t tp;
		//lcd.getTouch(&tp);
		//Serial.printf("\nx: %d y:%d s:%d i:%d\n\n",tp.x,tp.y,tp.size,tp.id)		;
	}

	//Time to ask for data again?
	if (tiktok && bt2Reader.isConnected() && millis()>lastCheckedTime+POLL_TIME_MS) 
	{
		lastCheckedTime=millis();
		bt2Reader.sendSolarOrAlternaterCommand(&bleSemaphore);
	}

	if (!tiktok && sokReader.isConnected() && millis()>lastCheckedTime+POLL_TIME_MS) 
	{
		lastCheckedTime=millis();
		sokReader.sendReadCommand(&bleSemaphore);
	}

	//Do we have any data waiting?
	if(bt2Reader.getIsNewDataAvailable())
	{
		bt2Reader.updateValues();
	}
	if(sokReader.getIsNewDataAvailable())
	{
		hertzCount++;
		sokReader.updateValues();
	}

	//toggle to the other device
	tiktok=!tiktok;

	//calc hertz
	if(millis()>hertzTime+1000)
	{
		currentHz=hertzCount;
		hertzTime=millis();
		hertzCount=0;
	}
}

boolean isTimedout()
{
	boolean timedOutFlag=false;

	if((bleSemaphore.startTime+BT_TIMEOUT_MS) > millis()  && (bleSemaphore.waitingForConnection || bleSemaphore.waitingForResponse))
	{
		timedOutFlag=true;
		if(bleSemaphore.waitingForConnection)
		{
			Serial.printf("ERROR: Timed out waiting for connection to %s\n",bleSemaphore.btDevice->getPerifpheryName());
		}
		else if(bleSemaphore.waitingForResponse)
		{
			Serial.printf("ERROR: Timed out waiting for response from %s\n",bleSemaphore.btDevice->getPerifpheryName());
		}
		else
		{
			Serial.println("ERROR: Timed out for an unknown reason");
		}
	}

	return timedOutFlag;
}

void updateLCD()
{
	//update center meter
    centerOutMeter.drawMeter(chargeAmps);
    centerInMeter.drawMeter(drawAmps);
    centerInMeter.drawText("A",chargeAmps-drawAmps);

	//update bitmap meters
    socMeter.updateLevel(stateOfCharge,2,0);
    waterMeter.updateLevel(stateOfWater,rangeForWater,2,0);	

    //update text values
	lcd.setTextFont(4);
	lcd.setTextSize(1);
    volts.drawText((lcd.width()/2)-(lcd.textWidth("99.9V")/2),centerInMeter.getY()+70,currentVolts,1,"V",4,TFT_WHITE);

	//(int x,int y,float value,int dec,const char *label,int font);
	//(BitmapConfig *bmCfg,int offset,float value,int dec,const char *label,int font)
	soc.drawBitmapTextCenter(socMeter.getBitmapConfig(),stateOfCharge,0,"%",2,TFT_WHITE,-1);
    battHoursLeft.drawBitmapTextBottom(socMeter.getBitmapConfig(),20,batteryHoursRem,1," Hrs",2,socMeter.getBitmapConfig()->color);
    waterDaysLeft.drawBitmapTextBottom(waterMeter.getBitmapConfig(),20,waterDaysRem,1," Dys",2,waterMeter.getBitmapConfig()->color);
    hertz.drawRightText(lcd.width()-10,lcd.height()-15,currentHz,0,"Hz",2,TFT_WHITE);

	batteryTemp.updateText(batteryTemperature);
	batteryAmps.updateText(batteryAmpValue);
	chargerTemp.updateText(chargerTemperature);
	solarAmps.updateText(solarAmpValue);
	alternaterAmps.updateText(alternaterAmpValue);

	//Battery heater
	if(heater)  { lcd.drawBitmap(heaterConfig.x,heaterConfig.y,heaterConfig.bmArray,heaterConfig.width,heaterConfig.height,heaterConfig.color);}
	else		{ lcd.drawBitmap(heaterConfig.x,heaterConfig.y,heaterConfig.bmArray,heaterConfig.width,heaterConfig.height,TFT_BLACK);}

	//Update C and D MOS
	if(cmos) {	cmosIndicator.updateCircle(TFT_GREEN);}
	else 	 {	cmosIndicator.updateCircle(TFT_LIGHTGREY);}

	if(dmos) {	dmosIndicator.updateCircle(TFT_GREEN);}
	else	 {	dmosIndicator.updateCircle(TFT_LIGHTGREY); }
}

void loadValues()
{
	stateOfCharge=sokReader.getSoc();
	currentVolts=sokReader.getVolts();
	chargeAmps=bt2Reader.getSolarAmps()+bt2Reader.getAlternaterAmps();
	drawAmps=chargeAmps-sokReader.getAmps();
	if(sokReader.getAmps()<0)
		batteryHoursRem=sokReader.getCapacity()/(sokReader.getAmps()*-1);
	else
		batteryHoursRem=999;

	stateOfWater=random(3);
	if(stateOfWater==0)  stateOfWater=20;
	if(stateOfWater==1)  stateOfWater=50;
	if(stateOfWater==2)  stateOfWater=90;
	waterDaysRem=((double)random(10))/10.0;   // 0 --> 10

	batteryTemperature=sokReader.getTemperature();
	chargerTemperature=bt2Reader.getTemperature();
	batteryAmpValue=sokReader.getAmps();
	solarAmpValue=bt2Reader.getSolarAmps();
	alternaterAmpValue=bt2Reader.getAlternaterAmps();

	heater=sokReader.isHeating();

	cmos=sokReader.isCMOS();
	dmos=sokReader.isDMOS();
}

void loadSimulatedValues()
{
  //let's slow things down a bit
  delay(2000);

  stateOfCharge=random(90)+10;  // 10-->100
  stateOfWater=random(4);
  if(stateOfWater==0)  { stateOfWater=0; rangeForWater=20; }
  if(stateOfWater==1)  { stateOfWater=20; rangeForWater=50; }
  if(stateOfWater==2)  { stateOfWater=50; rangeForWater=90; }
  if(stateOfWater==3)  { stateOfWater=90; rangeForWater=100; }

  currentVolts=((double)random(40)+100.0)/10.0;  // 10.0 --> 14.0
  chargeAmps=((double)random(200))/10.0;  // 0 --> 20.0
  drawAmps=((double)random(200))/10.0;  // 0 --> 20.0
  batteryHoursRem=((double)random(99))/10.0;   // 0 --> 99
  waterDaysRem=((double)random(10))/10.0;   // 0 --> 10

  batteryTemperature=((double)random(90))-20;  // 20 --> 70
  chargerTemperature=((double)random(90))-20;  // 20 --> 70
  batteryAmpValue=((double)random(200))/10.0;  // 0 --> 20.0
  solarAmpValue=((double)random(200))/10.0;  // 0 --> 20.0
  alternaterAmpValue=((double)random(200))/10.0;  // 0 --> 20.0 

  heater=random(2);

  cmos=random(2);
  dmos=random(2);
}

void setOnlineIndicator(bool online)
{
	if(online)
		lcd.drawBitmap(lcd.width()-33, 3, onlineBitmap,  30,  30,  TFT_GREEN);
	else
		lcd.drawBitmap(lcd.width()-33, 3, onlineBitmap,  30,  30,  TFT_DARKGREY);
}