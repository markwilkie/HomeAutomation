
#include "driver/pcnt.h"                                 /* Include the required header file, Its working Arduion IDE 2.0*/

/* For detailed reference please follow the documentations in following Link:- 
  Link:- https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/pcnt.html
*/

//Setup pulse counter 0
#define PCNT_UNIT_Used      PCNT_UNIT_0                  /* Select the Pulse Count 0  as the unit..*/
#define PCNT_H_LIM_VAL      10000                        /* Set the max limit to trigger the interrupt*/
#define PCNT_INPUT_SIG_IO   4                            /* Pulse Input selected as GPIO 4 */
#define PCNT_FILTER         1 / portTICK_PERIOD_MS      /* 1 ms delay to filter out noise */

// Timer setup
#define SAMPLE_SIZE_MS      3000                         /* Milliseconds for timer which equates to sample size - e.g. how often the pulses are summed.  Determines gusts */
volatile bool timerAlarm=false;
hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

//Annenometer sample setup
#define TOT_SAMPLES          10                        /* Total number of samples.  This is the time period used to determine max gust */
static byte totalSamples[TOT_SAMPLES];  //using bytes to save space as the count won't be more than 255 in one second
int currSamplePos = 0; // index for where in the sample array we are
long totalPulseCount = 0;
long maxGustPulseCount = 0;
unsigned long gustTime=0; //stores the time that the maxGust was recorded at 

//------------------------------------------------------- 
//Timer interrupt routine
// 
// Be very careful to make this FAST
//
void IRAM_ATTR onTimer() 
{
  //Read how many pulses happened since the last timer went off, then clear count
  int16_t currentPulseCount = 0;
  pcnt_get_counter_value(PCNT_UNIT_Used, &currentPulseCount);
  pcnt_counter_clear(PCNT_UNIT_Used);

  //Record max gust
  long currentTime = millis();
  if(currentPulseCount>maxGustPulseCount || ((currentTime - gustTime) >  (SAMPLE_SIZE_MS * TOT_SAMPLES)))
  {
    maxGustPulseCount = currentPulseCount;
    gustTime = currentTime;
  }

  //Calc moving average, and add count to sample array
  totalPulseCount=totalPulseCount-totalSamples[currSamplePos]+currentPulseCount;
  totalSamples[currSamplePos]=(byte)currentPulseCount;
  currSamplePos++;
  if(currSamplePos>=TOT_SAMPLES)
    currSamplePos=0;

  if((totalPulseCount/TOT_SAMPLES)>maxGustPulseCount || ((currentTime - gustTime) >  (SAMPLE_SIZE_MS * TOT_SAMPLES)))
  {
    maxGustPulseCount = currentPulseCount;
    gustTime = currentTime;
  }  

  //Set flag
  portENTER_CRITICAL_ISR(&timerMux);
  timerAlarm=true;
  portEXIT_CRITICAL_ISR(&timerMux);
 
}

//------------------------------------------------------------
void Init_PulseCounter (void)
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
  ESP_ERROR_CHECK(pcnt_unit_config(&pcntFreqConfig));        // Configure PCNT.

  ESP_ERROR_CHECK(pcnt_set_filter_value(PCNT_UNIT_Used, PCNT_FILTER));        // Set filter value
  ESP_ERROR_CHECK(pcnt_filter_enable(PCNT_UNIT_Used));
  
  ESP_ERROR_CHECK(pcnt_counter_pause(PCNT_UNIT_Used));                        // Pause PCNT counter such that we can set event.
  ESP_ERROR_CHECK(pcnt_counter_clear(PCNT_UNIT_Used));                        // Clear PCNT counter to avoid ant mis counting.
  ESP_ERROR_CHECK(pcnt_intr_enable(PCNT_UNIT_Used));                          // Enable PCNT
  ESP_ERROR_CHECK(pcnt_counter_resume(PCNT_UNIT_Used));                       // Re-started PCNT.

  Serial.println("PCNT Init Completed....");
}

/********************************************************************/
// Setup
//
void setup() {
   Serial.begin(115200);
   delay(500);

   //setup timer
   timer = timerBegin(0, 80, true);
   timerAttachInterrupt(timer, &onTimer, true);
   timerAlarmWrite(timer, (SAMPLE_SIZE_MS * 1000L), true);
   timerAlarmEnable(timer);

   //free heap
   Serial.print("Free Heap: ");
   Serial.println(ESP.getFreeHeap());

  
   //Setup pulse counters
   Init_PulseCounter();
}

/********************************************************************/
// Loop
//
void loop() 
{

  // Has timer "dinged" while we were away?
  if (timerAlarm) 
  {
    portENTER_CRITICAL(&timerMux);
    timerAlarm=false;
    portEXIT_CRITICAL(&timerMux);

    long avgPulseCount = totalPulseCount/TOT_SAMPLES;
    int ms_sec = (avgPulseCount * 8.75)/(SAMPLE_SIZE_MS/1000);
    int gust_ms_sec = (maxGustPulseCount * 8.75)/(SAMPLE_SIZE_MS/1000);
    int knots = ms_sec * .0194384449;
    int gust = gust_ms_sec * .0194384449;

    Serial.print("Time:  ");
    Serial.println(millis());

    Serial.print("Pulse avg for sample time= ");
    Serial.println(avgPulseCount);

    Serial.print("Knots = ");
    Serial.println(knots);

    Serial.print("Gust = ");
    Serial.println(gust);
    
    Serial.println("==========================");
  }
  

delay(500);  /* Delay is given to give result properly..*/
}
