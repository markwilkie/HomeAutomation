//Forked from factory firmware https://www.itead.cc/wiki/File:FWTRX-TAMCUSC-SONOFFSC-ATMEGA328P.zip

/*********************************************************************************************\
  Sonoff Sc
  sc_value[0] DHT11 Humidity
  sc_value[1] DHT11 Temperature in Celsius
  sc_value[2] Light level from 1 (Dark) to 10 (Bright) - inverted from original
  sc_value[3] Noise level from 1 (Quiet) to 10 (Loud)
  sc_value[4] Air Quality level from 1 (Bad) to 10 (Good) - inverted from original
  To ATMEGA328P:
    AT+DEVCONFIG="uploadFreq":1800,"humiThreshold":2,"tempThreshold":1[1B]
    AT+NOTIFY="uploadFreq":1800,"humiThreshold":2,"tempThreshold":1[1B]
      response: AT+NOTIFY=ok[1B]
    AT+SEND=fail[1B]
    AT+SEND=ok[1B]
    AT+STATUS=4[1B]
    AT+STATUS[1B]
    AT+START[1B]
  From ATMEGA328P:
    AT+UPDATE="humidity":42,"temperature":20,"light":7,"noise":3,"dusty":1[1B]
      response: AT+SEND=ok[1B] or AT+SEND=fail[1B]
    AT+STATUS?[1B]
      response: AT+STATUS=4[1B]
  Sequence:
   SC sends:        ATMEGA328P sends:
   AT+START[1B]
                    AT+UPDATE="humidity":42,"temperature":20,"light":7,"noise":3,"dusty":1[1B]
   AT+SEND=ok[1B]
                    AT+STATUS?[1B]
   AT+STATUS=4[1B]
\*********************************************************************************************/

#include "sample_data.h"
#include "upload_data_to_server.h"
#include "MsTimer2.h"
#include "global.h"

// For UNO and others without hardware serial, we must use software serial...
// pin #2 is IN from sensor (TX pin on sensor), leave pin #3 disconnected
// comment these two lines if using hardware serial
#include <SoftwareSerial.h>
SoftwareSerial pmsSerial(15, 3);  //15 is same as A1

// Initialize DHT sensor.
// Note that older versions of this library took an optional third parameter to
// tweak the timings for faster processors.  This parameter is no longer needed
// as the current DHT reading algorithm adjusts itself to work on faster procs.
#include "DHT.h"
DHT dht(6, DHT22);

static bool get_temp_humi_flag = false;
static bool get_ad_value_flag = false;
static bool update_value_flag = false;

void interCallback()
{
    get_ad_value_flag = true;
}
void time2Callback()
{
    static uint8_t couter = 0;
    (couter >= 200)?(couter = 1):(couter++);
    if(couter % 2 == 0)
    {
        get_temp_humi_flag = true;
    }
    update_value_flag = true;       
}
void debugData(void)
{
    Serial.print("temprature: ");
    Serial.println(sensor_dev[3].temp_humi_average[0]);
    Serial.print("humidity: ");
    Serial.println(sensor_dev[3].temp_humi_average[1]);
    Serial.print("light: ");
    Serial.println(sensor_dev[1].average_value);
    Serial.print("noise: ");
    Serial.println(sensor_dev[2].average_value);
    Serial.print("dusty: ");
    Serial.println(sensor_dev[0].average_value);
}
void setup() {
  // put your setup code here, to run once:
    Serial.begin(19200);
    initDevice();
    attachInterrupt(0, interCallback, RISING);
    MsTimer2::set(1000, time2Callback); // 500ms period
    MsTimer2::start();

    //airsensor setup
    pmsSerial.begin(9600);

    //temp humi
    dht.begin();
}

void loop() {
  // put your main code here, to run repeatedly:
    readUart();
    if(get_temp_humi_flag)
    {
        get_temp_humi_flag = false;
        getTempHumi();
    }
    if(get_ad_value_flag)
    {
        get_ad_value_flag = false;
        getAdcSensorValue(); 
    }
    getDust();
    if(update_value_flag)
    {
        update_value_flag = false;
        //debugData();
        uploadSensorDataToServer();
    }
}
