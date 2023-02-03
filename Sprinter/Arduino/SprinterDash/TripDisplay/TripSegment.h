#ifndef TripSegment_h
#define TripSegment_h

#include <genieArduino.h>

#define FUEL_TANK_SIZE 20.0   // in gallons.   It's actually 25, but the last 5 aren't really usable

class TripSegment 
{
  public:
    void setNextSegment(TripSegment *);

    void update(int service,int pid,int value);

    double getMilesTravelled();
    double getHoursDriving();
    double getFuelGallonsUsed();
    double getInstantMPG();
    double getAvgMPG();
    int getMilesLeftInTank();
    int getCurrentElevation();
    int getTotalClimb();

  private: 

    //Next segment in this trip  (linked list)
    TripSegment* nextSegment=NULL;

    int calcAltitude(int currentkPa);

    //Particulars of this segment
    int startKm=-1;
    int currentKm=0;
    int baseSeconds=0;      //seconds from before engine turned off
    int currentSeconds=0;   //seconds from last engine start
    int startFuelPerc=-1;
    int currentFuelPerc=0;
    int maxElevation=-1;
    int minElevation=-1;
    int totalClimb=0;
    int currentElevation=0;
    int currentMAF=0;
    int currentSpeed=0;
    int currentLoad=0;
};

#endif