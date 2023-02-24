#include "CurrentData.h"

CurrentData::CurrentData()
{
    //Other sensors
    pitot.init(400);  //update every 400 ms
    ignState.init(1000);
    barometer.init(5000);
    rtc.init(1000);
}

void CurrentData::init()
{
    //calibrate and setup sensors
    pitot.calibrate();
    barometer.setup();
    rtc.setup();
}

void CurrentData::updateData(int service,int pid,int value)
{
    updateDataFromPIDs(service,pid,value);
    updateDataFromSensors();
}

void CurrentData::updateDataFromSensors()
{
    //Barometer
    barometer.update();  //this has timing built in....
    currentElevation=barometer.getElevation();

    //RTC
    currentSeconds=rtc.getSecondsSinc2000();

    //Ignition state
    ignitionState=ignState.getIgnState();
}

// update value if appropriate
void CurrentData::updateDataFromPIDs(int service,int pid,int value)
{   
    //calc engine load
    if(service==0x41 && pid==0x04)
    {
        currentLoad=value;
        return;
    }
    //distance travelled in km, converting to miles
    if(service==0x41 && pid==0x31)
    {
        currentMiles=value*0.621371;
        return;
    }
    //fuel available in percentage  (30 --> 30%)
    if(service==0x41 && pid==0x2F)
    {
        currentFuelPerc=value;
        return;
    }
    //MAF in g/s
    if(service==0x41 && pid==0x10)
    {
        currentMAF=value;
        return;
    }      
    //Current speed in km/h
    if(service==0x41 && pid==0x0D)
    {
        currentSpeed=value*0.621371;
        currentPitotSpeed=pitot.readSpeed()-currentSpeed;
        return;
    } 
    //update gauges after doing any needed calculations
    if(service==0x41 && pid==0x04)
    { 
      currentLoad=value;   
    }  
    //Coolant temp
    if(service==0x41 && pid==0x05)
    {
      currentCoolantTemp=((float)value*(9.0/5.0)+32.0);
    }
    //Transmision temp
    if(service==0x61 && pid==0x30)
    {
      currentTransmissionTemp=((float)value*(9.0/5.0)+32.0);      
    }    
    //Manifold pressure in kPa
    if(service==0x41 && pid==0x0B)
    {        
      double bara=barometer.getPressure();
      float boost=value-bara;
      currentBoost=boost*.145;
    } 
    //Diag - e.g. how many codes are stored
    if(service==0x41 && pid==0x1)
    {
      if(value>0)
        codesPresent=true;
      else
        codesPresent=false;
    }
    //Light level -- this one is "fake" and sent as a PID from a sensor on the board
    if(service==0x77 && pid==0x01)
    {
        currentLightLevel=value;
    }

}
