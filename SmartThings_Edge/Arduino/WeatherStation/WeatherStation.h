#ifndef weatherstation_h
#define weatherstation_h

//defines
#define CYCLETIME             600                   // interval in seconds sensors will be read and wifi will attempt to POST data to the hub  (deep sleeps inbetween)
#define POWERSAVERTIME        5400UL                // interval in seconds that esp will deep sleep during power saver mode (5400 is 1.5 hours)
#define HTTPSERVERTIME        30                    // time blocking in server listen for handshaking while in loop
#define AIRWARMUPTIME         30                    // interval in seconds that we'll power the sensor and let it warm up and settle down
#define AIRREADTIME           (3600-AIRWARMUPTIME)  // interval in seconds we'll wake up the power hungry air sensor  (minus warmup)
#define SWITCHTOTOBAT         4.9                   // Capacitor voltage that ESP will switch to battery power (e.g. turn off boost)
#define SWITCHTOTOCAP         5.2                   // Capacitor voltage that ESP will switch to capacitor power (turn on boost)
#define PMSMINVOLTAGE         2.1                   // Won't try and get a reading if capacitors (pre boost circuit) are below this voltage
#define POWERSAVERVOLTAGE     3.55                  // VCC voltage that ESP will go into power saving mode on  (long deep sleeps)

//globals in ULP which survive deep sleep
extern RTC_DATA_ATTR ULP ulp;  //low power processor
extern RTC_DATA_ATTR bool handshakeRequired;  //will put the sketch into server mode  (power hungry)
extern RTC_DATA_ATTR unsigned long epoch;  //Epoch from hub
extern RTC_DATA_ATTR char hubAddress[30];

//methods
char *getTimeString(time_t now) ;

#endif
