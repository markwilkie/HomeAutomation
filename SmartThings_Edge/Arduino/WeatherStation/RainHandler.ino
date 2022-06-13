#include "RainHandler.h"

void initRainPulseCounter(void)
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

//rain meter pulse counter
void storeRainSample()
{
  //Read how many pulses happened since the last timer went off, then clear count
  int16_t currentPulseCount = 0;
  pcnt_get_counter_value(PCNT_UNIT_Used, &currentPulseCount);
  pcnt_counter_clear(PCNT_UNIT_Used);

  //Calc running sum, and add count to sample array
  rainSampleTotal=rainSampleTotal-rainSamples[rainSampleIdx]+currentPulseCount;
  rainSamples[rainSampleIdx]=currentPulseCount;

  //Increment index
  rainSampleIdx++;

  //If wraps, then reset AND store off high gust
  if(rainSampleIdx>=RAIN_PERIOD_SIZE)
  {
    //reset pos
    rainSampleIdx=0;  
  }
}


