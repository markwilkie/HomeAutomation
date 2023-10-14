#ifndef LAYOUT_H
#define LAYOUT_H

#include <ESP32Time.h>
#include <LovyanGFX.hpp>
#include "LGFX_ST32-SC01Plus.hpp"
#include "bitmaps.h"
#include "BitmapMeter.h"
#include "CircularMeter.h"
#include "SparkLine.h"
#include "Screen.h"
#include "DisplayData.h"

class Layout 
{
    public:
        void init();
        void drawInitialScreen();
        void updateLCD(ESP32Time *);
        void setWifiIndicator(boolean online);
        void setBLEIndicator(int color);
        void addToDayAhSpark(uint16_t value);
        void addToNightAhSpark(uint16_t value);

        DisplayData displayData;

    private:

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
        Text dateTime;  
};

#endif