/*
 * FrontSensorTest
 * 
 * An Arduino project to test the TF-Luna front distance sensor.
 * 
 * Created: April 30, 2025
 */

#include "include/DistanceSensor.h"
#include <SoftwareSerial.h> // Include SoftwareSerial

const float optimalFrontPerc = .18;   
const float plusMinusFrontPerc = .05;

// Define pins for SoftwareSerial communication with TF-Luna
#define TFLUNA_RX_PIN 10 // Choose appropriate pins for your board
#define TFLUNA_TX_PIN 11 // Choose appropriate pins for your board

// Create SoftwareSerial instance for TF-Luna
SoftwareSerial tflunaSerial(TFLUNA_RX_PIN, TFLUNA_TX_PIN);

// Class instances
DistanceSensor distanceSensor(tflunaSerial); // Pass the SoftwareSerial port for TF-Luna

void setup() {
  // Initialize serial communication for debugging
  Serial.begin(9600);
  // Note: TF-Luna serial port (SoftwareSerial) is initialized within distanceSensor.init() -> _tfluna.init()

  // Initialize components
  distanceSensor.init();
  
  Serial.println("Front Sensor Test Initializing...");
  Serial.println("Using TF-Luna via SoftwareSerial for front distance");
}

void loop() {
  // Read sensor value
  distanceSensor.readSensor(); // Use the simplified read method

  int frontDistanceCm = distanceSensor.readFrontLidarCm(); // Get the raw distance
  float frontPerc = distanceSensor.getFrontPercent(); // Get the percentage

  // Print the distance reading
  Serial.print("Front Distance: ");
  Serial.print(frontDistanceCm);
  Serial.print(" cm (");
  Serial.print(frontPerc * 100);
  Serial.println(" %)");

  delay(100); // Delay between readings
}