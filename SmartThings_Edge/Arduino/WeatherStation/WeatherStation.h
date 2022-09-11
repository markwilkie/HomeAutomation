#ifndef weatherstation_h
#define weatherstation_h

//defines
#define CYCLETIME             600               // interval in seconds sensors will be read and wifi will attempt to POST data to the hub  (deep sleeps inbetween)
#define POWERSAVERTIME        (1*30*60)         // interval in seconds that esp will deep sleep during power saver mode - ~70 minutes max  (voltage below a threshold)
#define HTTPSERVERTIME        30                // time blocking in server listen for handshaking while in loop
#define AIRTIME               (3600-CYCLETIME)  // interval in seconds we'll wake up the power hungry air sensor  (minus WIFITIME because of warmup)
#define SWITCHTOTOBAT         4.5               // voltage that ESP will switch to battery power (e.g. turn off boost)
#define SWITCHTOTOCAP         4.8               // voltage that ESP will switch to capacitor power (turn on boost)
#define POWERSAVERVOLTAGE     3.0               // voltage that ESP will go into power saving mode on  (long deep sleeps, no pms5003)

//globals in ULP which survive deep sleep
extern RTC_DATA_ATTR ULP ulp;  //low power processor
extern RTC_DATA_ATTR bool handshakeRequired;  //will put the sketch into server mode  (power hungry)
extern RTC_DATA_ATTR unsigned long epoch;  //Epoch from hub
extern RTC_DATA_ATTR char hubAddress[30];

//methods
char *getTimeString(time_t now) ;

#endif
