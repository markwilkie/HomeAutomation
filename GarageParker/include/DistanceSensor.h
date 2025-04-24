#ifndef DISTANCE_SENSOR_H
#define DISTANCE_SENSOR_H

#include <Arduino.h>

#define MAX_ULTRASONIC_DISTANCE 300 // Maximum distance for ultrasonic sensors in cm

#define MIN_SIDE_DIST 50       // Desired distance from the wall in cm
#define MAX_SIDE_DIST 80       // Maximum distance from the wall in cm
#define MIN_FRONT_DIST 60      // Minimum distance from the front wall in cm
#define MAX_FRONT_DIST 100      // Maximum distance from the front wall in cm


class DistanceSensor {
  private:
    // Ultrasonic sensors (left and right)
    int _leftTrigPin;
    int _leftEchoPin;
    int _rightTrigPin;
    int _rightEchoPin;
    int _FrontTrigPin;
    int _FrontEchoPin;
    
    // Distances
    long _leftDistance;   // Distance from left ultrasonic sensor
    long _rightDistance;  // Distance from right ultrasonic sensor
    long _frontDistance;  // Distance from front ultrasonic sensor 
    

    // Helper method for ultrasonic sensor readings
    long readUltrasonicCm(int trigPin, int echoPin);
    
  public:
    // Pin definitions for ultrasonic sensors - these replace the constants in the main file
    static const int LEFT_TRIG_PIN;    // Left ultrasonic trigger pin
    static const int LEFT_ECHO_PIN;    // Left ultrasonic echo pin
    static const int RIGHT_TRIG_PIN;   // Right ultrasonic trigger pin
    static const int RIGHT_ECHO_PIN;   // Right ultrasonic echo pin
    static const int FRONT_TRIG_PIN;   // Front ultrasonic trigger pin
    static const int FRONT_ECHO_PIN;   // Front ultrasonic echo pin
                
    void init();
    
    // Read individual sensors
    long readSideUltrasonicCm();
    long readFrontUltrasonicCm();

    // Read all sensors at once
    void readAllSensors();

    // Return %
    float getSidePercent();
    float getFrontPercent();

    // Is car in garage
    bool isCarInGarage();
};

#endif