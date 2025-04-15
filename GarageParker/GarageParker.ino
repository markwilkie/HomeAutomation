/*
 * GarageParker
 * 
 * An Arduino project to assist with parking in a garage
 * Using WS2812B LED strip as a 16x16 matrix for car visualization, 
 * Sharp GP2Y0A21YK0F IR sensor for middle distance,
 * and two HC-SR04 ultrasonic sensors for side distance and angle calculation
 * 
 * Created: April 14, 2025
 */

#include "include/DistanceSensor.h"
#include "include/LedController.h"

// Pin definitions
// IR sensor in the middle
const int irSensorPin = A0;  // Analog pin for Sharp IR sensor

// Ultrasonic sensors (left and right)
const int leftTrigPin = 9;   // Left ultrasonic trigger pin
const int leftEchoPin = 10;  // Left ultrasonic echo pin
const int rightTrigPin = 11; // Right ultrasonic trigger pin
const int rightEchoPin = 12; // Right ultrasonic echo pin

// LED strip
const int ledDataPin = 6;    // WS2812B data pin

// Constants
const int numLeds = 256;      // 16x16 matrix = 256 LEDs
const int matrixWidth = 16;   // 16 LEDs wide
const int matrixHeight = 16;  // 16 LEDs high
const float sensorSpacing = 100.0; // Distance between left and right sensors in cm

// Angle thresholds
const float angleThreshold = 5.0;  // Angle (in degrees) threshold for "straight"
const float maxAngle = 30.0;      // Maximum expected angle in degrees

// Class instances
DistanceSensor distanceSensor(irSensorPin,
                              leftTrigPin, leftEchoPin,
                              rightTrigPin, rightEchoPin,
                              sensorSpacing);
LedController ledController(ledDataPin, numLeds, 100, 50);

// Display update timing
unsigned long lastDisplayTime = 0;

void setup() {
  // Initialize serial communication
  Serial.begin(9600);
  
  // Initialize components
  distanceSensor.init();
  ledController.init();
  
  // Configure LED matrix
  ledController.setupMatrix(matrixWidth, matrixHeight);
  
  Serial.println("GarageParker system initializing...");
  Serial.println("Using Sharp GP2Y0A21YK0F IR sensor in the middle");
  Serial.println("Using HC-SR04 ultrasonic sensors on left and right");
  Serial.println("Using WS2812B LED strip as 16x16 matrix for car visualization");
}

void loop() {
  // Read all sensor values
  distanceSensor.readAllSensors();
  
  // Get distances from each sensor
  long middleDistance = distanceSensor.getIRDistance();
  long leftDistance = distanceSensor.getLeftDistance();
  long rightDistance = distanceSensor.getRightDistance();
  
  // Calculate car angle (in degrees)
  float carAngle = distanceSensor.calculateCarAngle();
  
  // Get side-to-side position (-1 = left, 0 = center, 1 = right)
  int sidePosition = distanceSensor.getSidePosition();
  
  // Display sensor readings every 500ms
  unsigned long currentTime = millis();
  if (currentTime - lastDisplayTime >= 500) {
    lastDisplayTime = currentTime;
    
    // Print distances
    Serial.print("Left: ");
    Serial.print(leftDistance);
    Serial.print(" cm | Middle: ");
    Serial.print(middleDistance);
    Serial.print(" cm | Right: ");
    Serial.print(rightDistance);
    Serial.print(" cm | Angle: ");
    Serial.print(carAngle);
    Serial.print(" degrees | Position: ");
    
    switch(sidePosition) {
      case -1:
        Serial.println("LEFT");
        break;
      case 0:
        Serial.println("CENTER");
        break;
      case 1:
        Serial.println("RIGHT");
        break;
    }
  }
  
  // Update car visualization on the LED matrix
  ledController.visualizeCar(carAngle, sidePosition, middleDistance);
  
  delay(50); // Short delay for smoother visual feedback
}