#include "TripData.h"
#include "PropBag.h"
#include "VanWifi.h"

TripData::TripData(CurrentData *_currentDataPtr,PropBag *_propBagPtr,int _tripIdx)
{
    currentDataPtr=_currentDataPtr;
    propBagPtr=_propBagPtr;
    tripIdx=_tripIdx;  //used to calc offset on EEPROM
}

void TripData::resetTripData()
{
    logger.log(INFO,"Reseting TripData Idx: %d",tripIdx);

    //If not online yet, set flag so we catch it later
    if(!currentDataPtr->currentMilesOnline)
    {
        startMilesNeedsUpdating=true;
        logger.log(WARNING,"Distance not online yet when reseting");
    }
    if(!currentDataPtr->currentFuelPercOnline)
    {
        startFuelNeedsUpdating=true;
        logger.log(WARNING,"Fuel not online yet when reseting");
    }

    data.startMiles=currentDataPtr->currentMiles;
    data.startSeconds=currentDataPtr->currentSeconds;
    data.startFuelPerc=currentDataPtr->currentFuelPerc;
    data.stoppedFuelPerc=currentDataPtr->currentFuelPerc;  //mostly so that fuel fill up doesn't get tripped
    data.lastElevation=currentDataPtr->currentElevation;

    data.priorTotalGallonsUsed=0;
    data.lastMiles=data.startMiles;
    data.priorTotalMiles=0;
    data.ignOffSeconds=0;
    data.totalParkedSeconds=0;
    data.totalStoppedSeconds=0;
    data.numberOfStops=0;
    data.totalClimb=0;
}

void TripData::updateStartValuesIfNeeded()
{
    if(startMilesNeedsUpdating && currentDataPtr->currentMilesOnline)
    {
        data.startMiles=currentDataPtr->currentMiles;
        startMilesNeedsUpdating=false;
        logger.log(WARNING,"Updated start miles now that distance is online");
    }

    if(startFuelNeedsUpdating && currentDataPtr->currentFuelPercOnline)
    {
        data.startFuelPerc=currentDataPtr->currentFuelPerc;
        data.stoppedFuelPerc=currentDataPtr->currentFuelPerc;
        startFuelNeedsUpdating=false;
        logger.log(WARNING,"Updated start (and stop) fuel now that fuel is online");
    }
}

//Save data to EEPROM
void TripData::saveTripData(int offset)
{
  logger.log(INFO,"Saving Trip Data to EEPROM");

  // The begin() call is required to initialise the EEPROM library
  int dataSize=sizeof(data);
  EEPROM.begin(512);

  // put some data into eeprom.  First number is where to start writing.
  EEPROM.put((dataSize*tripIdx)+offset, data); 

  // write the data to EEPROM
  logger.log(VERBOSE,"EEPROM Ret: %d",EEPROM.commit());

  dumpTripData();
}

//Load data to EEPROM
void TripData::loadTripData(int offset)
{
  logger.log(INFO,"Loading Trip Data from EEPROM");

  // The begin() call is required to initialise the EEPROM library
  int dataSize=sizeof(data);
  EEPROM.begin(512);
  // get some data from eeprom.  First number is where to start reading.
  EEPROM.get((dataSize*tripIdx)+offset, data);
  EEPROM.end();

  //do some error checking
  unsigned long currentTime=currentDataPtr->currentSeconds;
  if(currentTime < data.ignOffSeconds)
  {
    DateTime dateTime=DateTime(currentTime+SECONDS_FROM_1970_TO_2000);
    logger.log(ERROR,"EEPROM is *after* current seconds: %lu, or %d/%d/%d %d:%d:%d. (EEPROM %lu)  ",currentTime,dateTime.month(),dateTime.day(),dateTime.year(),dateTime.hour(),dateTime.minute(),dateTime.second(),data.ignOffSeconds);    

    //Let's try and reset things 
    currentDataPtr->setTime(data.ignOffSeconds);  
  }

  //dump
  dumpTripData();
}

void TripData::dumpTripData()
{
    logger.log(INFO,"Dumping TripData Idx: %d",tripIdx);
    logger.log(INFO,"   Start Miles: %ld",data.startMiles);
    logger.log(INFO,"   Start Seconds: %lu",data.startSeconds);
    logger.log(INFO,"   Start Fuel: %d",data.startFuelPerc);
    logger.log(INFO,"   Last Elev: %ld",data.lastElevation);
    logger.log(INFO,"   Last Miles: %ld",data.lastMiles);
    logger.log(INFO,"   Piror total Miles: %ld",data.priorTotalMiles);
    logger.log(INFO,"   Ign Off Sec: %lu",data.ignOffSeconds);
    logger.log(INFO,"   Parked Seconds: %lu",data.totalParkedSeconds);
    logger.log(INFO,"   Stopped Seconds: %lu",data.totalStoppedSeconds);
    logger.log(INFO,"   Num of Stops: %d",data.numberOfStops);
    logger.log(INFO,"   Stopped fuel: %d",data.stoppedFuelPerc);
    logger.log(INFO,"   Prior Gallons: %f",data.priorTotalGallonsUsed);
    logger.log(INFO,"   Total Climb: %ld",data.totalClimb);
}

//Store data about when van was turned off
void TripData::ignitionOff()
{
    data.stoppedFuelPerc=currentDataPtr->currentFuelPerc;        
    data.ignOffSeconds=currentDataPtr->currentSeconds;
    data.numberOfStops++;

    updateFuelGallonsUsed();
}

void TripData::ignitionOn()
{
    updateFuelGallonsUsed();

    int totalSecondsOff=currentDataPtr->currentSeconds-data.ignOffSeconds;
    if(totalSecondsOff>3600)
        data.totalParkedSeconds=data.totalParkedSeconds+totalSecondsOff;
    else
        data.totalStoppedSeconds=data.totalStoppedSeconds+totalSecondsOff;

    logger.log(INFO,"%d minutes since car was turned off",totalSecondsOff/60);
    
    data.ignOffSeconds=0;
}

void TripData::updateElevation()
{

    //Calc elevation gained
    long currentElevation=currentDataPtr->currentElevation;
    if(currentElevation>0)
    {
        if(currentElevation>data.lastElevation)
        {
            int climbToAdd=(currentElevation-data.lastElevation);
            data.totalClimb=data.totalClimb+climbToAdd;
            logger.log(VERBOSE,"ELEVATION add: %d  (Current: %ld)",climbToAdd,currentElevation);
        }
        data.lastElevation=currentElevation;
    }
}

// in miles
int TripData::getMilesTravelled()
{
    long currentMiles=currentDataPtr->currentMiles;

    //If MIL comes on, the current miles will reset to zero.  We should catch this situation
    //  Also, sometimes the miles don't update upon trip start
    if(currentMiles<data.lastMiles)
    {
        if(data.lastMiles>data.startMiles)
            data.priorTotalMiles=data.priorTotalMiles+data.lastMiles-data.startMiles;
        data.startMiles=currentMiles;
    }
    data.lastMiles=currentMiles;

    //OK, calc miles travelled once we have reasonable data
    int milesTravelled=0;
    if(currentMiles>data.startMiles)
        milesTravelled=currentMiles-data.startMiles+data.priorTotalMiles;

    return milesTravelled;
}

// in hours
double TripData::getElapsedTime()
{
    if(currentDataPtr->currentSeconds<=0)
        return 0;

    unsigned long seconds=currentDataPtr->currentSeconds-data.startSeconds;
    double hours=(double)seconds/3600.0;

    return(hours);
}

// in hours
double TripData::getDrivingTime()
{
    unsigned long seconds=currentDataPtr->currentSeconds-data.startSeconds-data.totalStoppedSeconds-data.totalParkedSeconds;
    double hours=(double)seconds/3600.0;

    return(hours);
}

// in hours
double TripData::getStoppedTime()
{
    double hours=(double)data.totalStoppedSeconds/3600.0;
    return(hours);
}

int TripData::getNumberOfStops()
{
    return data.numberOfStops;
}

// in hours
double TripData::getParkedTime()
{
    double hours=(double)data.totalParkedSeconds/3600.0;
    return(hours);
}

void TripData::updateFuelGallonsUsed()
{
    //Get current fuel tank level
    int currentFuelPerc=currentDataPtr->currentFuelPerc;

    //Did we fill up since we last stopped?
    if(currentFuelPerc>(data.stoppedFuelPerc+25)) 
    {
        logger.log(INFO,"Fueled up.  Tank now at %d",currentFuelPerc);

        //Calc gallons used *before* the fillup
        data.priorTotalGallonsUsed=data.priorTotalGallonsUsed+(FUEL_TANK_SIZE*((double)(data.startFuelPerc-data.stoppedFuelPerc)/100.0)); 
        data.startFuelPerc=currentFuelPerc;
        data.stoppedFuelPerc=currentFuelPerc;
    }

    //Calc current gallons used  (make sure values are valid)
    if(data.startFuelPerc>=currentFuelPerc)
    {
        fuelGallonsUsed = FUEL_TANK_SIZE*((double)(data.startFuelPerc-currentFuelPerc)/100.0);
        fuelGallonsUsed=fuelGallonsUsed+data.priorTotalGallonsUsed;
    }
}

// in gallons
double TripData::getFuelGallonsUsed()
{
    updateFuelGallonsUsed();
    return fuelGallonsUsed;
}

double TripData::getHeaterGallonsUsed()
{
    double currentFuelPerc=currentDataPtr->currentFuelPerc;
    
    //Make sure we didn't fill up at our stop
    double gallonsUsed=0.0;
    if(currentFuelPerc<data.stoppedFuelPerc && currentFuelPerc>0) 
    {
        gallonsUsed = FUEL_TANK_SIZE*((double)(data.stoppedFuelPerc-currentFuelPerc)/100.0);
    }

    return gallonsUsed;
}

//How many gallons would we expect to put in to fill up
double TripData::getGallonsExpected()
{
    double gallonsExpected = FUEL_TANK_SIZE-(FUEL_TANK_SIZE*((double)currentDataPtr->currentFuelPerc/100.0));
    return gallonsExpected;
}

double TripData::getInstantMPG()
{
    double galPerHour=((double)currentDataPtr->currentMAF*(double)currentDataPtr->currentLoad)/1006.777948;
    double currentInstMPG=(double)(currentDataPtr->currentSpeed)/galPerHour;
    instMPG = currentInstMPG;

    //running avg so we can calibrate on the fly with some filtering
    if(instMPG>3.0 && instMPG<30.0)
    {
        //Smooth
        instMPG = currentInstMPG*0.25 + instMPG*0.75;

        sumInstMPG=sumInstMPG+(instMPG*10);
        numInstMPGSamples++;
        lastAvgInstMPG=(sumInstMPG/numInstMPGSamples)/10.0;
    }

    //make adjustments based on avg if we're far enough along to have the data
    if(getFuelGallonsUsed()>=1.0 && getMilesTravelled()>=10 && (currentDataPtr->currentFuelPerc<75 || propBagPtr->data.instMPGFactor<0.01)) 
    {
        propBagPtr->data.instMPGFactor=getAvgMPG()/lastAvgInstMPG;
    }

    //Use the current factor we have (which may be loaded from EEPORM)
    if(propBagPtr->data.instMPGFactor>0.01) 
        instMPG=instMPG*propBagPtr->data.instMPGFactor;

    return instMPG;
}

double TripData::getAvgMPG()
{
    double gallonsUsed=getFuelGallonsUsed();
    int milesTravelled=getMilesTravelled();

    if(gallonsUsed<1.0 || milesTravelled<10)
        return getInstantMPG();

    double mpg=(double)milesTravelled/gallonsUsed;
    return(mpg);    
}

double TripData::getAvgMovingSpeed()
{
    double hours=getDrivingTime();
    int miles=getMilesTravelled();
    double avgSpeed=(double)miles/hours;

    //DEBUG
    //logger.log(VERBOSE,"Driving time: %f, Miles: %d, Avg Speed: %f",hours,miles,avgSpeed);
    return avgSpeed;
}

// Uses instant for miles left  (car already has the other)
int TripData::getMilesLeftInTank()
{
    double gallonsLeft=FUEL_TANK_SIZE*((double)currentDataPtr->currentFuelPerc/100.0);
    int milesLeft=gallonsLeft*getInstantMPG();

    if(milesLeft>999.0)
        milesLeft=999.0;

    return milesLeft;
}

// in feet
int TripData::getCurrentElevation()
{
    return currentDataPtr->currentElevation;
}

// in feet
long TripData::getTotalClimb()
{
    return data.totalClimb;
}
