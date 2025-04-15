#ifndef DISTANCE_SENSOR_H
#define DISTANCE_SENSOR_H

#include <Arduino.h>

class DistanceSensor {
  private:
    // IR sensor (middle)
    int _irAnalogPin;
    
    // Ultrasonic sensors (left and right)
    int _leftTrigPin;
    int _leftEchoPin;
    int _rightTrigPin;
    int _rightEchoPin;
    
    // Distances
    long _irDistance;      // Distance from IR sensor (middle)
    long _leftDistance;    // Distance from left ultrasonic sensor
    long _rightDistance;   // Distance from right ultrasonic sensor
    
    // Sensor spacing (in cm)
    float _sensorSpacing;  // Distance between left and right sensors
    
    // Maximum distance for sensors
    int _maxIRDistance;
    int _maxUltrasonicDistance;
    
  public:
    DistanceSensor(int irAnalogPin, 
                  int leftTrigPin, int leftEchoPin,
                  int rightTrigPin, int rightEchoPin,
                  float sensorSpacing);
                  
    void init();
    
    // Read individual sensors
    long readIRSensorCm();
    long readLeftUltrasonicCm();
    long readRightUltrasonicCm();
    
    // Read all sensors at once
    void readAllSensors();
    
    // Get stored distance values
    long getIRDistance() const { return _irDistance; }
    long getLeftDistance() const { return _leftDistance; }
    long getRightDistance() const { return _rightDistance; }
    
    // Calculate car angle in degrees
    // Positive angle means the car is angled clockwise
    // Negative angle means the car is angled counter-clockwise
    // Zero means the car is parallel
    float calculateCarAngle();
    
    // Calculate side-to-side position (-1 = left, 0 = center, 1 = right)
    int getSidePosition();
};

#endif