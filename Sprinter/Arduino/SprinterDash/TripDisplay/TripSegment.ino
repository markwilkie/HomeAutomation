#include "TripSegment.h"

// 'Trip' object will set this next segment as part of the linked list  (root is in 'Trip')
void TripSegment::setNextSegment(TripSegment *_segment)
{
    nextSegment=_segment;
}

// update value if appropriate
void TripSegment::update(int service,int pid,int value)
{
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
double TripSegment::getMilesTravelled()
{
    double milesTravlled=((double)currentKm-(double)startKm)*0.621371;
    return ((double)currentKm-(double)startKm)*0.621371;
}

// in minutes
double TripSegment::getHoursDriving()
{
    if(currentSeconds<=0)
        return 0.0;

    return ((float)currentSeconds/60.0/60.0);
}

// in gallons
double TripSegment::getFuelGallonsUsed()
{
    if(currentFuelPerc<=0 || startFuelPerc<=0)
        return 0;

    return FUEL_TANK_SIZE*(((double)currentFuelPerc-(double)startFuelPerc)/100.0);
}

double TripSegment::getInstantMPG()
{
    //
    //remember that gauge is  10-30  (means, 1 will translate to 11)
    //
    if(currentMAF<=0)
        return 0;

    double galPerHour=(((currentMAF/14.7)/454)/6.701)*3600.0;
    return (currentSpeed*0.621371)/galPerHour;
}

double TripSegment::getAvgMPG()
{
    double gallonsUsed=getFuelGallonsUsed();
    int milesTravelled=getMilesTravelled();
    return (double)milesTravelled/gallonsUsed;
}

// Uses instant for miles left  (car already has the other)
int TripSegment::getMilesLeftInTank()
{
    if(currentFuelPerc<=0)
        return 0;

    double gallonsLeft=FUEL_TANK_SIZE*(double)currentFuelPerc;
    double milesLeft=gallonsLeft*getInstantMPG();
    return (int)milesLeft;
}

// in feet
int TripSegment::getCurrentElevation()
{
    return currentElevation;
}

// in feet
int TripSegment::getTotalClimb()
{
    return totalClimb;
}

// return elevation in feet
// note that this is inacurate because it makes an assumption about what pressure sea level is at
int TripSegment::calcAltitude(int currentkPa) 
{
    if(currentkPa<0)
        return 0;

    float seaLevel = 1013.25;  //in hPa
    float temp = 15.0;  //in C
    
    float elevation = ((pow((seaLevel / (currentkPa*10)), 1/5.257) - 1.0) * (temp + 273.15)) / 0.0065;
    elevation = elevation * 3.28084;  // convert to feet
    return (int)elevation;
}