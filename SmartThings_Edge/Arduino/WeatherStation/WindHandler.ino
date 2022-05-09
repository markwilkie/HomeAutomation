#include "WindHandler.h"

void initWindPulseCounter(void)
{
  pinMode(PCNT_INPUT_SIG_IO, INPUT_PULLUP);  //pullup is required as LOW indicates one revolution
  
  pcnt_config_t pcntFreqConfig = { };                        // Declear variable for cinfig
  pcntFreqConfig.pulse_gpio_num = PCNT_INPUT_SIG_IO;         // Set the port ping using for counting
  pcntFreqConfig.pos_mode = PCNT_COUNT_DIS;
  pcntFreqConfig.neg_mode = PCNT_COUNT_INC;
  pcntFreqConfig.lctrl_mode = PCNT_MODE_KEEP;
  pcntFreqConfig.hctrl_mode = PCNT_MODE_KEEP;
  pcntFreqConfig.counter_h_lim = PCNT_H_LIM_VAL;             // Set Over flow Interupt / event value
  pcntFreqConfig.unit = PCNT_UNIT_Used;                      //  Set Pulsos unit to ne used
  pcntFreqConfig.channel = PCNT_CHANNEL_0;                   //  select PCNT channel 0

  ESP_ERROR_CHECK(pcnt_unit_config(&pcntFreqConfig));                         // Configure PCNT.

  ESP_ERROR_CHECK(pcnt_set_filter_value(PCNT_UNIT_Used, PCNT_FILTER));        // Set filter value
  ESP_ERROR_CHECK(pcnt_filter_enable(PCNT_UNIT_Used));
  
  ESP_ERROR_CHECK(pcnt_counter_pause(PCNT_UNIT_Used));                        // Pause PCNT counter such that we can set event.
  ESP_ERROR_CHECK(pcnt_counter_clear(PCNT_UNIT_Used));                        // Clear PCNT counter to avoid ant mis counting.
  ESP_ERROR_CHECK(pcnt_intr_enable(PCNT_UNIT_Used));                          // Enable PCNT
  ESP_ERROR_CHECK(pcnt_counter_resume(PCNT_UNIT_Used));                       // Re-started PCNT.

  VERBOSEPRINTLN("PCNT Init Completed");
}

//Anemometer pulse counter
void storeWindSample()
{
  //Read how many pulses happened since the last timer went off, then clear count
  int16_t currentPulseCount = 0;
  pcnt_get_counter_value(PCNT_UNIT_Used, &currentPulseCount);
  pcnt_counter_clear(PCNT_UNIT_Used);

  //Set max gust if current sample is bigger than current max gust
  long currentTime=epoch+secondsSinceEpoch;
  if(currentPulseCount>gustMax)
  {
    gustTime=currentTime;
    gustMax=currentPulseCount;
  }
  //find new max gust if the gust has expired
  if((currentTime-gustTime) > (WIND_PERIOD_MIN*60L))
  {
    gustTime=currentTime;
    gustMax=getCurrentMaxGust();
  }  

  //Calc running sum, and add count to sample array
  windSampleTotal=windSampleTotal-windSamples[windSampleIdx]+currentPulseCount;
  windSamples[windSampleIdx]=(byte)currentPulseCount;

  //Increment index
  windSampleIdx++;

  //If wraps, then reset AND store off high gust
  if(windSampleIdx>=WIND_PERIOD_SIZE)
  {
    //reset pos
    windSampleIdx=0;  

    //Add current max gust 
    gustLast12[gustLast12Idx]=gustMax;
    gustLast12Time[gustLast12Idx]=currentTime;

    //increment last12 pos
    gustLast12Idx++;
    if(gustLast12Idx>=GUST_LAST12_SIZE)
    {
      gustLast12Idx=0;
    }
  }
}

//return max gust sample
int getCurrentMaxGust()
{
  int maxGust=0;
  for(int idx=0;idx<WIND_PERIOD_SIZE;idx++)
  {
    if(windSamples[idx]>maxGust)
    {
      maxGust=windSamples[idx];
    }
  }

  return maxGust;
}

//Get index for max gust last 12 hours
int getLast12MaxGustIndex()
{
  int maxGustIdx=0;
  int maxGust=0;
  for(int idx=0;idx<GUST_LAST12_SIZE;idx++)
  {
    if(gustLast12[idx]>maxGust)
    {
      maxGust=gustLast12[idx];
      maxGustIdx=idx;
    }
  }

  return maxGustIdx;
}

//Convert pulses to kts
float convertToKTS(int pulses)
{
  //Convert furst to Meters/Sec
  float msSec = (pulses/WIND_SAMPLE_SEC)*.0875;  //constant is the factor for the enemometer I have  https://www.makerfabs.com/wiki/index.php?title=Anemometer
  
  return msSec * 1.94384449;                     //conversion factor from meters per second to knots
}
