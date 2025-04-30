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
  } else {
    // Failed to get data (e.g., communication error or incomplete packet)
    // Keep the previous distance or set to max? For now, keep previous valid reading.
    // If you prefer setting to max on failure, uncomment the next line:
    // _frontDistance = MAX_LIDAR_DISTANCE;
    // Serial.println("TF-Luna readData() failed or no new data"); // Optional debug message
  }

  Serial.print(F("Front distance: "));
  Serial.print(_frontDistance);
  Serial.println(F(" cm"));

  // Keep the heuristic logic for out-of-range scenarios if desired,
  // but adjust the max distance check to MAX_LIDAR_DISTANCE
  if (_frontDistance >= MAX_LIDAR_DISTANCE
    && _rightDistance < MAX_ULTRASONIC_DISTANCE
    && _leftDistance >= MAX_ULTRASONIC_DISTANCE) {
    _frontDistance = .75 * GARAGE_LENGTH; // 25% of garage length
  }

  if(_frontDistance >= MAX_LIDAR_DISTANCE
    && _leftDistance < MAX_ULTRASONIC_DISTANCE
    && _rightDistance < MAX_ULTRASONIC_DISTANCE) { // Corrected logic: both sides should see something
    _frontDistance = .35 * GARAGE_LENGTH; // 65% of garage length
  }

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