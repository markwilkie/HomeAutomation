#ifndef WATERTANK_H
#define WATERTANK_H

//Defines
#define WATER_SENSOR0_PIN 12
#define WATER_SENSOR1_PIN 11
#define WATER_SENSOR2_PIN 10

#define WATER_SENSOR0_LEVEL 90
#define WATER_SENSOR1_LEVEL 50
#define WATER_SENSOR2_LEVEL 20

class WaterTank {
private:

  //properties
  int stateOfWater;  //perc full the tank is
  long waterRemaining;
  long lastFilled;
   
public:
   //members

  void init();
  int readLevel();
  double getSoW() const { return stateOfWater; }
  long getLastFilled() const { return lastFilled; }
  double getDaysRemaining(); //calculates time remaining based on given usage
};

#endif 
