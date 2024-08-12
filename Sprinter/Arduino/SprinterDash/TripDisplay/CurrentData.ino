#include "CurrentData.h"
#include "VanWifi.h"


CurrentData::CurrentData()
{
    //Other sensors
    pitot.init(200); 
    ignState.init(1000);
    barometer.init(10000);  //It takes about 400ms to get a reading, so this is expensive
    rtc.init(1000);
    ldr.init(1000);
}

void CurrentData::init()
{
    //calibrate and setup sensors
    barometer.setup();
    rtc.setup();

    //For filtering out outlier values  (24 value window, 10 threshold, and 3x outlier override)
    distanceHampelFilter.init(24, 10, 3);
    fuelHampelFilter.init(24, 10, 3);
}

void CurrentData::setTime(unsigned long secondsToSet)
{
    currentSeconds=rtc.adjust(secondsToSet);
}

double_t CurrentData::calibratePitot()
{
  return pitot.calibrate(currentSpeed);
}

void CurrentData::setPitotCalibrationFactor(double_t factor)
{
    pitot.setCalibrationFactor(factor);
}

int CurrentData::readPitot()
{
  return pitot.readSpeed();
}

void CurrentData::updateStatusForm(char *buffer)
{
  int offlineCount=0;

  //sensors
  if(!barometer.isOnline())
  {
    offlineCount++;
    strcpy(buffer+strlen(buffer),"Barometer\n");
  }
  if(!rtc.isOnline())
  {
    offlineCount++;
    strcpy(buffer+strlen(buffer),"RTC\n");
  }
  if(!pitot.isOnline())
  {
    offlineCount++;
    strcpy(buffer+strlen(buffer),"Pitot\n");
  }

  //Loop through pids and list those which are still not online
  const int arrLen = sizeof(pidArray) / sizeof(pidArray[0]);
  for(int i=0;i<arrLen;i++)
  {
    if(!pidArray[i]->online)
    {
      //Let's be sure and not overflow
      if(offlineCount>5)
        return;

      offlineCount++;
      strcpy(buffer+strlen(buffer),pidArray[i]->label);
    }
  }
}

bool CurrentData::verifyInterfaces(int service, int pid, int value)
{
  //Let's assume we're done and then check for exceptions
  bool allOnline=true;

  //Poll sensors
  updateDataFromSensors();
  
  //check sensors
  if(!barometer.isOnline())
  {
    allOnline=false;
  }
  if(!rtc.isOnline())
  {
    allOnline=false;
  }
  if(!pitot.isOnline())
  {
    allOnline=false;
  }
  
  //Loop through pids 
  const int arrLen = sizeof(pidArray) / sizeof(pidArray[0]);
  for(int i=0;i<arrLen;i++)
  {

    //is this one we've been waiting for?
    if(service==pidArray[i]->service && pid==pidArray[i]->pid)
    {
        pidArray[i]->online=true;
        
        //Be sure and save off the value
        updateDataFromPIDs(service,pid,value);
      
        //Serial.print("service: ");Serial.print(service,HEX);Serial.print("/");Serial.print(pid,HEX);Serial.print(":");Serial.print(value);
        //Serial.print("   ---> Status: ");Serial.println(pidArray[i]->online);
    }

    //check allonline flag
    if(!pidArray[i]->online)
    {
      allOnline=false;
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
        bool isNotOutlierFlag = distanceHampelFilter.writeIfNotOutlier(value);
        if(isNotOutlierFlag)
        {
          currentMiles=value*0.621371;
          currentMilesOnline=true;
        }
        else
          logger.log(VERBOSE,"Miles Outlier: %d   Median: %d",value,distanceHampelFilter.readMedian());
        return;
    }
    //fuel available in percentage  (30 --> 30%) 
    if(service==fuel.service && pid==fuel.pid)
    {
        bool isNotOutlierFlag = fuelHampelFilter.writeIfNotOutlier(value);
        if(isNotOutlierFlag)
        {
          currentFuelPerc=value;
          currentFuelPercOnline=true;
        }
        else
          logger.log(VERBOSE,"Fuel Outlier: %d   Median: %d",value,fuelHampelFilter.readMedian());
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
        currentPitotSpeed=pitot.readSpeed();
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
      double bara=barometer.getPressure()/10.0;  //convert from pa to kpa
      float boost=value-bara;
      currentBoost=boost*.145;  //convert from kpa to psi
    } 
    //Diag - e.g. how many codes are stored
    if(service==diag.service && pid==diag.pid)
    {
      if(value>1)   //because of the DPF delete, there's always one stored code w/ no details.  (dpf pressure doesn't agree)
        codesPresent=true;
      else
        codesPresent=false;
    }
}

void CurrentData::dumpData()
{
  logger.log(INFO,"   Current Miles: %d",currentMiles);
  logger.log(INFO,"   Current Seconds: %lu",currentSeconds);
  logger.log(INFO,"   Current Fuel Perc: %d",currentFuelPerc);
  logger.log(INFO,"   Current Elevation: %d",currentElevation);
  logger.log(INFO,"   Current MAF: %d",currentMAF);
  logger.log(INFO,"   Current Speed: %d",currentSpeed);
  logger.log(INFO,"   Current Pitot Speed: %d",currentPitotSpeed);
  logger.log(INFO,"   Current Load: %d",currentLoad);
  logger.log(INFO,"   Current Coolant Temp: %d",currentCoolantTemp);
  logger.log(INFO,"   Current Transmission Temp: %d",currentTransmissionTemp);
  logger.log(INFO,"   Current Light Level: %d",currentLightLevel);
  logger.log(INFO,"   Codes Present Flag: %d",codesPresent);
  logger.log(INFO,"   Ignition State: %d",ignitionState);
}
