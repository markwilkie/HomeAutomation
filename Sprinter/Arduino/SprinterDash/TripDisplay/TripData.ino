#include "TripData.h"

TripData::TripData(CurrentData *_currentDataPtr)
{
    currentDataPtr=_currentDataPtr;
}

void TripData::resetTripData()
{
    startValuesSetFlag=true;
    startMiles=currentDataPtr->currentMiles;
    startSeconds=currentDataPtr->currentSeconds;
    startFuelPerc=currentDataPtr->currentFuelPerc;
    lastElevation=currentDataPtr->currentElevation;
    totalClimb=0;
}

void TripData::updateTripData()
{
    //If we haven't set the starting values yet, we need to
    if(!startValuesSetFlag)
        resetTripData();

    //Calc elevation gained
    if(currentDataPtr->currentElevation>lastElevation)
    {
        totalClimb=totalClimb+(currentDataPtr->currentElevation-lastElevation);
    }
    lastElevation=currentDataPtr->currentElevation;
}

// in miles
int TripData::getMilesTravelled()
{
    int milesTravelled=currentDataPtr->currentMiles-startMiles;
    return milesTravelled;
}

// in minutes
double TripData::getHoursDriving()
{
    unsigned long seconds=currentDataPtr->currentSeconds-startSeconds;
    double hours=(double)seconds/3600.0;

    return(hours);
}

// in gallons
double TripData::getFuelGallonsUsed()
{
    return FUEL_TANK_SIZE*(((double)currentDataPtr->currentFuelPerc-(double)startFuelPerc)/100.0);
}

double TripData::getInstantMPG()
{
    double galPerHour=((double)currentDataPtr->currentMAF*(double)currentDataPtr->currentLoad)/1006.777948;
    double instMPG=(currentDataPtr->currentSpeed)/galPerHour;

    return instMPG;
}

double TripData::getAvgMPG()
{
    double gallonsUsed=getFuelGallonsUsed();
    int milesTravelled=getMilesTravelled();

    if(gallonsUsed==0 || milesTravelled==0)
        return getInstantMPG();

    double mpg=(double)milesTravelled/gallonsUsed;
    return(mpg);    
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
    return totalClimb;
}
