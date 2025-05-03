/*
 * GarageParker
 * 
 * An Arduino project to assist with parking in a garage
 * Using WS2812B LED strip as a 16x16 matrix for car visualization, 
 * and two HC-SR04 ultrasonic sensors for distance and angle calculation
 * 
 * Created: April 14, 2025
 * Updated: April 17, 2025 - Using Adafruit NeoPixel library
 * Updated: April 29, 2025 - Switched TF-Luna to SoftwareSerial
 */

#include "include/DistanceSensor.h"
#include "include/LedController.h"
#include <SoftwareSerial.h> // Include SoftwareSerial

// Constants
const float optimalSidePerc = .96;  //target is just right of center
const float plusMinusSidePerc = .15; // +/- perc off wall
const float optimalFrontPerc = 1.0; //target is centered in garage
const float plusMinusFrontPerc = .2;

// Define pins for SoftwareSerial communication with TF-Luna
#define TFLUNA_RX_PIN 10 // Choose appropriate pins for your board
#define TFLUNA_TX_PIN 11 // Choose appropriate pins for your board

// Create SoftwareSerial instance for TF-Luna
SoftwareSerial tflunaSerial(TFLUNA_RX_PIN, TFLUNA_TX_PIN);

// Class instances
DistanceSensor distanceSensor(tflunaSerial); // Pass the SoftwareSerial port for TF-Luna
LedController ledController;

// Display update timing
unsigned long lastDisplayTime = 0;
unsigned long carFirstDetectedTime = 0; // Time when car was first detected
bool carWasInGarage = false;            // Track if car was in garage in the previous loop
const unsigned long screenTimeout = 120000; // 2 minutes in milliseconds

void setup() {
  // Initialize serial communication for debugging
  Serial.begin(9600);
  // Note: TF-Luna serial port (SoftwareSerial) is initialized within distanceSensor.init() -> _tfluna.init()

  // Initialize components
  distanceSensor.init();
  ledController.init();
  
  Serial.println("GarageParker system initializing...");
  Serial.println("Using two HC-SR04 ultrasonic sensors for distance measurement");
  Serial.println("Using TF-Luna via SoftwareSerial for front distance"); // Update message
  Serial.println("Running with Adafruit NeoPixel library"); // Corrected library name
}

SidePositionStatus getSidePosition() { // Return type changed to enum
  float sidePerc = distanceSensor.getSidePercent(optimalSidePerc);

  //remember that getsidepercentage returns 1.0 for optimal already
  float maxSidePerc = 1.0 + plusMinusSidePerc; // Maximum side percentage
  float minSidePerc = 1.0 - plusMinusSidePerc; // Minimum side percentage
  
  // Check if the car is too close to the left wall
  if (sidePerc > maxSidePerc) {
    if(sidePerc > (maxSidePerc + plusMinusFrontPerc)) { 
      return SidePositionStatus::DANGEROUSLY_LEFT; // Return enum value
    }
    else {
      return SidePositionStatus::LEFT; // Return enum value
    }
  }
  
  // Check if the car is too close to the right wall
  if (sidePerc < minSidePerc) {
    if(sidePerc < (minSidePerc - plusMinusFrontPerc)) { 
      return SidePositionStatus::DANGEROUSLY_RIGHT; // Return enum value
    }
    else {
      return SidePositionStatus::RIGHT; // Return enum value
    }
  }
  
  //we're centered!
  return SidePositionStatus::CENTER; // Return enum value
}

FrontPositionStatus getFrontPosition() { // Return type changed to enum
  float frontPerc = distanceSensor.getFrontPercent(optimalFrontPerc);

  //remember that getfrontpercentage returns 1.0 for optimal already
  float maxFrontPerc = 1.0 + plusMinusFrontPerc; // Maximum front percentage
  float minFrontPerc = 1.0 - plusMinusFrontPerc; // Minimum front percentage
  
  // Check if the car is too close to the front wall
  if (frontPerc < minFrontPerc) {
    if(frontPerc < (minFrontPerc - plusMinusFrontPerc)) { 
      return FrontPositionStatus::DANGEROUSLY_CLOSE; // Return enum value
    }
    else {
      return FrontPositionStatus::TOO_CLOSE; // Return enum value
    }
  }
  
  // Check if the car is too far from the front wall
  if (frontPerc > maxFrontPerc) {
    return FrontPositionStatus::TOO_FAR; // Return enum value
  }
  
  //we're centered!
  return FrontPositionStatus::OPTIMAL; // Return enum value
}

void loop() {
  // Read all sensor values and update occupancy state
  distanceSensor.readAllSensors();

  // Use the new method to check if the garage is confirmed empty
  bool garageIsEmpty = distanceSensor.isGarageEmpty();
  unsigned long currentTime = millis();

  // Check if the garage is NOT empty (car is present or potentially present)
  if (!garageIsEmpty) {
    // Check if the car just entered the garage (or was detected again)
    if (!carWasInGarage) {
      carFirstDetectedTime = currentTime; // Record the time the car entered/detected
      carWasInGarage = true;
      Serial.println("Car detected in garage.");
    }

    // Check if the screen timeout has been reached since first detection
    if (currentTime - carFirstDetectedTime > screenTimeout) {
      ledController.clearScreen(); // Turn off screen after timeout
      // Optional: Add a serial print indicating timeout
      // if (carWasInGarage) { // Print only once when timeout occurs
      //   Serial.println("Screen timed out.");
      // }
    } else {
      showCarPosition(); // Show position if within timeout
    }
  } 
  else {
    // Garage is confirmed empty
    if (carWasInGarage) {
      Serial.println("Garage confirmed empty."); // Updated message
      ledController.clearScreen(); // Clear the screen immediately when confirmed empty
      carWasInGarage = false;      // Reset the state
      carFirstDetectedTime = 0;    // Reset the timer
    }
    // Keep screen clear if garage remains empty
    ledController.clearScreen(); 
  }

  delay(100); // Adjusted delay slightly
}

void showCarPosition() {
  
  // Get distances from each sensor
  float sidePerc = distanceSensor.getSidePercent(optimalSidePerc);
  float frontPerc = distanceSensor.getFrontPercent(optimalFrontPerc); // This now uses TF-Luna data implicitly
   
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
      case SidePositionStatus::DANGEROUSLY_LEFT:
        Serial.print("DANGEROUSLY LEFT | ");
        break;
      case SidePositionStatus::LEFT:
        Serial.print("TOO FAR LEFT | ");
        break;
      case SidePositionStatus::CENTER:
        Serial.print("CENTERED LEFT/RIGHT | ");
        break;
      case SidePositionStatus::RIGHT:
        Serial.print("TOO FAR RIGHT | ");
        break;
      case SidePositionStatus::DANGEROUSLY_RIGHT:
        Serial.print("DANGEROUSLY RIGHT | ");
        break;
    }

    switch(frontStatus) { // Use enum variable
      case FrontPositionStatus::DANGEROUSLY_CLOSE:
        Serial.println("DANGEROUSLY CLOSE TO FRONT WALL");
        break;
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
  
  // Update car visualization on the LED matrix, passing plusMinusSidePerc
  ledController.visualizeCar(sidePerc, frontPerc, sideStatus, frontStatus); 
  ledController.show(); // Add show() call here to update the physical LEDs

}