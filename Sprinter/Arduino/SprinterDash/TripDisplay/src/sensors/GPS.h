#ifndef GPS_h
#define GPS_h

#include <DFRobot_GNSS.h>
#include <Wire.h>
#include "RTClib.h"

/*
  GPS Module: DFRobot TEL0157 (Quectel L76K) over I2C
  I2C address: 0x20
  Library: DFRobot_GNSS (https://github.com/DFRobot/DFRobot_GNSS)
  
  Provides lat, lon, altitude (GPS), speed, fix quality, and satellite count.
  Supports GPS + GLONASS + BeiDou for faster fix and better accuracy.
  Barometric altitude from MPL3115A2 is preferred for elevation accuracy —
  GPS altitude is available here as a cross-reference but should not be used
  as the primary elevation source in GPX files.
*/

#define GPS_UPDATE_RATE 1000   // How often to check for new GPS data (ms)

class GPSModule
{
public:
    void init(int refreshTicks, int _offset=0);
    void setup();
    bool update();   // returns true when new data was polled
    bool isOnline();
    bool hasFix();

    // Position
    float getLatitude();
    float getLongitude();
    float getGPSAltitudeMeters();   // GPS-reported altitude (less accurate than baro)
    float getSpeedKnots();
    float getCourse();

    // Seed with last known position from EEPROM (used before first fix)
    void setLastKnownPosition(float lat, float lon);

    // Time (from GPS NMEA sentences)
    bool     hasValidTime();          // true when GPS has a plausible date/time
    uint32_t getGPSSecondsSince2000(); // seconds since Jan 1 2000 from GPS clock

    // Quality
    int   getSatellites();

private:
    DFRobot_GNSS_I2C gnss = DFRobot_GNSS_I2C(&Wire, GNSS_DEVICE_ADDR);
    bool online = false;
    bool fix = false;
    bool firstFixLogged = false;

    // Cached values
    float latitude = 0;
    float longitude = 0;
    float gpsAltitude = 0;     // meters
    float speedKnots = 0;
    float course = 0;
    int   satellites = 0;

    // Timing
    int refreshTicks;
    unsigned long nextTickCount = 0;

    // Cached time from last update()
    sTim_t cachedUTC  = {};
    sTim_t cachedDate = {};
};

#endif
