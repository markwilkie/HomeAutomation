#include <Arduino.h>
#include "GPS.h"
#include "../Globals.h"

void GPSModule::init(int _refreshTicks)
{
    refreshTicks = _refreshTicks;
}

void GPSModule::setup()
{
    if (!gps.begin(GPS_I2C_ADDRESS))
    {
        logger.log(ERROR, "Could not find PA1010D GPS at 0x10");
        online = false;
        return;
    }

    gps.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
    gps.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);
    gps.sendCommand(PGCMD_ANTENNA);

    online = true;
    logger.log(INFO, "GPS PA1010D initialized at 0x10");
}

void GPSModule::update()
{
    while (gps.available())
    {
        gps.read();
    }

    if (millis() < nextTickCount)
        return;
    nextTickCount = millis() + refreshTicks;

    if (gps.newNMEAreceived())
    {
        if (!gps.parse(gps.lastNMEA()))
            return;
    }

    fix = gps.fix;
    fixQuality = gps.fixquality;
    satellites = gps.satellites;

    if (fix)
    {
        latitude = gps.latitudeDegrees;
        longitude = gps.longitudeDegrees;
        gpsAltitude = gps.altitude;
        speedKnots = gps.speed;
        course = gps.angle;

        if (!online)
        {
            logger.log(INFO, "GPS has fix!  Lat: %f  Lon: %f  Sats: %d", 
                       (double)latitude, (double)longitude, satellites);
            online = true;
        }
    }
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

int GPSModule::getFixQuality()
{
    return fixQuality;
}

int GPSModule::getSatellites()
{
    return satellites;
}

bool GPSModule::hasValidTime()
{
    // GPS year is 2-digit (26 = 2026). Accept anything > 0 with a valid fix.
    return fix && gps.year > 0;
}

uint32_t GPSModule::getGPSSecondsSince2000()
{
    DateTime dt(2000 + gps.year, gps.month, gps.day,
                gps.hour, gps.minute, gps.seconds);
    return dt.secondstime();
}
