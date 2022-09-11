#include "ULP.h"

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");

/****************\
|*     ULP      *|
\****************/
void ULP::setupULP()
{
  //load program into ULP
  esp_err_t err_load = ulp_load_binary(0, ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t));
  ESP_ERROR_CHECK(err_load);

  ulp_debounce_max_cnt = 5;
  ulp_wind_low_tick_cnt = 0;

  // suppress boot messages
  esp_deep_sleep_disable_rom_logging(); 
  
  /* Set ULP wake up period to T = 4ms.
   * Minimum pulse width has to be T * (ulp_debounce_cntr + 1) = 24ms. */
  ulp_set_wakeup_period(0, ULPSLEEP);

  /* Start the program */
  esp_err_t err_run = ulp_run(&ulp_entry - RTC_SLOW_MEM);
  ESP_ERROR_CHECK(err_run);  
}

void ULP::setupWindPin()
{
  setupPin(PIN_WIND,true,true);  //input and pulled up
}

void ULP::setupRainPin()
{
  setupPin(PIN_RAIN,true,false);  //input and pulled down
}

void ULP::setupAirPin()
{
  setupPin(PIN_AIR,false,false);  //output and pulled down  
  setAirPinHigh(false);
}

void ULP::setupBoostPin()
{
  setupPin(PIN_BOOST,false,true);  //output and pulled up  
  setBoostPinHigh(true);
}

void ULP::setAirPinHigh(bool setPinHighFlag)
{
  rtc_gpio_hold_dis(PIN_AIR);

  if(setPinHighFlag)
    rtc_gpio_set_level(PIN_AIR,HIGH);
  else
    rtc_gpio_set_level(PIN_AIR,LOW);

  rtc_gpio_hold_en(PIN_AIR);    
}

void ULP::setBoostPinHigh(bool setPinHighFlag)
{
  rtc_gpio_hold_dis(PIN_BOOST);

  if(setPinHighFlag)
    rtc_gpio_set_level(PIN_BOOST,HIGH);
  else
    rtc_gpio_set_level(PIN_BOOST,LOW);

  rtc_gpio_hold_en(PIN_BOOST);    
}

//if not input, the pin is output.  If not pullUP, then it's pulldown (no floating)
void ULP::setupPin(gpio_num_t gpio_num, bool inputFlag, bool pullUpFlag)   
{
  //Init and setup to input our output
  rtc_gpio_init(gpio_num);
  if(inputFlag)
    rtc_gpio_set_direction(gpio_num, RTC_GPIO_MODE_INPUT_ONLY);
  else
    rtc_gpio_set_direction(gpio_num, RTC_GPIO_MODE_OUTPUT_ONLY);

  //set pull up or pull down
  if(pullUpFlag)
  {
    rtc_gpio_pulldown_dis(gpio_num);
    rtc_gpio_pullup_en(gpio_num);
  }
  else
  {
    rtc_gpio_pulldown_en(gpio_num);
    rtc_gpio_pullup_dis(gpio_num);   
  }

  //hold
  rtc_gpio_hold_en(gpio_num); 
}

uint32_t ULP::getULPRainPulseCount() 
{
  /* ULP program counts signal edges, convert that to the number of pulses */
  uint32_t pulse_cnt_from_ulp = (ulp_rain_edge_cnt & UINT16_MAX) / 2;
  
  /* In case of an odd number of edges, keep one until next time */
  ulp_rain_edge_cnt = ulp_rain_edge_cnt % 2;
  
  return pulse_cnt_from_ulp;
}

uint32_t ULP::getULPWindPulseCount() 
{
  /* ULP program counts signal edges, convert that to the number of pulses */
  uint32_t pulse_cnt_from_ulp = (ulp_wind_edge_cnt & UINT16_MAX) / 2;
  
  /* In case of an odd number of edges, keep one until next time */
  ulp_wind_edge_cnt = ulp_wind_edge_cnt % 2;
  
  return pulse_cnt_from_ulp;
}

uint32_t ULP::getULPShortestWindPulseTime() 
{
  /* ULP program saves shortes pulse */
  uint32_t pulse_time_min = ((ulp_wind_low_tick_cnt & UINT16_MAX) * ULPSLEEP) * GUSTFACTOR;
  
  /* Reset shortest edge */
  ulp_wind_low_tick_cnt = 0;
  
  return pulse_time_min;
}
