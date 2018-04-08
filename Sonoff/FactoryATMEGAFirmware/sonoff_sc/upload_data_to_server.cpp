#include "global.h"
#include "upload_data_to_server.h"

static bool network_status_ok  = false;
static uint16_t upload_freq = 1800;
static uint8_t humithreshold = 2;
static uint8_t tempthreshold = 1;
static int8_t last_temp_humi_average[2] = {0};
static bool first_update = false;
static void sendData(void)
{
    Serial.write("AT+UPDATE=\"humidity\":");
    Serial.print(String(sensor_dev[3].temp_humi_average[1]));
    Serial.write(",\"temperature\":");
    Serial.print(String(sensor_dev[3].temp_humi_average[0]));
    Serial.write(",\"light\":");
    Serial.print(String(sensor_dev[1].level));
    Serial.write(",\"noise\":");
    Serial.print(String(sensor_dev[2].level));
    Serial.write(",\"dusty\":");
    Serial.print(String(sensor_dev[0].level));
    Serial.write(0x1B);
}
static void checkNetwork(void)
{
    Serial.write("AT+STATUS?");
    Serial.write(0x1b);
}
void syncData(void)
{
    uint8_t i = 0;
    for(i = 0; i < 3; i++)
    {
        sensor_dev[i].last_level = sensor_dev[i].level;  
    }
    last_temp_humi_average[0] = sensor_dev[3].temp_humi_average[0];
    last_temp_humi_average[1] = sensor_dev[3].temp_humi_average[1];
}
void uploadSensorDataToServer(void)
{
    static uint16_t couter = 0;
    static uint8_t diff_couter = 0;
    couter++;
    if(network_status_ok && couter == upload_freq)
    {
        syncData();
        sendData();
        couter = 0;
    }
    else if(network_status_ok && (abs(sensor_dev[3].temp_humi_average[0] - last_temp_humi_average[0]) >= tempthreshold
          || abs(sensor_dev[3].temp_humi_average[1] - last_temp_humi_average[1]) >= humithreshold
          || sensor_dev[0].level != sensor_dev[0].last_level
          || sensor_dev[1].level != sensor_dev[1].last_level
          || sensor_dev[2].level != sensor_dev[2].last_level
          ))
    {
          diff_couter ++;
          if(diff_couter >= 3)
         {
              syncData();
              sendData();
              couter = 0;
              diff_couter = 0;
          }
    }
    else if(couter % 10 == 0)
    {
        checkNetwork();
    }
    else
    {
        diff_couter = 0; 
     }
}
String getString(String *src,char *start,char *end)
{
    int16_t index1;
    String dst = "";
    if(NULL == src || NULL == start)
    {
        return dst;
    }
    if(-1 != (index1 = src->indexOf(start)))
    {
        int16_t index2 = index1 + strlen(start);
        if(-1 == src->indexOf(end,index2))
        {
            dst = src->substring(index2);
        }
        else
        {
            dst = src->substring(index2 ,src->indexOf(end,index2));
        }
    }
    return dst;
}
void getDevConfigParam(String *rec_string)
{
    String string_uploadfreq = "";
    String string_humithreshold = "";
    String string_tempthreshold = "";
    string_uploadfreq = getString(rec_string,"\"uploadFreq\":",",");
    if(!string_uploadfreq.equals(""))
    {
        upload_freq = string_uploadfreq.toInt();
    }
    string_humithreshold = getString(rec_string,"\"humiThreshold\":",",");
    if(!string_humithreshold.equals(""))
    {
        humithreshold = string_humithreshold.toInt();
    }
    string_tempthreshold = getString(rec_string,"\"tempThreshold\":",",");
    if(!string_tempthreshold.equals(""))
    {
        tempthreshold = string_tempthreshold.toInt();
    }
}
void readUart(void)
{
    static bool get_at_flag = false;
    static String rec_string = "";
    int16_t index1;
    
    while ((Serial.available() > 0))
    {
        char a = (char)Serial.read();
        rec_string +=a;
        
        if(a == 0x1B)
        {
             break;
        }
    }
    if(-1 != rec_string.indexOf(0x1B))
    {
        if(-1 != (index1 = rec_string.indexOf("AT+DEVCONFIG=")))
        {
            network_status_ok = true;
            getDevConfigParam(&rec_string);
        }
        else if(-1 != (index1 = rec_string.indexOf("AT+NOTIFY=")))
        {
            getDevConfigParam(&rec_string);
            Serial.write("AT+NOTIFY=ok");
            Serial.write(0x1b);
        }
        else if(-1 != (index1 = rec_string.indexOf("AT+SEND=fail")))
        {
            network_status_ok = false;
        }
        else if(-1 != (index1 = rec_string.indexOf("AT+STATUS=4")))
        {
            network_status_ok = true;
            if(first_update)
            {
                first_update = false;
                last_temp_humi_average[0] = 0;
                last_temp_humi_average[1] = 0;
            }
        }
        else if(-1 != (index1 = rec_string.indexOf("AT+STATUS")))
        {
              first_update = true;
              network_status_ok = false;
        }
        else if(-1 != (index1 = rec_string.indexOf("AT+START")))
        {
              network_status_ok = true;
              last_temp_humi_average[0] = 0;
              last_temp_humi_average[1] = 0;
        }
        else
        {
            /*do nothing*/
        }
        rec_string = "";
    }
    else if(-1 == rec_string.indexOf("AT"))
    {
        Serial.flush();
    }
    else
    {
        /*do nothing*/
    }
}
