#include <PID_v1.h>

void setupFanPID()
{
    PIDSetpoint = SETPOINT;  //desired temp
  
    //PID Setup
    myPID.SetMode(AUTOMATIC);
}

void doFanPID()
{
  //Read temp and feed it to the PID
  PIDInput=(double)readTemp();
  int pidValue=computePID();
  
  //If less than cutoff, just turn off fan
  if(pidValue < FAN_CUTOFF)
    pidValue=0;

  //Write pwm value to pin  
  analogWrite(FANPIN, pidValue); 
}

int computePID()
{
    //Compute PID value
  double gap = abs(PIDSetpoint-PIDInput); //distance away from setpoint
  if(gap<1)
  {  
    //Close to Setpoint, be conservative
    myPID.SetTunings(consKp, consKi, consKd);
  }
  else
  {
     //Far from Setpoint, be aggresive
     myPID.SetTunings(aggKp, aggKi, aggKd);
  } 
  myPID.Compute();
  VERBOSE_PRINT(PIDInput);
  VERBOSE_PRINT(" ");
  VERBOSE_PRINTLN(PIDOutput);
  
  //Write PID output to fan if not critical
  if (PIDInput<CRITICAL)
    return PIDOutput;
  else
    return 255;
}
