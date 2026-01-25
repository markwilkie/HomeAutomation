#ifndef LAYOUT_H
#define LAYOUT_H

/*
 * Layout - Display layout manager and UI renderer
 * 
 * This class handles all visual rendering on the 480x320 TFT display.
 * 
 * SCREEN LAYOUT (Main Dashboard):
 * ┌──────────────────────────────────────────────────────────┐
 * │  DateTime          Sprinkle Data          WiFi  BLE     │
 * ├──────────────────────────────────────────────────────────┤
 * │ ┌────┐ ┌────┐    [Moon]  [Calendar]    ┌────┐ ┌────┐    │
 * │ │Bat1│ │Bat2│   NightAh   DayAh        │Gas │ │Water│   │
 * │ │ %  │ │ %  │  [sparkline][sparkline]  │ %  │ │  %  │   │
 * │ │    │ │    │                          │    │ │     │   │
 * │ │    │ │    │      ┌───────────┐       │    │ │     │   │
 * │ │    │ │    │      │  Center   │       │    │ │     │   │
 * │ │    │ │    │      │  Meters   │       │    │ │     │   │
 * │ │    │ │    │      │  ±Amps    │       │    │ │     │   │
 * │ │    │ │    │      │  Volts    │       │    │ │     │   │
 * │ └────┘ └────┘      └───────────┘       └────┘ └─────┘   │
 * │ V1  H1  V2  H2                          D     D         │
 * │      [Battery Icon]      [Van/Solar Icons]              │
 * │      Temp  Amps          Solar  Alt  ChargerTemp        │
 * │      [C/D MOS indicators]                               │
 * └──────────────────────────────────────────────────────────┘
 * 
 * TOUCH REGIONS (detected by is*Region methods):
 * - isBLERegion: Top-right corner for BLE toggle
 * - isVanRegion: Van/solar icons for BT2 detail screen
 * - isBatteryIconRegion: Battery icon for battery mode cycling
 * - isCenterRegion: Center area for screen reset (long touch)
 * 
 * COLOR CODING:
 * - White: Device online, current data
 * - Grey: Device stale or disconnected (trying to reconnect)
 * - Red: Device in backoff (offline)
 */

#include <ESP32Time.h>
#include <LovyanGFX.hpp>
#include "LGFX_ST32-SC01Plus.hpp"
#include "bitmaps.h"
#include "BitmapMeter.h"
#include "BarMeter.h"
#include "CircularMeter.h"
#include "SparkLine.h"
#include "Screen.h"
#include "DisplayData.h"
#include "../logging/logger.h"
#include "../ble/BT2Reader.h"

//Screen
extern Screen screen;

class Layout 
{
    public:
        void init();
        void drawInitialScreen();
        void updateLCD(ESP32Time *);
        void updateBitmaps();
        void setWifiIndicator(boolean online);
        void setBLEIndicator(int color);
        bool isBLERegion(int x,int y);
        bool isWaterRegion(int x,int y);
        bool isVanRegion(int x, int y);
        bool isBatteryIconRegion(int x,int y);
        bool isCenterRegion(int x, int y);
        void showBT2Detail(BT2Reader* bt2Reader);
        SparkLine<float> *getDaySparkPtr();
        SparkLine<float> *getNightSparkPtr();

        DisplayData displayData;

    private:

        float cTof(float c);
        uint16_t getStatusColor(DeviceStatus status);  // Get text color based on device status

        //Meters
        CircularMeter centerOutMeter;
        CircularMeter centerInMeter;
        BarMeter battery1Meter;
        BarMeter battery2Meter;
        BarMeter waterMeter;
        BarMeter gasMeter;

        BitmapConfig battery1Config;
        BitmapConfig battery2Config;
        FillConfig socFill;
        BitmapConfig waterConfig;
        BitmapConfig gasConfig;
        FillConfig waterFill;

        BitmapConfig heaterConfig;
        BitmapConfig calendarConfig;
        BitmapConfig moonConfig;

        Primitive cmosIndicator;
        Primitive dmosIndicator;

        //Dynamic text
        Text soc;
        Text soc2;
        Text battVolts;
        Text battVolts2;
        Text battHoursLeft;
        Text battHoursLeft2;
        Text waterDaysLeft;
        Text waterPercent;
        Text gasPercent;
        Text gasDaysLeft;
        Text battery1Title;
        Text battery2Title;
        Text waterTitle;
        Text gasTitle;
        Text volts;
        Text hertz;
        Text batteryTemp;
        Text batteryAmps;
        Text chargerTemp;
        Text solarAmps;
        Text alternaterAmps;
        Text nightAh;
        Text dayAh;  
        Text dateTime;  
};

#endif