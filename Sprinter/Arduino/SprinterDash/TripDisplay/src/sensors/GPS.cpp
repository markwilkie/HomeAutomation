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
        // Raw unsigned degree values from the TEL0157
        latitude  = lat.latitudeDegree;
        longitude = lon.lonitudeDegree;

        // TEL0157 BUG: direction characters are swapped between getLat/getLon.
        // getLat().latDirection actually contains the LONGITUDE direction (W/E)
        // getLon().lonDirection actually contains the LATITUDE direction (N/S)
        char latD = lon.lonDirection;   // N or S (swapped from lon register)
        char lonD = lat.latDirection;   // W or E (swapped from lat register)
        if (latD == 'S' || latD == 's') latitude  = -latitude;
        if (lonD == 'W' || lonD == 'w') longitude = -longitude;

        gpsAltitude = gnss.getAlt();
        speedKnots  = gnss.getSog();
        course      = gnss.getCog();

        if (!firstFixLogged)
        {
            firstFixLogged = true;
            // Logger doesn't support %X or width specifiers — use %d for hex-like diag
            logger.log(INFO, "GPS fix! Sats:%d latDir=%c(%d) lonDir=%c(%d)",
                       satellites,
                       latD, (int)latD,
                       lonD, (int)lonD);
            logger.log(INFO, "GPS raw lat:%f lon:%f", (double)lat.latitudeDegree, (double)lon.lonitudeDegree);
            logger.log(INFO, "GPS signed lat:%f lon:%f", (double)latitude, (double)longitude);
        }

        if (!online)
            online = true;
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
