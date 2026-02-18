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
  
  Usage:
    GPSModule gps;
    gps.init(GPS_UPDATE_RATE);   // set refresh rate in ms
    gps.setup();                 // call once in setup()
    gps.update();                // call every loop() — drains NMEA buffer
    if (gps.hasFix()) {
        float lat = gps.getLatitude();   // decimal degrees, signed
        float lon = gps.getLongitude();  // decimal degrees, signed
    }
  
  Important: update() must be called as often as possible (every loop iteration)
  because the PA1010D buffers NMEA sentences internally and can overflow if 
  not read promptly.  The actual data caching (lat/lon/etc.) only happens at 
  the configured refresh rate, but the I2C reads happen every call.

  Wiring:
    PA1010D SDA → ESP32 SDA (shared I2C bus with barometer, RTC, pitot)
    PA1010D SCL → ESP32 SCL
    PA1010D VIN → 3.3V
    PA1010D GND → GND
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
