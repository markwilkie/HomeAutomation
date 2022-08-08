/*
    Put your ULP globals here you want visibility
    for your sketch. Add "ulp_" to the beginning
    of the variable name and must be size 'uint32_t'
*/
#include "Arduino.h"

extern uint32_t ulp_entry;

//extern uint32_t ulp_wind_io;
//extern uint32_t ulp_rain_io;
extern uint32_t ulp_current_io;
extern uint32_t ulp_debounce_max_cnt;

extern uint32_t ulp_rain_next_edge;
extern uint32_t ulp_rain_debounce_cntr;
extern uint32_t ulp_rain_edge_cnt;

extern uint32_t ulp_wind_next_edge;
extern uint32_t ulp_wind_debounce_cntr;
extern uint32_t ulp_wind_edge_cnt;
extern uint32_t ulp_wind_tick_cnt;
extern uint32_t ulp_wind_low_tick_cnt;
