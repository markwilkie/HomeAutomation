#ifndef TripSegmentData_h
#define TripSegmentData_h

#define FUEL_TANK_SIZE 22.0   // in gallons

class TripSegmentData
{
  public:
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