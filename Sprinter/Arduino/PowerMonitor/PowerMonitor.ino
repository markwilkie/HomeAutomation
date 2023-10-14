#include <ESP32Time.h>  // https://github.com/fbiego/ESP32Time/blob/main/examples/esp32_time/esp32_time.ino

#include "src/ble/ble.h"
#include "src/display/display.h"
#include "src/wifi/wifi.h"
#include "src/logging/logging.h"

#define TIMEAPI_URL "http://worldtimeapi.org/api/timezone/America/Los_Angeles"

#define POLL_TIME_MS	500
#define SCR_UPDATE_TIME 2000
#define BT_TIMEOUT_MS	5000
#define SPARK_UPD_TIME	250

//Objects to handle connection
//Handles reading from the BT2 Renogy device
BT2Reader bt2Reader;
SOKReader sokReader;
BTDevice *targetedDevices[] = {&bt2Reader,&sokReader};

VanWifi wifi;
ESP32Time rtc(0);
Layout layout;

Logger logger;

//state
BLE_SEMAPHORE bleSemaphore;
long lastScrUpdatetime=0;
long lastSparkUpdateTime=0;
int renogyCmdSequenceToSend=0;
long lastCheckedTime=0;
long hertzTime=0;
int hertzCount=0;
boolean tiktok=true;

int sparkCount=0;

void setup() 
{
	Serial.begin(115200);
	delay(3000);

	Serial.println("Starting wifi...");
    wifi.startWifi();

    Serial.println("Getting time...");
	setTime();

	//init screen and draw initial form
	layout.init();
	layout.drawInitialScreen();

	//Start BLE
	Serial.println("ArduinoBLE (via ESP32-S3) connecting to Renogy BT-2 and SOK battery");
	Serial.println("-----------------------------------------------------\n");
	if (!BLE.begin()) 
	{
		Serial.println("ERROR: Starting Bluetooth® Low Energy module failed!.  Restarting in 10 seconds.");
		delay(10000);
		ESP.restart();
	}

	//Set BLE callback functions
	BLE.setEventHandler(BLEDiscovered, scanCallback);
	BLE.setEventHandler(BLEConnected, connectCallback);
	BLE.setEventHandler(BLEDisconnected, disconnectCallback);	

	// start scanning for peripherals
	delay(2000);
	Serial.println("Starting BLE scan");	
	BLE.scan(true);  //scan with duplicates - meaning, it'll keep showing the same ones over and over
}

void setTime()
{
	if(!wifi.isConnected())
		return;

    DynamicJsonDocument timeDoc=wifi.sendGetMessage(TIMEAPI_URL);	
	long epoch=timeDoc["unixtime"].as<long>();
    long offset=timeDoc["raw_offset"].as<long>();
    long dstOffset=timeDoc["dst_offset"].as<long>();

	rtc.offset=offset+dstOffset;		
	rtc.setTime(epoch);
	Serial.println(rtc.getTime("%A, %B %d %Y %H:%M:%S"));
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

	//We're at least partially connected
	layout.setBLEIndicator(TFT_YELLOW);

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
		layout.setBLEIndicator(TFT_BLUE);
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

	layout.setBLEIndicator(TFT_DARKGRAY);
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

	//time to update spark lines?
	//   -- use RTC to determine if night  (10pm -- 7am??)
	if(millis()>lastSparkUpdateTime+SPARK_UPD_TIME)
	{
		lastSparkUpdateTime=millis();
		layout.addToDayAhSpark(random(8,15));
		layout.addToNightAhSpark(random(3, 5));

		sparkCount++;
		if(sparkCount==40)
		{
			sparkCount=0;
			layout.addToDayAhSpark(100);
			layout.addToNightAhSpark(25);
		}
	}

	//load values
	if(millis()>lastScrUpdatetime+SCR_UPDATE_TIME)
	{
		lastScrUpdatetime=millis();
		loadSimulatedValues();
		//loadValues();
		layout.updateLCD(&rtc);
		layout.setWifiIndicator(wifi.isConnected());

		//gist:  https://gist.github.com/lovyan03/e6e21d4e65919cec34eae403e099876c
		//touch test  (-1 when no touch.  size/id are always 0)
		//lgfx::v1::touch_point_t tp;
		//lcd.getTouch(&tp);
		//Serial.printf("\nx: %d y:%d s:%d i:%d\n\n",tp.x,tp.y,tp.size,tp.id)		;

		//If rtc is stale, be sure and update it from the interwebs
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
		layout.displayData.currentHertz=hertzCount;
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

void loadValues()
{
	layout.displayData.stateOfCharge=sokReader.getSoc();
	layout.displayData.currentVolts=sokReader.getVolts();
	layout.displayData.chargeAmps=bt2Reader.getSolarAmps()+bt2Reader.getAlternaterAmps();
	layout.displayData.drawAmps=layout.displayData.chargeAmps-sokReader.getAmps();
	if(sokReader.getAmps()<0)
		layout.displayData.batteryHoursRem=sokReader.getCapacity()/(sokReader.getAmps()*-1);
	else
		layout.displayData.batteryHoursRem=999;

	layout.displayData.stateOfWater=random(3);
	if(layout.displayData.stateOfWater==0)  layout.displayData.stateOfWater=20;
	if(layout.displayData.stateOfWater==1)  layout.displayData.stateOfWater=50;
	if(layout.displayData.stateOfWater==2)  layout.displayData.stateOfWater=90;
	layout.displayData.waterDaysRem=((double)random(10))/10.0;   // 0 --> 10

	layout.displayData.batteryTemperature=sokReader.getTemperature();
	layout.displayData.chargerTemperature=bt2Reader.getTemperature();
	layout.displayData.batteryAmpValue=sokReader.getAmps();
	layout.displayData.solarAmpValue=bt2Reader.getSolarAmps();
	layout.displayData.alternaterAmpValue=bt2Reader.getAlternaterAmps();

	layout.displayData.heater=sokReader.isHeating();

	layout.displayData.cmos=sokReader.isCMOS();
	layout.displayData.dmos=sokReader.isDMOS();
}

void loadSimulatedValues()
{
  //let's slow things down a bit
  delay(2000);

  layout.displayData.stateOfCharge=random(90)+10;  // 10-->100
  layout.displayData.stateOfWater=random(4);
  if(layout.displayData.stateOfWater==0)  { layout.displayData.stateOfWater=0; layout.displayData.rangeForWater=20; }
  if(layout.displayData.stateOfWater==1)  { layout.displayData.stateOfWater=20; layout.displayData.rangeForWater=50; }
  if(layout.displayData.stateOfWater==2)  { layout.displayData.stateOfWater=50; layout.displayData.rangeForWater=90; }
  if(layout.displayData.stateOfWater==3)  { layout.displayData.stateOfWater=90; layout.displayData.rangeForWater=100; }

  layout.displayData.currentVolts=((double)random(40)+100.0)/10.0;  // 10.0 --> 14.0
  layout.displayData.chargeAmps=((double)random(200))/10.0;  // 0 --> 20.0
  layout.displayData.drawAmps=((double)random(200))/10.0;  // 0 --> 20.0
  layout.displayData.batteryHoursRem=((double)random(99))/10.0;   // 0 --> 99
  layout.displayData.waterDaysRem=((double)random(10))/10.0;   // 0 --> 10

  layout.displayData.batteryTemperature=((double)random(90))-20;  // 20 --> 70
  layout.displayData.chargerTemperature=((double)random(90))-20;  // 20 --> 70
  layout.displayData.batteryAmpValue=((double)random(200))/10.0;  // 0 --> 20.0
  layout.displayData.solarAmpValue=((double)random(200))/10.0;  // 0 --> 20.0
  layout.displayData.alternaterAmpValue=((double)random(200))/10.0;  // 0 --> 20.0 

  layout.displayData.heater=random(2);

  layout.displayData.cmos=random(2);
  layout.displayData.dmos=random(2);
}