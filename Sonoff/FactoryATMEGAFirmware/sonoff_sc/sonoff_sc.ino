#include "sample_data.h"
#include "upload_data_to_server.h"
#include "MsTimer2.h"
#include "global.h"

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
    if(update_value_flag)
    {
        update_value_flag = false;
        //debugData();
        uploadSensorDataToServer();
    }
}
