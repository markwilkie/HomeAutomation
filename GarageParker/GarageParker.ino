/*
 * GarageParker
 * 
 * An Arduino project to assist with parking in a garage
 * Using WS2812B LED strip as a 16x16 matrix for car visualization, 
 * and two HC-SR04 ultrasonic sensors for distance and angle calculation
 * 
 * Created: April 14, 2025
 * Updated: April 17, 2025 - Migrated from Adafruit NeoPixel to FastLED library
 */

#include "include/DistanceSensor.h"
#include "include/LedController.h"

// Constants
const float optimalSidePerc = .22; //perc off wall
const float plusMinusSidePerc = .03; // +/- perc off wall
const float optimalFrontPerc = .18;   
const float plusMinusFrontPerc = .05;

// Class instances
DistanceSensor distanceSensor;
LedController ledController;

// Display update timing
unsigned long lastDisplayTime = 0;
unsigned long carFirstDetectedTime = 0; // Time when car was first detected
bool carWasInGarage = false;            // Track if car was in garage in the previous loop
const unsigned long screenTimeout = 120000; // 2 minutes in milliseconds

void setup() {
  // Initialize serial communication
  Serial.begin(9600);
  
  // Initialize components
  distanceSensor.init();
  ledController.init();
  
  Serial.println("GarageParker system initializing...");
  Serial.println("Using two HC-SR04 ultrasonic sensors for distance measurement");
  Serial.println("Using WS2812B LED strip as 16x16 matrix for car visualization");
  Serial.println("Running with FastLED library for improved performance");
}

SidePositionStatus getSidePosition() { // Return type changed to enum
  float sidePerc = distanceSensor.getSidePercent();

  float maxSidePerc = optimalSidePerc + plusMinusSidePerc; // Maximum side percentage
  float minSidePerc = optimalSidePerc - plusMinusSidePerc; // Minimum side percentage
  
  // Check if the car is too close to the left wall
  if (sidePerc > maxSidePerc) {
    return SidePositionStatus::LEFT; // Return enum value
  }
  
  // Check if the car is too close to the right wall
  if (sidePerc < minSidePerc) {
    return SidePositionStatus::RIGHT; // Return enum value
  }
  
  //we're centered!
  return SidePositionStatus::CENTER; // Return enum value
}

FrontPositionStatus getFrontPosition() { // Return type changed to enum
  float frontPerc = distanceSensor.getFrontPercent();

  float maxFrontPerc = optimalFrontPerc + plusMinusFrontPerc; // Maximum front percentage
  float minFrontPerc = optimalFrontPerc - plusMinusFrontPerc; // Minimum front percentage
  
  // Check if the car is too close to the front wall
  if (frontPerc < minFrontPerc) {
    return FrontPositionStatus::TOO_CLOSE; // Return enum value
  }
  
  // Check if the car is too far from the front wall
  if (frontPerc > maxFrontPerc) {
    return FrontPositionStatus::TOO_FAR; // Return enum value
  }
  
  //we're centered!
  return FrontPositionStatus::OPTIMAL; // Return enum value
}

void loop() {
  // Read all sensor values
  distanceSensor.readAllSensors();

  bool carIsInGarage = distanceSensor.isCarInGarage();
  unsigned long currentTime = millis();

  // Check if the car is in the garage
  if (carIsInGarage) {
    // Check if the car just entered the garage
    if (!carWasInGarage) {
      carFirstDetectedTime = currentTime; // Record the time the car entered
      carWasInGarage = true;
      Serial.println("Car detected in garage.");
    }

    // Check if the screen timeout has been reached
    if (currentTime - carFirstDetectedTime > screenTimeout) {
      ledController.clearScreen(); // Turn off screen after timeout
    } else {
      showCarPosition(); // Show position if within timeout
    }
  } 
  else {
    // Car is not in the garage
    if (carWasInGarage) {
      Serial.println("Car left the garage.");
      ledController.clearScreen(); // Clear the screen immediately when car leaves
      carWasInGarage = false;      // Reset the state
      carFirstDetectedTime = 0;    // Reset the timer
    }
    // Keep screen clear if car remains out
    ledController.clearScreen(); 
  }

  delay(100); // Adjusted delay slightly
}

void showCarPosition() {
  
  // Get distances from each sensor
  float sidePerc = distanceSensor.getSidePercent();
  float frontPerc = distanceSensor.getFrontPercent();
   
  // Get side-to-side position
  SidePositionStatus sideStatus = getSidePosition(); // Variable type changed
  FrontPositionStatus frontStatus = getFrontPosition(); // Variable type changed
  
  // Display sensor readings every 500ms
  unsigned long currentTime = millis();
  if (currentTime - lastDisplayTime >= 500) {
    lastDisplayTime = currentTime;
    
    // Print distances
    Serial.print("Front: ");
    Serial.print(frontPerc);
    Serial.print(" % | Side: ");
    Serial.print(sidePerc);
    Serial.print(" % | Position: ");
    
    switch(sideStatus) { // Use enum variable
      case SidePositionStatus::LEFT:
        Serial.print("TOO FAR LEFT | ");
        break;
      case SidePositionStatus::CENTER:
        Serial.print("CENTERED LEFT/RIGHT | ");
        break;
      case SidePositionStatus::RIGHT:
        Serial.print("TOO FAR RIGHT | ");
        break;
    }

    switch(frontStatus) { // Use enum variable
      case FrontPositionStatus::TOO_CLOSE:
        Serial.println("TOO CLOSE TO FRONT WALL");
        break;
      case FrontPositionStatus::OPTIMAL:
        Serial.println("CENTERED IN GARAGE");
        break;
      case FrontPositionStatus::TOO_FAR:
        Serial.println("TOO FAR FROM FRONT WALL");
        break;
    }
  }
  
  // Update car visualization on the LED matrix
  ledController.visualizeCar(sidePerc, frontPerc, optimalSidePerc, optimalFrontPerc, sideStatus, frontStatus); 

}