#include "../include/DistanceSensor.h"
#include <Arduino.h>
#include <math.h>

// Pin definitions for ultrasonic sensors
const int DistanceSensor::FRONT_TRIG_PIN = 4;   // Front ultrasonic trigger pin
const int DistanceSensor::FRONT_ECHO_PIN = 5;   // Front ultrasonic echo pin
const int DistanceSensor::LEFT_TRIG_PIN = 6;    // Left ultrasonic trigger pin
const int DistanceSensor::LEFT_ECHO_PIN = 7;   // Left ultrasonic echo pin
const int DistanceSensor::RIGHT_TRIG_PIN = 8;  // Right ultrasonic trigger pin
const int DistanceSensor::RIGHT_ECHO_PIN = 9;  // Right ultrasonic echo pin

void DistanceSensor::init() {
  _leftTrigPin = LEFT_TRIG_PIN;
  _leftEchoPin = LEFT_ECHO_PIN;
  _rightTrigPin = RIGHT_TRIG_PIN;
  _rightEchoPin = RIGHT_ECHO_PIN;
  _FrontTrigPin = FRONT_TRIG_PIN;
  _FrontEchoPin = FRONT_ECHO_PIN;
  
  _leftDistance = 0;
  _rightDistance = 0;
  _frontDistance = 0;

  // Set up ultrasonic sensor pins
  pinMode(_leftTrigPin, OUTPUT);
  pinMode(_leftEchoPin, INPUT);
  pinMode(_rightTrigPin, OUTPUT);
  pinMode(_rightEchoPin, INPUT);
  pinMode(_FrontTrigPin, OUTPUT);
  pinMode(_FrontEchoPin, INPUT);
}

// Common helper method for ultrasonic distance reading
long DistanceSensor::readUltrasonicCm(int trigPin, int echoPin) {
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
  if (distance == 0 || distance > MAX_ULTRASONIC_DISTANCE) {
    distance = MAX_ULTRASONIC_DISTANCE; // Out of range or invalid reading
  }
  
  return distance;
}

long DistanceSensor::readSideUltrasonicCm() {
  _leftDistance = readUltrasonicCm(_leftTrigPin, _leftEchoPin);
  _rightDistance = readUltrasonicCm(_rightTrigPin, _rightEchoPin);
  return (_leftDistance + _rightDistance) / 2; // Average distance from both sensors
}

long DistanceSensor::readFrontUltrasonicCm() {
  _frontDistance = readUltrasonicCm(_FrontTrigPin, _FrontEchoPin);
  return _frontDistance;
}

float DistanceSensor::getFrontPercent() {
  // Calculate the percentage of the distance from the maximum distance
  float perc =  ((_frontDistance / (float)MAX_ULTRASONIC_DISTANCE)) * 100.0;

  if(perc < 0) {
    perc = 0; // Ensure percentage is not negative
  } else if(perc > 100) {
    perc = 100; // Ensure percentage does not exceed 100%
  }
  return perc;
}

float DistanceSensor::getSidePercent() {
  // Calculate the percentage of the distance from the maximum distance
  int sideDistance = (_leftDistance + _rightDistance) / 2;
  if(_leftDistance>=MAX_SIDE_DIST)
    sideDistance = _rightDistance;
  else if(_rightDistance>=MAX_SIDE_DIST)
    sideDistance = _leftDistance;

  float perc = ((sideDistance / (float)MAX_SIDE_DIST)) * 100.0;

  if(perc < 0) {
    perc = 0; // Ensure percentage is not negative
  } else if(perc > 100) {
    perc = 100; // Ensure percentage does not exceed 100%
  }
  return perc;
} 

void DistanceSensor::readAllSensors() {
  // Read sensors sequentially with a small delay to avoid interference
  readSideUltrasonicCm();
  delay(10);
  readFrontUltrasonicCm();
}

// Once the left sensor see something, the car is in
bool DistanceSensor::isCarInGarage() {
  return ( _leftDistance <= MAX_ULTRASONIC_DISTANCE/2 );
}