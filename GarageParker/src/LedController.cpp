#include "../include/LedController.h"
#include <Arduino.h>

#define DATA_PIN 3
#define MATRIX_WIDTH 16
#define MATRIX_HEIGHT 16

LedController::LedController(int safeDistance, int warningDistance) {
  _dataPin = DATA_PIN;
  _numLeds = MATRIX_WIDTH*MATRIX_HEIGHT;
 
  // Allocate memory for the LED array
  _leds = new CRGB[_numLeds];
  
  // Define colors (RGB format)
  _colorGreen = CRGB(0, 255, 0);    // Green
  _colorYellow = CRGB(255, 255, 0); // Yellow
  _colorRed = CRGB(255, 0, 0);      // Red
  _colorOff = CRGB(0, 0, 0);        // Off
  _colorCar = CRGB(255, 255, 255);  // White for car
  _colorGrey = CRGB(25, 25, 30); // Grey interior
  _colorOptimal = CRGB(0, 100, 255); // Light blue for optimal position
}

LedController::~LedController() {
  // Free allocated memory
  delete[] _leds;
}

void LedController::init() {


  FastLED.addLeds<WS2811, DATA_PIN, GRB>(_leds, _numLeds);
  FastLED.clear();
  
  // Set initial brightness (0-255)
  FastLED.setBrightness(150);
  
  // Turn all pixels off initially
  setAllPixels(_colorOff);
}

void LedController::clearScreen() {
  fill_solid(_leds, _numLeds, _colorOff);
  FastLED.show();
}

void LedController::visualizeCar(float sidePerc, float frontPerc, SidePositionStatus sideStatus, FrontPositionStatus frontStatus) {
  
  int carWidth = 3; // Width of the car in pixels
  int carHeight = 7; // Height of the car in pixels
  
  // Clear all pixels first
  if(sideStatus == SidePositionStatus::CENTER
    && frontStatus == FrontPositionStatus::OPTIMAL) {
    fill_solid(_leds, _numLeds, _colorGreen); 
  } else {
  fill_solid(_leds, _numLeds, _colorOff);
  }

  //calc x and y based on percentages
  int carX = (int)(MATRIX_WIDTH-(sidePerc / 100.0) * (MATRIX_WIDTH - 1)); // X position based on side percentage
  int carY = (int)(frontPerc / 100.0 * (MATRIX_HEIGHT - 1)); // Y position based on front percentage
  
  // Draw the car in the center of the matrix
  drawRect(carX, carY, carWidth, carHeight, _colorCar); // Car body
  fillRect(carX+1, carY+1, carWidth-1, carHeight-1, _colorGrey); // Car interior


  // Example of using the enums (replace with actual logic)
  CRGB frontIndicatorColor = _colorOff;
  if (frontStatus == FrontPositionStatus::TOO_CLOSE) {
    frontIndicatorColor = _colorRed;
  } else if (frontStatus == FrontPositionStatus::OPTIMAL) {
    frontIndicatorColor = _colorGreen;
  } else { // TOO_FAR
    frontIndicatorColor = _colorOff;
  }
  // Draw front indicator (e.g., a line at the front)
  drawRect(carX, carY - 1, carWidth, 0, frontIndicatorColor);

  CRGB sideIndicatorColor = _colorOff;
   if (sideStatus == SidePositionStatus::LEFT) {
    sideIndicatorColor = _colorYellow; 
    drawRect(carX, carY, 0, carHeight, sideIndicatorColor);
  } else if (sideStatus == SidePositionStatus::RIGHT) {
    sideIndicatorColor = _colorYellow;
    drawRect(carX + carWidth, carY, 0, carHeight, sideIndicatorColor);
  }
  // If sideStatus is CENTER, no side indicator is drawn (or use a specific color)

  FastLED.show();
}


int LedController::getPixelIndex(int x, int y) {
  // Convert x,y coordinates to pixel index
  // Assumes a zigzag layout typical of LED matrices
  // Even rows run left to right, odd rows right to left
  
  // Validate coordinates are within bounds
  if (x < 0 || x >= MATRIX_WIDTH || y < 0 || y >= MATRIX_HEIGHT) {
    return -1; // Out of bounds
  }
  
  int index;
  if (x % 2 == 0) {
    // Even columns (0, 2, 4...) run top to bottom
    index = x * MATRIX_HEIGHT + y;
  } else {
    // Odd columns (1, 3, 5...) run bottom to top (reversed)
    index = x * MATRIX_HEIGHT + (MATRIX_HEIGHT-y-1);
  }
  
  // Make sure index is within valid range
  if (index >= _numLeds) {
    return -1;
  }
  
  return index;
}

void LedController::drawRect(int x, int y, int width, int height, CRGB color) {
  // Draw the vertical lines
  for (int i = y; i <= y + height; i++) {
    // left line
    int pixelIndex = getPixelIndex(x, i);
    if (pixelIndex >= 0) {
      _leds[pixelIndex] = color;
    }
    
    // right line
    pixelIndex = getPixelIndex(x+width, i);
    if (pixelIndex >= 0) {
      _leds[pixelIndex] = color;
    }
  }
  
  // Draw the horizontal lines
  for (int j = x; j <= x + width; j++) {
    // top line
    int pixelIndex = getPixelIndex(j, y);
    if (pixelIndex >= 0) {
      _leds[pixelIndex] = color;
    }
    
    // bottom line
    pixelIndex = getPixelIndex(j, y+height);
    if (pixelIndex >= 0) {
      _leds[pixelIndex] = color;
    }
  }
}

void LedController::fillRect(int x, int y, int width, int height, CRGB color) {
  for (int j = y; j < y + height; j++) {
    for (int i = x; i < x + width; i++) {
      int pixelIndex = getPixelIndex(i, j);
      if (pixelIndex >= 0) {
        _leds[pixelIndex] = color;
      }
    }
  }
}

void LedController::setAllPixels(CRGB color) {
  fill_solid(_leds, _numLeds, color);
  FastLED.show();
}
