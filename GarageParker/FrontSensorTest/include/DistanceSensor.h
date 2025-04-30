#ifndef DISTANCE_SENSOR_H
#define DISTANCE_SENSOR_H

#include <Arduino.h>
#include "TFLunaUART.h" // Include the new direct UART implementation
#include <SoftwareSerial.h> // Include SoftwareSerial

#define GARAGE_LENGTH 670 //size of garage space front to back

#define MAX_LIDAR_DISTANCE 800 // Maximum distance for TF-Luna sensor can see in cm (TF-Luna range is up to 8m)
#define OPTIMAL_FRONT_DISTANCE 120      // perfect distance from the front wall in cm

class DistanceSensor {
  private:
    // Distances
    int _frontDistance;  // Distance from front TF-Luna Lidar sensor (cm)

    // TF-Luna specific objects
    TFLunaUART _tfluna; // Use the new TFLunaUART class

  public:
    // Constructor now takes SoftwareSerial reference for TF-Luna
    DistanceSensor(SoftwareSerial& lidarSerial);

    void init();

    // Read individual sensors
    int readFrontLidarCm(); // Renamed method for clarity

    // Read all sensors at once
    void readSensor(); // Renamed from readAllSensors

    // Return %
    float getFrontPercent();
};

#endif