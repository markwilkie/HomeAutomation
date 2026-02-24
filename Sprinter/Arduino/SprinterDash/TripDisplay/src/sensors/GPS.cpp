#include <Arduino.h>
#include "GPS.h"
#include "../Globals.h"

void GPSModule::init(int _refreshTicks)
{
    refreshTicks = _refreshTicks;
}

void GPSModule::setup()
{
    if (!gnss.begin())
    {
        logger.log(ERROR, "Could not find TEL0157 GPS at 0x20");
        online = false;
        return;
    }

    gnss.enablePower();
    gnss.setGnss(eGPS_BeiDou_GLONASS);
    gnss.setRgbOn();

    online = true;
    logger.log(INFO, "GPS TEL0157 (L76K) initialized at 0x20");
}

bool GPSModule::update()
{
    if (millis() < nextTickCount)
        return false;
    nextTickCount = millis() + refreshTicks;

    sLonLat_t lat = gnss.getLat();
    sLonLat_t lon = gnss.getLon();
    satellites = gnss.getNumSatUsed();
    cachedUTC  = gnss.getUTC();
    cachedDate = gnss.getDate();

    // Consider having a fix when we have at least 1 satellite and non-zero coords
    fix = (satellites > 0 && (lat.latitudeDegree != 0.0 || lon.lonitudeDegree != 0.0));

    if (fix)
    {
        // Apply sign based on direction (S = negative lat, W = negative lon)
        latitude  = (lat.latDirection == 'S') ? -lat.latitudeDegree : lat.latitudeDegree;
        longitude = (lon.lonDirection == 'W') ? -lon.lonitudeDegree : lon.lonitudeDegree;
        gpsAltitude = gnss.getAlt();
        speedKnots  = gnss.getSog();
        course      = gnss.getCog();

        if (!online)
        {
            logger.log(INFO, "GPS has fix!  Lat: %f  Lon: %f  Sats: %d", 
                       (double)latitude, (double)longitude, satellites);
            online = true;
        }
    }

    return true;
}

bool GPSModule::isOnline()
{
    return online;
}

bool GPSModule::hasFix()
{
    return fix;
}

float GPSModule::getLatitude()
{
    return latitude;
}

float GPSModule::getLongitude()
{
    return longitude;
}

float GPSModule::getGPSAltitudeMeters()
{
    return gpsAltitude;
}

float GPSModule::getSpeedKnots()
{
    return speedKnots;
}

float GPSModule::getCourse()
{
    return course;
}

int GPSModule::getSatellites()
{
    return satellites;
}

bool GPSModule::hasValidTime()
{
    // Uses cached date from last update() — no I2C read
    return fix && cachedDate.year > 0;
}

uint32_t GPSModule::getGPSSecondsSince2000()
{
    // Uses cached UTC/date from last update() — no I2C read
    DateTime dt(cachedDate.year, cachedDate.month, cachedDate.date,
                cachedUTC.hour, cachedUTC.minute, cachedUTC.second);
    return dt.secondstime();
}
