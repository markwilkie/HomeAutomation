#ifndef GPS_h
#define GPS_h

#include <Adafruit_GPS.h>
#include <Wire.h>

/*
  GPS Module: Adafruit PA1010D (MTK3333) over I2C
  I2C address: 0x10
  Library: Adafruit GPS Library (install via Arduino Library Manager)
  
  Provides lat, lon, altitude (GPS), speed, fix quality, and satellite count.
  Barometric altitude from MPL3115A2 is preferred for elevation accuracy —
  GPS altitude is available here as a cross-reference but should not be used
  as the primary elevation source in GPX files.
*/

#define GPS_I2C_ADDRESS 0x10
#define GPS_UPDATE_RATE 1000   // How often to check for new GPS data (ms)

class GPSModule
{
public:
    void init(int refreshTicks);
    void setup();
    void update();
    bool isOnline();
    bool hasFix();

    // Position
    float getLatitude();
    float getLongitude();
    float getGPSAltitudeMeters();   // GPS-reported altitude (less accurate than baro)
    float getSpeedKnots();
    float getCourse();

    // Quality
    int   getFixQuality();     // 0=none, 1=GPS, 2=DGPS
    int   getSatellites();

private:
    Adafruit_GPS gps = Adafruit_GPS(&Wire);
    bool online = false;
    bool fix = false;

    // Cached values
    float latitude = 0;
    float longitude = 0;
    float gpsAltitude = 0;     // meters
    float speedKnots = 0;
    float course = 0;
    int   fixQuality = 0;
    int   satellites = 0;

    // Timing
    int refreshTicks;
    unsigned long nextTickCount = 0;
};

#endif
