#ifndef TripSegment_h
#define TripSegment_h

#include <genieArduino.h>

#define FUEL_TANK_SIZE 25.0   // in gallons

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
    int currentSeconds=0;
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