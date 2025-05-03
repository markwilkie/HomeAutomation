#ifndef DISTANCE_SENSOR_H
#define DISTANCE_SENSOR_H

#include <Arduino.h>
#include "TFLunaUART.h" // Include the new direct UART implementation
#include <SoftwareSerial.h> // Include SoftwareSerial

#define GARAGE_WIDTH 310 //size of garage space side to side in cm
#define GARAGE_LENGTH 670 //size of garage space front to back

#define CAR_WIDTH 180 // Width of the car in cm
#define CAR_LENGTH 460 // Length of the car in cm

#define MAX_ULTRASONIC_DISTANCE 300 // Maximum distance for ultrasonic sensors can see in cm
#define MAX_LIDAR_DISTANCE 800 // Maximum distance for TF-Luna sensor can see in cm (TF-Luna range is up to 8m)
//#define OPTIMAL_SIDE_DISTANCE 75       // perfect distance from the wall in cm
//#define OPTIMAL_FRONT_DISTANCE 120      // perfect distance from the front wall in cm

// Add constant for the empty confirmation delay
#define EMPTY_CONFIRMATION_DELAY 10000 // 10 seconds in milliseconds

class DistanceSensor {
  private:
    // Ultrasonic sensors (left and right)
    int _leftTrigPin;
    int _leftEchoPin;
    int _rightTrigPin;
    int _rightEchoPin;
    
    // Distances
    int _leftDistance;   // Distance from left ultrasonic sensor (cm)
    int _rightDistance;  // Distance from right ultrasonic sensor (cm)
    int _frontDistance;  // Distance from front TF-Luna Lidar sensor (cm)
    
    // Add state variables for garage occupancy
    unsigned long _lastPotentialEmptyTime; // Timestamp when the garage might have become empty
    bool _isConfirmedEmpty; // Flag indicating if the garage is confirmed empty

    // TF-Luna specific objects
    TFLunaUART _tfluna; // Use the new TFLunaUART class

    // Helper method for ultrasonic sensor readings
    int readUltrasonicCm(int trigPin, int echoPin);
    
  public:
    // Pin definitions for ultrasonic sensors
    static const int LEFT_TRIG_PIN;    // Left ultrasonic trigger pin
    static const int LEFT_ECHO_PIN;    // Left ultrasonic echo pin
    static const int RIGHT_TRIG_PIN;   // Right ultrasonic trigger pin
    static const int RIGHT_ECHO_PIN;   // Right ultrasonic echo pin

    // Constructor now takes SoftwareSerial reference for TF-Luna
    DistanceSensor(SoftwareSerial& lidarSerial);
              
    void init();
    
    // Read individual sensors
    int readSideUltrasonicCm();
    int readFrontLidarCm(); // Renamed method for clarity

    // Read all sensors at once
    void readAllSensors();

    // Return % 
    float getSidePercent(float optimalSidePerdc);
    float getFrontPercent(float optimalFrontPerc);

    // Update garage occupancy state based on sensor readings
    void updateGarageOccupancyState();

    // Check if the garage is confirmed empty
    bool isGarageEmpty();
};

#endif