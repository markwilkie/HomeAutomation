#include "../include/DistanceSensor.h"
#include <Arduino.h>
#include <math.h>
#include "../include/TFLunaUART.h" // Ensure new header is included
#include <SoftwareSerial.h> // Include SoftwareSerial

// Pin definitions for ultrasonic sensors
const int DistanceSensor::LEFT_TRIG_PIN = 8;    // Left ultrasonic trigger pin
const int DistanceSensor::LEFT_ECHO_PIN = 9;   // Left ultrasonic echo pin
const int DistanceSensor::RIGHT_TRIG_PIN = 6;  // Right ultrasonic trigger pin
const int DistanceSensor::RIGHT_ECHO_PIN = 7;  // Right ultrasonic echo pin

DistanceSensor::DistanceSensor(SoftwareSerial& lidarSerial) : _tfluna(lidarSerial) {}

void DistanceSensor::init() {
  _leftTrigPin = LEFT_TRIG_PIN;
  _leftEchoPin = LEFT_ECHO_PIN;
  _rightTrigPin = RIGHT_TRIG_PIN;
  _rightEchoPin = RIGHT_ECHO_PIN;
  
  _leftDistance = MAX_ULTRASONIC_DISTANCE; // Initialize distances to max
  _rightDistance = MAX_ULTRASONIC_DISTANCE;
  _frontDistance = MAX_LIDAR_DISTANCE;

  // Initialize occupancy state variables
  _lastPotentialEmptyTime = 0;
  _isConfirmedEmpty = true; // Assume empty initially until sensors confirm otherwise

  // Set up ultrasonic sensor pins
  pinMode(_leftTrigPin, OUTPUT);
  pinMode(_leftEchoPin, INPUT);
  pinMode(_rightTrigPin, OUTPUT);
  pinMode(_rightEchoPin, INPUT);

  // Initialize TF-Luna Sensor using the new class with SoftwareSerial
  _tfluna.init(); // Default baud rate is 115200
  Serial.println(F("TF-Luna SoftwareSerial Initialized")); // Update message
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
  if (distance == 0 || distance > MAX_ULTRASONIC_DISTANCE) { // Use MAX_ULTRASONIC_DISTANCE
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

// Method to read distance from TF-Luna Lidar sensor using direct UART implementation
int DistanceSensor::readFrontLidarCm() {
  // Attempt to get data from TF-Luna using the new class
  if (_tfluna.readData()) { // readData returns true if a complete packet was received
    int dist_cm = _tfluna.getDistance();
    if (dist_cm > 0 && dist_cm <= MAX_LIDAR_DISTANCE) {
      _frontDistance = dist_cm; // Update distance if valid reading within range
    } else {
      // Reading is out of range or invalid (e.g., 0 or > max)
      _frontDistance = MAX_LIDAR_DISTANCE;
    }
  } 

  Serial.print(F("Front distance: "));
  Serial.print(_frontDistance);
  Serial.println(F(" cm"));

  return _frontDistance;
}

//100% is perfectly centered, 0 is all the way forward
float DistanceSensor::getFrontPercent(float optimalFrontPerc) {
  int frontDistance = readFrontLidarCm(); // Get current average front distance
  
  float denom = optimalFrontPerc*(GARAGE_LENGTH - CAR_LENGTH)/2.0; // Adjusted denominator to account for car length
  float perc =  frontDistance / denom; // Adjusted denominator to account for car length

  return perc;
}

  // 100% is perfectly centered, 0 is full right and 200% is full left
float DistanceSensor::getSidePercent(float optimalSidePerc) {
  int sideDistance = readSideUltrasonicCm(); // Get current average side distance

  float denom = optimalSidePerc*(GARAGE_WIDTH - CAR_WIDTH)/2.0; // Adjusted denominator to account for car width
  float perc = sideDistance / denom; // Adjusted denominator to account for car width

  return perc;
} 

void DistanceSensor::readAllSensors() {
  // Read sensors sequentially
  readSideUltrasonicCm();
  readFrontLidarCm(); 
  // Update occupancy state after reading sensors
  updateGarageOccupancyState();
}

// Update garage occupancy state based on sensor readings and time
void DistanceSensor::updateGarageOccupancyState() {
  // Define conditions that suggest the garage is occupied.
  // Use the right sensor: if it detects something closer than half its max range.
  bool potentiallyOccupied = (_rightDistance < MAX_ULTRASONIC_DISTANCE / 2);

  unsigned long currentTime = millis();

  if (potentiallyOccupied) {
    // Garage seems occupied, reset empty confirmation state
    _isConfirmedEmpty = false;
    _lastPotentialEmptyTime = 0; // Reset timer
    // Serial.println("Garage potentially occupied (right sensor)."); // Debug
  } else {
    // Garage seems empty
    if (_lastPotentialEmptyTime == 0) {
      // Start the timer if it hasn't started yet
      _lastPotentialEmptyTime = currentTime;
      // Serial.println("Garage potentially empty, starting timer (right sensor)."); // Debug
    } else {
      // Timer is running, check if the delay has passed
      if (!_isConfirmedEmpty && (currentTime - _lastPotentialEmptyTime >= EMPTY_CONFIRMATION_DELAY)) {
        _isConfirmedEmpty = true; // Confirm empty after delay
        // Serial.println("Garage confirmed empty after delay (right sensor)."); // Debug
      }
    }
  }
}

// Check if the garage is confirmed empty
bool DistanceSensor::isGarageEmpty() {
  return _isConfirmedEmpty;
}