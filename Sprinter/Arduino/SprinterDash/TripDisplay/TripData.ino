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

    data.startMiles=currentDataPtr->currentMiles;
    data.startSeconds=currentDataPtr->currentSeconds;
    data.startFuelPerc=currentDataPtr->currentFuelPerc;
    data.lastElevation=currentDataPtr->currentElevation;

    data.lastMiles=data.startMiles;
    data.priorTotalMiles=0;
    data.ignOffSeconds=0;
    data.totalParkedSeconds=0;
    data.totalStoppedSeconds=0;
    data.numberOfStops=0;
    data.lastFuelPerc=data.startFuelPerc;
    data.stoppedFuelPerc=0;
    data.priorTotalGallonsUsed=0;
    data.totalClimb=0;
}

//Save data to EEPROM
void TripData::saveTripData(int offset)
{
  logger.log(VERBOSE,"Saving Trip Data to EEPROM");

  // The begin() call is required to initialise the EEPROM library
  int dataSize=sizeof(data);
  EEPROM.begin(512);

  // put some data into eeprom
  EEPROM.put((dataSize*tripIdx)+offset, data); 

  // write the data to EEPROM
  logger.log(VERBOSE,"EEPROM Ret: %d",EEPROM.commit());

  dumpTripData();
}

//Load data to EEPROM
void TripData::loadTripData(int offset)
{
  logger.log(VERBOSE,"Loading Trip Data from EEPROM");

  // The begin() call is required to initialise the EEPROM library
  int dataSize=sizeof(data);
  EEPROM.begin(512);
  // get some data from eeprom
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
    logger.log(INFO,"   Start Fuel: %f",data.startFuelPerc);
    logger.log(INFO,"   Last Elev: %ld",data.lastElevation);
    logger.log(INFO,"   Last Miles: %ld",data.lastMiles);
    logger.log(INFO,"   Piror total Miles: %ld",data.priorTotalMiles);
    logger.log(INFO,"   Ign Off Sec: %lu",data.ignOffSeconds);
    logger.log(INFO,"   Parked Seconds: %lu",data.totalParkedSeconds);
    logger.log(INFO,"   Stopped Seconds: %lu",data.totalStoppedSeconds);
    logger.log(INFO,"   Num of Stops: %d",data.numberOfStops);
    logger.log(INFO,"   Last fuel: %f",data.lastFuelPerc);
    logger.log(INFO,"   Prior total Gall: %f",data.priorTotalGallonsUsed);
    logger.log(INFO,"   Total Climb: %ld",data.totalClimb);
}

void TripData::updateTripData()
{

    //Calc elevation gained
    int currentElevation=currentDataPtr->currentElevation;
    if(currentElevation>0)
    {
        if(currentElevation>data.lastElevation)
        {
            data.totalClimb=data.totalClimb+(currentElevation-data.lastElevation);
        }
        data.lastElevation=currentElevation;
    }

    //Calc time igntion off and the buckets that go with that
    unsigned long currentSeconds = currentDataPtr->currentSeconds;

    //Store time the car was shut off
    if(!currentDataPtr->ignitionState && data.ignOffSeconds==0)
    {
        data.stoppedFuelPerc=currentDataPtr->currentFuelPerc;        
        data.ignOffSeconds=currentSeconds;
        data.numberOfStops++;
    }

    //Store time the car was started again
    if(currentDataPtr->ignitionState && data.ignOffSeconds>0)
    {
        int totalSecondsOff=currentSeconds-data.ignOffSeconds;
        if(totalSecondsOff>3600)
            data.totalParkedSeconds=data.totalParkedSeconds+totalSecondsOff;
        else
            data.totalStoppedSeconds=data.totalStoppedSeconds+totalSecondsOff;
        
        data.ignOffSeconds=0;
    }
}

// in miles
int TripData::getMilesTravelled()
{
    int currentMiles=currentDataPtr->currentMiles;
    if(currentMiles<=0)
        return 0;

    if(currentMiles<data.lastMiles)
    {
        data.priorTotalMiles=data.priorTotalMiles+data.lastMiles-data.startMiles;
        data.startMiles=currentMiles;
    }
    data.lastMiles=currentMiles;

    int milesTravelled=currentMiles-data.startMiles+data.priorTotalMiles;

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

// in gallons
double TripData::getFuelGallonsUsed()
{
    double currentFuelPerc=currentDataPtr->currentFuelPerc;
    if(currentFuelPerc<=0)
        return 0;

    unsigned long currentSeconds=currentDataPtr->currentSeconds;
    unsigned long fillUpSeconds=currentDataPtr->fillUpSeconds;

    //Do we need to factor in fuel being added?
    if(currentSeconds>fillUpSeconds && currentSeconds>lastFillupSeconds)
    {
        //Has it been long enough to stabalize?
        if(currentSeconds>(fillUpSeconds+15))
        {
            lastFillupSeconds=fillUpSeconds;
            double priorGallonsUsed=FUEL_TANK_SIZE*((data.startFuelPerc-data.lastFuelPerc)/100.0);
            data.priorTotalGallonsUsed=data.priorTotalGallonsUsed+priorGallonsUsed;
            data.startFuelPerc=currentFuelPerc;
        }
    }
    else
    {
        //Make sure we always know the last fuel percentage in case we add fuel
        data.lastFuelPerc=currentFuelPerc;
    }

    //Calc current gallons used
    double gallonsUsed = FUEL_TANK_SIZE*((data.startFuelPerc-currentFuelPerc)/100.0);
    gallonsUsed=gallonsUsed+data.priorTotalGallonsUsed;

    return gallonsUsed;
}

double TripData::getHeaterGallonsUsed()
{
    double currentFuelPerc=currentDataPtr->currentFuelPerc;
    double gallonsUsed = FUEL_TANK_SIZE*((currentFuelPerc-data.stoppedFuelPerc)/100.0);

    return gallonsUsed;
}

double TripData::getGallonsExpected()
{
    double currentFuelPerc=currentDataPtr->currentFuelPerc;
    double gallonsExpected = FUEL_TANK_SIZE-(FUEL_TANK_SIZE*(currentFuelPerc/100.0));
    return gallonsExpected;
}

double TripData::getInstantMPG()
{
    double galPerHour=((double)currentDataPtr->currentMAF*(double)currentDataPtr->currentLoad)/1006.777948;
    double currentInstMPG=(currentDataPtr->currentSpeed)/galPerHour;
    instMPG = currentInstMPG;

    //running avg so we can calibrate on the fly with some filtering
    if(instMPG>3 && instMPG<30)
    {
        //Smooth
        instMPG = currentInstMPG*0.25 + instMPG*0.75;

        sumInstMPG=sumInstMPG+(instMPG*10);
        numInstMPGSamples++;
        lastAvgInstMPG=(sumInstMPG/numInstMPGSamples)/10.0;
    }

    //make adjustments based on avg if we're far enough along to have the data
    if(getFuelGallonsUsed()>=1.0 && getMilesTravelled()>=10 && (currentDataPtr->currentFuelPerc<75 || propBagPtr->data.instMPGFactor==0)) 
    {
        propBagPtr->data.instMPGFactor=getAvgMPG()/lastAvgInstMPG;
    }

    //Use the current factor we have (which may be loaded from EEPORM)
    if(propBagPtr->data.instMPGFactor>0)
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
    return (double)getMilesTravelled()/hours;
}

// Uses instant for miles left  (car already has the other)
int TripData::getMilesLeftInTank()
{
    double gallonsLeft=FUEL_TANK_SIZE*((double)currentDataPtr->currentFuelPerc/100.0);
    double milesLeft=gallonsLeft*getInstantMPG();

    if(milesLeft>999)
        milesLeft=999;

    return (int)milesLeft;
}

// in feet
int TripData::getCurrentElevation()
{
    return currentDataPtr->currentElevation;
}

// in feet
int TripData::getTotalClimb()
{
    return data.totalClimb;
}
