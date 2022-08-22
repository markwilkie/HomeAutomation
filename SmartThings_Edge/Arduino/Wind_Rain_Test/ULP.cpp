#include "ULP.h"

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");

/****************\
|*     ULP      *|
\****************/
void ULP::setupULP()
{
  esp_err_t err_load = ulp_load_binary(0, ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t));
  ESP_ERROR_CHECK(err_load);

  gpio_num_t wind_gpio_num = PIN_WIND;
  assert(rtc_gpio_desc[wind_gpio_num].reg && "GPIO used for wind pulse counting must be an RTC IO");

  gpio_num_t rain_gpio_num = PIN_RAIN;
  assert(rtc_gpio_desc[rain_gpio_num].reg && "GPIO used for rain pulse counting must be an RTC IO");

  gpio_num_t air_gpio_num = PIN_AIR;  
  assert(rtc_gpio_desc[rain_gpio_num].reg && "GPIO used for disable/enable air quality sensor must be an RTC IO");  
    
  /* Initialize some variables used by ULP program.
   * Each 'ulp_xyz' variable corresponds to 'xyz' variable in the ULP program.
   * These variables are declared in an auto generated header file,
   * 'ulp_main.h', name of this file is defined in component.mk as ULP_APP_NAME.
   * These variables are located in RTC_SLOW_MEM and can be accessed both by the
   * ULP and the main CPUs.
   *
   * Note that the ULP reads only the lower 16 bits of these variables. */
  //ulp_wind_io = rtc_gpio_desc[wind_gpio_num].rtc_num; /* map from GPIO# to RTC_IO# */
  //ulp_rain_io = rtc_gpio_desc[rain_gpio_num].rtc_num; /* map from GPIO# to RTC_IO# */
  ulp_debounce_max_cnt = 5;
     
  //ulp_wind_debounce_cntr = 5;
  //ulp_wind_next_edge = 1;
  //ulp_wind_tick_cnt = 0;
  ulp_wind_low_tick_cnt = 0;

  //ulp_rain_debounce_cntr = 5;
  //ulp_rain_next_edge = 1;

    /* Initialize selected GPIO as RTC IO, enable input, sets pullup and pulldown */
  /* GPIO used for wind pulse counting. */
  rtc_gpio_init(wind_gpio_num);
  rtc_gpio_set_direction(wind_gpio_num, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_dis(wind_gpio_num);
  rtc_gpio_pullup_en(wind_gpio_num);
  rtc_gpio_hold_en(wind_gpio_num);

  /* GPIO used for wind pulse counting. */
  rtc_gpio_init(rain_gpio_num);
  rtc_gpio_set_direction(rain_gpio_num, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_en(rain_gpio_num);
  rtc_gpio_pullup_dis(rain_gpio_num);
  rtc_gpio_hold_en(rain_gpio_num);

  /* turn off air quality sensor*/
  rtc_gpio_init(air_gpio_num);
  rtc_gpio_set_direction(air_gpio_num, RTC_GPIO_MODE_OUTPUT_ONLY);
  rtc_gpio_pulldown_en(air_gpio_num);
  rtc_gpio_pullup_dis(air_gpio_num);
  rtc_gpio_hold_en(air_gpio_num);  
  
  /* Set ULP wake up period to T = 4ms.
   * Minimum pulse width has to be T * (ulp_debounce_cntr + 1) = 24ms. */
  ulp_set_wakeup_period(0, ULPSLEEP);

  /* Start the program */
  esp_err_t err_run = ulp_run(&ulp_entry - RTC_SLOW_MEM);
  ESP_ERROR_CHECK(err_run);  
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
  
  /* In case of an odd number of edges, keep one until next time (this is where the pulse counter is cleared*/
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
