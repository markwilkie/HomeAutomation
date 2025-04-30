#include "../include/DistanceSensor.h"
#include <Arduino.h>
#include <math.h>
#include "../include/TFLunaUART.h" // Ensure new header is included
#include <SoftwareSerial.h> // Include SoftwareSerial

DistanceSensor::DistanceSensor(SoftwareSerial& lidarSerial) : _tfluna(lidarSerial) {}

void DistanceSensor::init() {
  _frontDistance = MAX_LIDAR_DISTANCE;

  // Initialize TF-Luna Sensor using the new class with SoftwareSerial
  _tfluna.init(); // Default baud rate is 115200
  Serial.println(F("TF-Luna SoftwareSerial Initialized")); // Update message
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

void DistanceSensor::readSensor() {
  // Read sensors sequentially
  readFrontLidarCm();
}