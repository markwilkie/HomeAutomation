/*
Pages for the windvane and anemometer
 * 
http://www.qsl.net/zl1vfo/wx200/wx_index.htm
http://www.qsl.net/zl1vfo/wx200/wind_sensor.htm
http://www.qsl.net/zl1vfo/wx200/wm918_main_b.htm (schematic)
*/

void getWindSpeed(float *mph,float *mphGust)
{
  float rps=0,rpsGust=0;
  readAnemometer(ANEMOMETER_POLL_TIME,&rps,&rpsGust);

  //Calc wind speed per http://www.qsl.net/zl1vfo/wx200/wind_sensor.htm
  (*mph)=lookupMPH(rps);
  (*mphGust)=lookupMPH(rpsGust);
}

//lookup table
float lookupMPH(float rps)
{
  if(rps<0.2)
    return 0.0;
    
  // in[] holds revolutions per second  (29 values)
  float in[] = {0.2,0.4,0.8,1,1.2,1.4,1.6,1.8,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,58};

  // out[] holds the values wanted in mph
  float out[] = {0.6,1.7,3.6,4.0,4.5,4.9,5.3,5.8,6.3,10.5,14.8,19.0,23.2,27.5,31.8,36.0,40.3,44.5,48.8,53.0,57.3,61.5,65.7,70.0,74.3,78.5,82.8,87.0,124.9};

  float mph=FmultiMap(rps, in, out, 29);
  VERBOSE_PRINT("Wind MPH: ");
  VERBOSE_PRINT(mph);
  VERBOSE_PRINT("  RPS: ");
  VERBOSE_PRINTLN(rps);  
  
  return mph;
}

void readAnemometer(int _pollTime,float *rps,float *rpsGust)
{
    anemometerCount=0;
    int lastAnemometerCount=0;
    boolean attachedFlag=false;
    pinMode(INTERRUPTPIN2, INPUT_PULLUP);  //pullup is required as LOW is the trigger

    VERBOSE_PRINTLN("Delay to record anemometer");
    for(int i=0;i<(_pollTime/1000);i++) //one for every second
    {
      if(!digitalRead(INTERRUPTPIN2) && attachedFlag)
      {
        detachInterrupt(digitalPinToInterrupt(INTERRUPTPIN2));
        attachedFlag=false;
      }
      if(digitalRead(INTERRUPTPIN2) && !attachedFlag)      
      {
        attachInterrupt(digitalPinToInterrupt(INTERRUPTPIN2), anemometerInterruptHandler, LOW);      
        attachedFlag=true;
      }

      //Delay for 1 second
      for(int s=0;s<1000;s++)
        delayMicroseconds(1000);

      //Record gusts
      int gustCount=(anemometerCount-lastAnemometerCount);
      if(gustCount>(*rpsGust))
        (*rpsGust)=gustCount;
      lastAnemometerCount=anemometerCount;
    }

    if(attachedFlag)
      detachInterrupt(digitalPinToInterrupt(INTERRUPTPIN2));

     VERBOSE_PRINT("Interrupt count: ");
     VERBOSE_PRINTLN(anemometerCount);

     float seconds=_pollTime/1000;
     (*rps)=((float)anemometerCount/seconds);
}


void anemometerInterruptHandler(void)
{
 static unsigned long last_interrupt_time = 0;
 unsigned long interrupt_time = millis();
 if (interrupt_time - last_interrupt_time > 25) 
 {
  anemometerCount++;
 }
 last_interrupt_time = interrupt_time;  
}

// note: the in array should have increasing values
float FmultiMap(float val, float * _in, float * _out, uint8_t size)
{
  // take care the value is within range
  // val = constrain(val, _in[0], _in[size-1]);
  if (val <= _in[0]) return _out[0];
  if (val >= _in[size-1]) return _out[size-1];

  // search right interval
  uint8_t pos = 1;  // _in[0] allready tested
  while(val > _in[pos]) pos++;

  // this will handle all exact "points" in the _in array
  if (val == _in[pos]) return _out[pos];

  // interpolate in the right segment for the rest
  return (val - _in[pos-1]) * (_out[pos] - _out[pos-1]) / (_in[pos] - _in[pos-1]) + _out[pos-1];
}

