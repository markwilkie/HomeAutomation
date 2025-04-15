#line 1 "C:\\Users\\mark\\MyProjects\\GitHub\\GarageParker\\src\\DistanceSensor.cpp"
#include "../include/DistanceSensor.h"
#include <Arduino.h>
#include <math.h>

DistanceSensor::DistanceSensor(int irAnalogPin, 
                             int leftTrigPin, int leftEchoPin,
                             int rightTrigPin, int rightEchoPin,
                             float sensorSpacing) {
  _irAnalogPin = irAnalogPin;
  
  _leftTrigPin = leftTrigPin;
  _leftEchoPin = leftEchoPin;
  _rightTrigPin = rightTrigPin;
  _rightEchoPin = rightEchoPin;
  
  _sensorSpacing = sensorSpacing;
  
  _irDistance = 0;
  _leftDistance = 0;
  _rightDistance = 0;
  
  // Set default maximum distances
  _maxIRDistance = 80; // Sharp GP2Y0A21YK0F has a range of 10-80cm
  _maxUltrasonicDistance = 400; // HC-SR04 has a range up to 400-500cm
}

void DistanceSensor::init() {
  // Set up IR sensor pin as input
  pinMode(_irAnalogPin, INPUT);
  
  // Set up ultrasonic sensor pins
  pinMode(_leftTrigPin, OUTPUT);
  pinMode(_leftEchoPin, INPUT);
  pinMode(_rightTrigPin, OUTPUT);
  pinMode(_rightEchoPin, INPUT);
}

long DistanceSensor::readIRSensorCm() {
  // Read the analog value from the IR sensor
  int sensorValue = analogRead(_irAnalogPin);
  
  // Convert the analog reading to distance in cm
  // For Sharp GP2Y0A21YK0F, the formula is approximately:
  // Distance = 2076/(sensorValue - 11)
  // This is a simplified approximation and may need calibration
  
  long distance = 0;
  if (sensorValue > 30) { // Avoid division by small numbers
    distance = 2076 / (sensorValue - 11);
  } else {
    distance = _maxIRDistance; // Out of range or invalid reading
  }
  
  // Constrain to valid range for this sensor (10-80cm)
  distance = constrain(distance, 10, _maxIRDistance);
  
  _irDistance = distance;
  return distance;
}

long DistanceSensor::readLeftUltrasonicCm() {
  // Clear the trigger pin
  digitalWrite(_leftTrigPin, LOW);
  delayMicroseconds(2);
  
  // Set the trigger pin HIGH for 10 microseconds
  digitalWrite(_leftTrigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(_leftTrigPin, LOW);
  
  // Read the echo pin, return the sound wave travel time in microseconds
  long duration = pulseIn(_leftEchoPin, HIGH, 30000); // Timeout after 30ms
  
  // Calculate the distance
  // Speed of sound is 343 m/s = 0.0343 cm/µs
  long distance = (duration / 2) * 0.0343;
  
  // Check if the reading is valid
  if (distance == 0 || distance > _maxUltrasonicDistance) {
    distance = _maxUltrasonicDistance; // Out of range or invalid reading
  }
  
  _leftDistance = distance;
  return distance;
}

long DistanceSensor::readRightUltrasonicCm() {
  // Clear the trigger pin
  digitalWrite(_rightTrigPin, LOW);
  delayMicroseconds(2);
  
  // Set the trigger pin HIGH for 10 microseconds
  digitalWrite(_rightTrigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(_rightTrigPin, LOW);
  
  // Read the echo pin, return the sound wave travel time in microseconds
  long duration = pulseIn(_rightEchoPin, HIGH, 30000); // Timeout after 30ms
  
  // Calculate the distance
  // Speed of sound is 343 m/s = 0.0343 cm/µs
  long distance = (duration / 2) * 0.0343;
  
  // Check if the reading is valid
  if (distance == 0 || distance > _maxUltrasonicDistance) {
    distance = _maxUltrasonicDistance; // Out of range or invalid reading
  }
  
  _rightDistance = distance;
  return distance;
}

void DistanceSensor::readAllSensors() {
  // Read all sensors sequentially with small delays to avoid interference
  readLeftUltrasonicCm();
  delay(10);
  readIRSensorCm();
  delay(10);
  readRightUltrasonicCm();
}

float DistanceSensor::calculateCarAngle() {
  // Ensure we have fresh readings
  readAllSensors();
  
  // Calculate angle using the difference between left and right distances
  // Angle (in radians) = atan2(difference in distances, sensor spacing)
  float distanceDifference = _leftDistance - _rightDistance;
  
  // Calculate angle in radians
  float angleRadians = atan2(distanceDifference, _sensorSpacing);
  
  // Convert to degrees
  float angleDegrees = angleRadians * 180.0 / PI;
  
  return angleDegrees;
}

int DistanceSensor::getSidePosition() {
  // Calculate average position based on all three sensors
  readAllSensors();
  
  // Simple approach: compare left and right distances
  // If left is larger, car is closer to right side (position = 1)
  // If right is larger, car is closer to left side (position = -1)
  // If roughly equal (within 10% of each other), car is centered (position = 0)
  
  // Calculate difference as percentage
  float avgDistance = (_leftDistance + _rightDistance) / 2.0;
  float difference = _leftDistance - _rightDistance;
  float percentDifference = abs(difference) / avgDistance * 100.0;
  
  if (percentDifference < 10.0) {
    // Within 10% difference - considered centered
    return 0;
  } else if (_leftDistance > _rightDistance) {
    // Left distance greater - car is closer to right side
    return 1;
  } else {
    // Right distance greater - car is closer to left side
    return -1;
  }
}