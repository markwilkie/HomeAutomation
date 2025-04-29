#include "../include/DistanceSensor.h"
#include <Arduino.h>
#include <math.h>
#include <Wire.h> // Include Wire library for I2C communication

// Pin definitions for ultrasonic sensors
// Removed FRONT_TRIG_PIN, FRONT_ECHO_PIN
const int DistanceSensor::LEFT_TRIG_PIN = 8;    // Left ultrasonic trigger pin
const int DistanceSensor::LEFT_ECHO_PIN = 9;   // Left ultrasonic echo pin
const int DistanceSensor::RIGHT_TRIG_PIN = 6;  // Right ultrasonic trigger pin
const int DistanceSensor::RIGHT_ECHO_PIN = 7;  // Right ultrasonic echo pin

Adafruit_VL53L1X _vl53 = Adafruit_VL53L1X(XSHUT_PIN, IRQ_PIN);

void DistanceSensor::init() {
  _leftTrigPin = LEFT_TRIG_PIN;
  _leftEchoPin = LEFT_ECHO_PIN;
  _rightTrigPin = RIGHT_TRIG_PIN;
  _rightEchoPin = RIGHT_ECHO_PIN;
  
  _leftDistance = 0;
  _rightDistance = 0;
  _frontDistance = 0;

  // Set up ultrasonic sensor pins
  pinMode(_leftTrigPin, OUTPUT);
  pinMode(_leftEchoPin, INPUT);
  pinMode(_rightTrigPin, OUTPUT);
  pinMode(_rightEchoPin, INPUT);

  // Initialize I2C for VL53L1X
  Wire.begin();
  if (! _vl53.begin(0x29, &Wire)) {
    Serial.print(F("Error on init of VL53L1X sensor: "));
    Serial.println(_vl53.vl_status);
    while (1) delay(10); // Halt if sensor init fails
  }
  Serial.println(F("VL53L1X sensor OK!"));

  if (! _vl53.startRanging()) {
    Serial.print(F("Couldn't start ranging VL53L1X: "));
    Serial.println(_vl53.vl_status);
    while (1) delay(10); // Halt if ranging fails
  }
  Serial.println(F("VL53L1X Ranging started"));

  // Set timing budget (50ms is a good default)
  _vl53.setTimingBudget(50);
  Serial.print(F("VL53L1X Timing budget (ms): "));
  Serial.println(_vl53.getTimingBudget());
}

// Common helper method for ultrasonic distance reading
int DistanceSensor::readUltrasonicCm(int trigPin, int echoPin) {
  // Clear the trigger pin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  
  // Set the trigger pin HIGH for 10 microseconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  // Read the echo pin, return the sound wave travel time in microseconds
  long duration = pulseIn(echoPin, HIGH, 30000); // Timeout after 30ms
  
  // Calculate the distance
  // Speed of sound is 343 m/s = 0.0343 cm/µs
  long distance = (duration / 2) * 0.0343;
  
  // Check if the reading is valid
  if (distance == 0 || distance > MAX_ULTRASONIC_DISTANCE) { // Use MAX_SIDE_DISTANCE
    distance = MAX_ULTRASONIC_DISTANCE; // Out of range or invalid reading
  }
  
  return distance;
}

int DistanceSensor::readSideUltrasonicCm() {
  _leftDistance = readUltrasonicCm(_leftTrigPin, _leftEchoPin);
  _rightDistance = readUltrasonicCm(_rightTrigPin, _rightEchoPin);

  Serial.print(F("Left distance: "));
  Serial.print(_leftDistance);  
  Serial.print(F(" cm | Right distance: "));
  Serial.print(_rightDistance);
  Serial.println(F(" cm"));

  // Average distance, handle cases where one sensor is out of range
  if (_leftDistance >= MAX_ULTRASONIC_DISTANCE && _rightDistance < MAX_ULTRASONIC_DISTANCE) {
      return _rightDistance;
   } else if (_leftDistance >= MAX_ULTRASONIC_DISTANCE && _rightDistance >= MAX_ULTRASONIC_DISTANCE) {
      return MAX_ULTRASONIC_DISTANCE; // Both out of range
  }
  return (_leftDistance + _rightDistance) / 2; // Average if both are in range
}

// Method to read distance from VL53L1X sensor
int DistanceSensor::readFrontVl53l1xCm() {
  int16_t distance_mm = -1; // Use int16_t as per library example

  if (_vl53.dataReady()) {
    // New measurement available
    distance_mm = _vl53.distance();
    if (distance_mm == -1) {
      // Error reading distance
      //Serial.println("VL out of range");
      _frontDistance = MAX_TOF_DISTANCE; // Set to max on error
    } else {
      // Convert mm to cm
      _frontDistance = distance_mm / 10;
      // Clamp to max distance
      if (_frontDistance > MAX_TOF_DISTANCE) {
          _frontDistance = MAX_TOF_DISTANCE;
      }
    }
    // Clear the interrupt flag to allow next measurement
    _vl53.clearInterrupt();

  } 

  // if still out of range, help out
  if (_frontDistance >= MAX_TOF_DISTANCE 
    && _rightDistance < MAX_ULTRASONIC_DISTANCE
    && _leftDistance >= MAX_ULTRASONIC_DISTANCE) {
    _frontDistance = .75 * GARAGE_LENGTH; // 25% of garage length
  }

  if(_frontDistance >= MAX_TOF_DISTANCE
    && _leftDistance < MAX_ULTRASONIC_DISTANCE
    && _leftDistance < MAX_ULTRASONIC_DISTANCE) {
    _frontDistance = .35 * GARAGE_LENGTH; // 65% of garage length
  }

  Serial.print(F("VL53L1X distance: "));
  Serial.println(_frontDistance);

  // If data is not ready, return the last known distance
  // This prevents blocking if the sensor is slow or unresponsive
  return _frontDistance;
}

float DistanceSensor::getFrontPercent() {
  float perc =  (float)_frontDistance / (float)GARAGE_LENGTH;

  if(perc < 0) {
    perc = 0; // Ensure percentage is not negative
  } else if(perc > 1) {
    perc = 1; // Ensure percentage does not exceed 100%
  }
  return perc;
}

float DistanceSensor::getSidePercent() {
  // Calculate the percentage of the distance from the maximum distance
  int sideDistance = readSideUltrasonicCm(); // Get current average side distance

  // Handle cases where one or both sensors might be out of range
  if (_leftDistance >= MAX_ULTRASONIC_DISTANCE && _rightDistance >= MAX_ULTRASONIC_DISTANCE) {
      sideDistance = MAX_ULTRASONIC_DISTANCE; // If both out of range, use max
  } else if (_leftDistance >= MAX_ULTRASONIC_DISTANCE) {
      sideDistance = _rightDistance; // Use right if left is out of range
  } else {
      sideDistance = (_leftDistance + _rightDistance) / 2; // Average if both in range
  }

  float perc = (float)sideDistance / (float)GARAGE_WIDTH;

  if(perc < 0) {
    perc = 0; // Ensure percentage is not negative
  } else if(perc > 1) {
    perc = 1; // Ensure percentage does not exceed 100%
  }
  return perc;
} 

void DistanceSensor::readAllSensors() {
  // Read sensors sequentially
  readSideUltrasonicCm();
  // No delay needed before VL53L1X as it's non-blocking
  readFrontVl53l1xCm(); 
}

// Once the left sensor see something, the car is in
bool DistanceSensor::isCarInGarage() {
  // Use the most recent _leftDistance value
  // Consider car in if left sensor sees something closer than half its max range
  return ( _rightDistance <= MAX_ULTRASONIC_DISTANCE / 2 ); 
}