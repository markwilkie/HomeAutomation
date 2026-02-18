#include "GPS.h"

//
// GPS.ino - Adafruit PA1010D (MTK3333) I2C GPS driver
//
// NMEA sentence types enabled:
//   RMC - Recommended Minimum (lat, lon, speed, course, date/time)
//   GGA - Global Positioning System Fix Data (fix quality, satellites, altitude)
//
// The PA1010D updates at 1Hz by default.  We read NMEA data as fast as 
// possible to prevent the internal 255-byte buffer from overflowing,
// but only parse and cache values at our configured refresh rate.
//

extern Logger logger;

void GPSModule::init(int _refreshTicks)
{
    refreshTicks = _refreshTicks;
}

void GPSModule::setup()
{
    // begin() initializes I2C communication with the PA1010D at 0x10
    // Returns false if the device is not found on the bus
    if (!gps.begin(GPS_I2C_ADDRESS))
    {
        logger.log(ERROR, "Could not find PA1010D GPS at 0x10");
        online = false;
        return;
    }

    // Enable RMC (position+time) and GGA (fix+altitude+satellites)
    // This gives us everything we need without excessive NMEA traffic
    gps.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);

    // 1 Hz update rate (GPS fixes per second)
    // Can be increased to 10Hz with PMTK_SET_NMEA_UPDATE_10HZ but
    // 1Hz is sufficient for vehicle tracking and conserves I2C bandwidth
    gps.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);

    // Request antenna status updates (useful for debugging antenna issues)
    gps.sendCommand(PGCMD_ANTENNA);

    online = true;
    logger.log(INFO, "GPS PA1010D initialized at 0x10");
}

void GPSModule::update()
{
    // IMPORTANT: Read all available NMEA data every loop iteration.
    // The PA1010D has a small internal buffer (~255 bytes).  If we don't
    // read fast enough, sentences get dropped and we lose fix data.
    // The gps.read() calls are cheap — just I2C byte reads.
    while (gps.available())
    {
        gps.read();
    }

    // Only parse and cache position values at our configured refresh rate.
    // This separates the "drain the buffer" concern (every loop) from the
    // "process the data" concern (at refresh interval).
    if (millis() < nextTickCount)
        return;
    nextTickCount = millis() + refreshTicks;

    // Check if a complete NMEA sentence was received since last check
    if (gps.newNMEAreceived())
    {
        if (!gps.parse(gps.lastNMEA()))
            return; // bad checksum or incomplete sentence, try again next time
    }

    fix = gps.fix;
    fixQuality = gps.fixquality;
    satellites = gps.satellites;

    if (fix)
    {
        // latitudeDegrees/longitudeDegrees are already in decimal degrees (signed)
        latitude = gps.latitudeDegrees;
        longitude = gps.longitudeDegrees;
        gpsAltitude = gps.altitude;        // meters
        speedKnots = gps.speed;            // knots
        course = gps.angle;                // degrees

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
