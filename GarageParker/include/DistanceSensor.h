#ifndef DISTANCE_SENSOR_H
#define DISTANCE_SENSOR_H

#include <Arduino.h>
#include <Adafruit_VL53L1X.h> // Include the VL53L1X library

#define IRQ_PIN 2
#define XSHUT_PIN 3

#define GARAGE_WIDTH 310 //size of garage space side to side in cm
#define GARAGE_LENGTH 670 //size of garage space front to back

#define MAX_ULTRASONIC_DISTANCE 300 // Maximum distance for ultrasonic sensors can see in cm
#define MAX_TOF_DISTANCE 400 // Maximum distance for VL53L1X sensor can see in cm
#define OPTIMAL_SIDE_DISTANCE 75       // perfect distance from the wall in cm
#define OPTIMAL_FRONT_DISTANCE 120      // perfect distance from the front wall in cm


class DistanceSensor {
  private:
    // Ultrasonic sensors (left and right)
    int _leftTrigPin;
    int _leftEchoPin;
    int _rightTrigPin;
    int _rightEchoPin;
    // Removed _FrontTrigPin, _FrontEchoPin
    
    // Distances
    int _leftDistance;   // Distance from left ultrasonic sensor (cm)
    int _rightDistance;  // Distance from right ultrasonic sensor (cm)
    int _frontDistance;  // Distance from front VL53L1X sensor (cm)
    

    // Helper method for ultrasonic sensor readings
    int readUltrasonicCm(int trigPin, int echoPin);
    
  public:
    // Pin definitions for ultrasonic sensors
    static const int LEFT_TRIG_PIN;    // Left ultrasonic trigger pin
    static const int LEFT_ECHO_PIN;    // Left ultrasonic echo pin
    static const int RIGHT_TRIG_PIN;   // Right ultrasonic trigger pin
    static const int RIGHT_ECHO_PIN;   // Right ultrasonic echo pin
    // Removed FRONT_TRIG_PIN, FRONT_ECHO_PIN
              
    void init();
    
    // Read individual sensors
    int readSideUltrasonicCm();
    int readFrontVl53l1xCm(); // Renamed method for clarity

    // Read all sensors at once
    void readAllSensors();

    // Return %
    float getSidePercent();
    float getFrontPercent();

    // Is car in garage
    bool isCarInGarage();
};

#endif