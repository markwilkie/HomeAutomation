#include "TripSegmentData.h"

// update value if appropriate
void TripSegmentData::update(int service,int pid,int value)
{
    //calc engine load
    if(service==0x41 && pid==0x04)
    {
        currentLoad=value;
        return;
    }
    //distance travelled in km
    if(service==0x41 && pid==0x31)
    {
        if(startKm<0)  startKm=value;
        currentKm=value;
        return;
    }
    //time since start in seconds
    if(service==0x41 && pid==0x1F)
    {
        currentSeconds=value;
        return;
    }  
    //fuel available in percentage  (30 --> 30%)
    if(service==0x41 && pid==0x2F)
    {
        if(startFuelPerc<0)   startFuelPerc=value;
        currentFuelPerc=value;
        return;
    }
    //elevation via atmos pressure in kPa
    if(service==0x41 && pid==0x33)
    {
        int _elevation=calcAltitude(value);
        
        if(_elevation>maxElevation || maxElevation<0)
            maxElevation=_elevation;

        if(_elevation<minElevation || minElevation<0)
            minElevation=_elevation;

        if(_elevation>currentElevation)
            totalClimb=totalClimb+(_elevation-currentElevation);
        
        currentElevation=_elevation;
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
        currentSpeed=value;
        return;
    } 
}

// in miles
double TripSegmentData::getMilesTravelled()
{
    double milesTravlled=((double)currentKm-(double)startKm)*0.621371;
    return ((double)currentKm-(double)startKm)*0.621371;
}

// in minutes
double TripSegmentData::getHoursDriving()
{
    if(currentSeconds<=0)
        return 0.0;

    double hours=(double)currentSeconds/3600.0;
    return(hours);
}

// in gallons
double TripSegmentData::getFuelGallonsUsed()
{
    if(currentFuelPerc<=0 || startFuelPerc<=0)
        return 0;

    return FUEL_TANK_SIZE*(((double)currentFuelPerc-(double)startFuelPerc)/100.0);
}

double TripSegmentData::getInstantMPG()
{
    if(currentMAF<=0)
        return 0;

    double galPerHour=((double)currentMAF*(double)currentLoad)/1006.777948;
    double instMPG=(currentSpeed*0.621371)/galPerHour;

    return instMPG;
}

double TripSegmentData::getAvgMPG()
{
    double gallonsUsed=getFuelGallonsUsed();
    int milesTravelled=getMilesTravelled();

    if(gallonsUsed==0 || milesTravelled==0)
        return getInstantMPG();

    double mpg=(double)milesTravelled/gallonsUsed;
    return(mpg);    
}

// Uses instant for miles left  (car already has the other)
int TripSegmentData::getMilesLeftInTank()
{
    if(currentFuelPerc<=0)
        return 0;   

    double gallonsLeft=FUEL_TANK_SIZE*((double)currentFuelPerc/100.0);
    double milesLeft=gallonsLeft*getInstantMPG();

    if(milesLeft>999)
        milesLeft=999;

    return (int)milesLeft;
}

// in feet
int TripSegmentData::getCurrentElevation()
{
    return currentElevation;
}

// in feet
int TripSegmentData::getTotalClimb()
{
    return totalClimb;
}

// return elevation in feet
// note that this is inacurate because it makes an assumption about what pressure sea level is at
int TripSegmentData::calcAltitude(int currentkPa) 
{
    if(currentkPa<0)
        return 0;

    float seaLevel = 1013.25;  //in hPa
    float temp = 15.0;  //in C
    
    float elevation = ((pow((seaLevel / (currentkPa*10)), 1/5.257) - 1.0) * (temp + 273.15)) / 0.0065;
    elevation = elevation * 3.28084;  // convert to feet
    return (int)elevation;
}
