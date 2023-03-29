#include "CurrentData.h"



CurrentData::CurrentData()
{
    //Other sensors
    pitot.init(200);  //update every 400 ms
    ignState.init(1000);
    barometer.init(5000);
    rtc.init(1000);
    ldr.init(1000);
}

void CurrentData::init()
{
    //calibrate and setup sensors
    barometer.setup();
    rtc.setup();
}

void CurrentData::calibratePitot()
{
    pitot.calibrate();
}

int CurrentData::readPitot()
{
  return pitot.readSpeed();
}

bool CurrentData::verifyInterfaces(int service, int pid, int value,char *buffer)
{
  //Let's assume we're done and then check for exceptions
  bool allOnline=true;
  int offlineCount=0;

  //Poll sensors
  updateDataFromSensors();
  
  //check sensors
  if(!barometer.isOnline())
  {
    strcpy(buffer+strlen(buffer),"Barometer\n");
    offlineCount++;
    allOnline=false;
  }
  if(!rtc.isOnline())
  {
    strcpy(buffer+strlen(buffer),"RTC\n");
    offlineCount++;
    allOnline=false;
  }
  if(!pitot.isOnline())
  {
    strcpy(buffer+strlen(buffer),"Pitot\n");
    offlineCount++;
    allOnline=false;
  }
  
  //Loop through pids 
  const int arrLen = sizeof(pidArray) / sizeof(pidArray[0]);
  for(int i=0;i<arrLen;i++)
  {

    if(service==pidArray[i]->service && pid==pidArray[i]->pid)
    {
        pidArray[i]->online=true;
        
        //Be sure and save off the value
        updateDataFromPIDs(service,pid,value);
    }
    else
    {
      if(!pidArray[i]->online)
      {
        allOnline=false;
        offlineCount++;
        if(offlineCount>5)
          return allOnline;

        strcpy(buffer+strlen(buffer),pidArray[i]->label);
      }
    }  
  }

  return allOnline;
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

    //Light level
    currentLightLevel=ldr.readLightLevel();
}

// update value if appropriate
void CurrentData::updateDataFromPIDs(int service,int pid,int value)
{   
    //calc engine load
    if(service==load.service && pid==load.pid)
    {
        currentLoad=value;
        return;
    }
    //distance travelled in km, converting to miles
    if(service==distance.service && pid==distance.pid)
    {
        currentMiles=value*0.621371;
        return;
    }
    //fuel available in percentage  (30 --> 30%)
    if(service==fuel.service && pid==fuel.pid)
    {
        currentFuelPerc=value;
        return;
    }
    //MAF in g/s
    if(service==maf.service && pid==maf.pid)
    {
        currentMAF=value;
        return;
    }      
    //Current speed in km/h
    if(service==speed.service && pid==speed.pid)
    {
        currentSpeed=value*0.621371;
        currentPitotSpeed=pitot.readSpeed()-currentSpeed;
        return;
    } 
    //Coolant temp
    if(service==coolant.service && pid==coolant.pid)
    {
      currentCoolantTemp=((float)value*(9.0/5.0)+32.0);
    }
    //Transmision temp
    if(service==trans.service && pid==trans.pid)
    {
      currentTransmissionTemp=((float)value*(9.0/5.0)+32.0);      
    }    
    //Manifold pressure in kPa
    if(service==manPres.service && pid==manPres.pid)
    {        
      double bara=barometer.getPressure();
      float boost=value-bara;
      currentBoost=boost*.145;
    } 
    //Diag - e.g. how many codes are stored
    if(service==diag.service && pid==diag.pid)
    {
      if(value>0)
        codesPresent=true;
      else
        codesPresent=false;
    }
}
