/* ULP Example: pulse counting

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.

   This file contains assembly code which runs on the ULP.

   ULP wakes up to run this code at a certain period, determined by the values
   in SENS_ULP_CP_SLEEP_CYCx_REG registers. On each wake up, the program checks
   the input on GPIO0. If the value is different from the previous one, the
   program "debounces" the input: on the next debounce_max_cnt wake ups,
   it expects to see the same value of input.
   If this condition holds true, the program increments edge_cnt and starts
   waiting for input signal polarity to change again.
   When the edge counter reaches certain value (set by the main program),
   this program running triggers a wake up from deep sleep.
*/

/* ULP assembly files are passed through C preprocessor first, so include directives
   and C macros may be used in these files 
 */
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"

  /* Define variables, which go into .bss section (zero-initialized data) */
  .bss

  /* Next input signal edge expected: 0 (negative) or 1 (positive) */
  .global rain_next_edge
rain_next_edge:
  .long 0

  /* Counter started when signal value changes.
     Edge is "debounced" when the counter reaches zero. */
  .global rain_debounce_cntr
rain_debounce_cntr:
  .long 0

  /* Total number of signal edges acquired */
  .global rain_edge_cnt
rain_edge_cnt:
  .long 0 

  ///
  ///
  //

  /* Next input signal edge expected: 0 (negative) or 1 (positive) */
  .global wind_next_edge
wind_next_edge:
  .long 0

  /* Counter started when signal value changes.
     Edge is "debounced" when the counter reaches zero. */
  .global wind_debounce_cntr
wind_debounce_cntr:
  .long 0

  /* Total number of signal edges acquired */
  .global wind_edge_cnt
wind_edge_cnt:
  .long 0  

  /* Tick count between edges */
  .global wind_tick_cnt
wind_tick_cnt:
  .long 0  

  /* Tick count between edges */
  .global wind_low_tick_cnt
wind_low_tick_cnt:
  .long 0  

  ///
  //
  //

  /* Value to which debounce_cntr gets reset.
     Set by the main program. */
  .global debounce_max_cnt
debounce_max_cnt:
  .long 0
    
  /* RTC IO number used to sample the wind input signal.
     Set by main program. */
//  .global wind_io
//wind_io:
//  .long 0

  /* RTC IO number used to sample the rain input signal.
     Set by main program. */
//  .global rain_io
//rain_io:
//  .long 0

  /* current pin we're using.   */
io_toggle:
  .long 0

//
//
//

  /* Code goes into .text section */
  .text
  .global entry
entry:
  /* if in the middle of a wind pulse, don't check rain to make sure we don't miss */
    //problem is that it often stops in the middle of a pulse....
  //move r3, wind_next_edge
  //ld r0, r3, 0
  //jumpr read_wind_now, 1, eq
  
  /* check whatever pin we didn't last time */
  move r3, io_toggle
  ld r2, r3, 0
  add r2, r2, 1
  and r2, r2, 1
  st r2, r3, 0
  jump read_wind_now, eq
  jump read_rain_now
  
read_wind_now:
  /* Increment tick count so we can measure gust by short pulses*/
  move r3, wind_tick_cnt
  ld r2, r3, 0
  add r2, r2, 1
  st r2, r3, 0
 
  /* Set pin */
  move r3, 16

  /* Lower 16 IOs and higher need to be handled separately,
   * because r0-r3 registers are 16 bit wide.
   * Check which IO this is.
   */
  move r0, r3
  jumpr read_io_high, 16, ge

  /* Read the value of lower 16 RTC IOs into R0 */
  READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S, 16)
  rsh r0, r0, r3
  jump read_wind_done

read_rain_now:
  /* Load io */
  move r3, 7

  /* Lower 16 IOs and higher need to be handled separately,
   * because r0-r3 registers are 16 bit wide.
   * Check which IO this is.
   */
  move r0, r3
  jumpr read_io_high, 16, ge

  /* Read the value of lower 16 RTC IOs into R0 */
  READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S, 16)
  rsh r0, r0, r3
  jump read_rain_done

  /* Read the value of RTC IOs 16-17, into R0 */
read_io_high:
  READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S + 16, 2)
  sub r3, r3, 16
  rsh r0, r0, r3

read_wind_done:
  and r0, r0, 1
  /* State of input changed? */
  move r3, wind_next_edge
  ld r3, r3, 0
  add r3, r0, r3
  and r3, r3, 1
  jump wind_changed, eq
  /* Not changed */
  /* Reset debounce_cntr to debounce_max_cnt */
  move r3, debounce_max_cnt
  move r2, wind_debounce_cntr
  ld r3, r3, 0
  st r3, r2, 0
  /* End program */
  halt

read_rain_done:
  and r0, r0, 1
  /* State of input changed? */
  move r3, rain_next_edge
  ld r3, r3, 0
  add r3, r0, r3
  and r3, r3, 1
  jump rain_changed, eq
  /* Not changed */
  /* Reset debounce_cntr to debounce_max_cnt */
  move r3, debounce_max_cnt
  move r2, rain_debounce_cntr
  ld r3, r3, 0
  st r3, r2, 0
  /* End program */
  halt

wind_changed:
  /* Input state changed */
  /* Has debounce_cntr reached zero? */
  move r3, wind_debounce_cntr
  ld r2, r3, 0
  add r2, r2, 0 /* dummy ADD to use "jump if ALU result is zero" */
  jump wind_edge_detected, eq
  /* Not yet. Decrement debounce_cntr */
  sub r2, r2, 1
  st r2, r3, 0
  /* End program */
  halt

rain_changed:
  /* Input state changed */
  /* Has debounce_cntr reached zero? */
  move r3, rain_debounce_cntr
  ld r2, r3, 0
  add r2, r2, 0 /* dummy ADD to use "jump if ALU result is zero" */
  jump rain_edge_detected, eq
  /* Not yet. Decrement debounce_cntr */
  sub r2, r2, 1
  st r2, r3, 0
  /* End program */
  halt

wind_edge_detected:
  /* Reset debounce_cntr to debounce_max_cnt */
  move r3, debounce_max_cnt
  move r2, wind_debounce_cntr
  ld r3, r3, 0
  st r3, r2, 0
  /* Flip next_edge */
  move r3, wind_next_edge
  ld r2, r3, 0
  add r2, r2, 1
  and r2, r2, 1
  st r2, r3, 0
  /* Increment edge_cnt */
  move r3, wind_edge_cnt
  ld r2, r3, 0
  add r2, r2, 1
  st r2, r3, 0
  /* If trailing edge of the pulse, see if low tick count needs setting */
  jumpr trailing_detected, 1, eq  //r0 is the state of the pin, 1 is trailing edge
  //end
  halt

rain_edge_detected:
  /* Reset debounce_cntr to debounce_max_cnt */
  move r3, debounce_max_cnt
  move r2, rain_debounce_cntr
  ld r3, r3, 0
  st r3, r2, 0
  /* Flip next_edge */
  move r3, rain_next_edge
  ld r2, r3, 0
  add r2, r2, 1
  and r2, r2, 1
  st r2, r3, 0
  /* Increment edge_cnt */
  move r3, rain_edge_cnt
  ld r2, r3, 0
  add r2, r2, 1
  st r2, r3, 0
  /* Compare edge_count to wake up value to save overflows */
  move r3, 10000
  sub r3, r3, r2
  jump wake_up, eq
  halt

wake_up:
  wake
  halt  

trailing_detected:
  // Jump to pulse_lower if we find lowest value
  move r3, wind_low_tick_cnt
  move r2, wind_tick_cnt
  ld r3, r3, 0
  ld r2, r2, 0
  sub r1, r2, r3
  jump pulse_lower, ov
  // Jump to pulse_lower when pulse_min is zero 
  add r3, r3, 0
  jump pulse_lower, eq
  jump pulse_reset

pulse_lower:
  // Set pulse_min to pulse_cur 
  move r3, wind_low_tick_cnt
  st r2, r3, 0  //r2 was set w/ current count from before
  jump pulse_reset

pulse_reset:
  // Reset pulse_cur to zero 
  move r3, 0
  move r2, wind_tick_cnt
  st r3, r2, 0
  halt
