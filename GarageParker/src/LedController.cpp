#include "../include/LedController.h"
#include <Arduino.h>

// Constructor now takes pin and dimensions
LedController::LedController() {
  _dataPin = LED_DATA_PIN;
  _matrixWidth = LED_MATRIX_WIDTH;
  _matrixHeight = LED_MATRIX_HEIGHT;
  _numLeds = _matrixWidth * _matrixHeight;

  // Allocate Adafruit_NeoPixel object
  // Parameter 3: Pixel type flags, add NEO_KHZ800 for WS2812
  _strip = new Adafruit_NeoPixel(_numLeds, _dataPin, NEO_GRB + NEO_KHZ800);

  // Define colors using the strip's Color method
  _colorGreen = _strip->Color(0, 255, 0);    // Green
  _colorYellow = _strip->Color(255, 255, 0); // Yellow
  _colorRed = _strip->Color(255, 0, 0);      // Red
  _colorOff = _strip->Color(0, 0, 0);        // Off
  _colorCar = _strip->Color(255, 255, 255);  // White for car
  _colorGrey = _strip->Color(25, 25, 30);    // Grey interior
  _colorOptimal = _strip->Color(0, 100, 255); // Light blue for optimal position (adjust if needed)
}

LedController::~LedController() {
  // Free allocated memory
  if (_strip) {
    delete _strip;
    _strip = nullptr;
  }
}

void LedController::init(uint8_t brightness) {
  if (!_strip) return;
  _strip->begin(); // Initialize NeoPixel library
  _strip->setBrightness(brightness); // Set initial brightness
  _strip->clear(); // Initialize all pixels to 'off'
  _strip->show(); // Update the strip
}

void LedController::clearScreen() {
  if (!_strip) return;
  _strip->clear();
  _strip->show();
}

void LedController::setBrightness(uint8_t brightness) {
    if (!_strip) return;
    _strip->setBrightness(brightness);
    _strip->show(); // Show brightness change immediately
}

void LedController::show() {
    if (!_strip) return;
    _strip->show();
}

void LedController::visualizeCar(float sidePerc, float frontPerc, float optimalSidePerc, float optimalFrontPerc, SidePositionStatus sideStatus, FrontPositionStatus frontStatus) {
  if (!_strip) return;

  int carWidth = 3; // Width of the car in pixels
  int carHeight = 10; // Height of the car in pixels

  // Clear all pixels first or fill with background color
  if (sideStatus == SidePositionStatus::CENTER && frontStatus == FrontPositionStatus::OPTIMAL) {
    _strip->fill(_colorGreen); // Use fill for solid color
  } else {
    _strip->fill(_colorOff); // Use fill for solid color (clear)
  }

  // Calculate x and y based on percentages
  // Ensure division by zero doesn't happen
  float vizSidePixels = (optimalSidePerc > 0) ? ((_matrixWidth / 2.0f) / optimalSidePerc) : 0;
  int rightPixels = round(sidePerc * vizSidePixels); // Right side percentage
  int carX = _matrixWidth - (carWidth / 2) - rightPixels; // X position based on side percentage

  //float vizFrontPixels = (optimalFrontPerc > 0) ? ((_matrixHeight / 2.0f) / optimalFrontPerc) : 0;
  //int frontPixels = round(frontPerc * vizFrontPixels);
  //int frontPixels = round(frontPerc * _matrixHeight); // Front percentage
  //int carY = frontPixels - (carHeight / 2); // Y position based on front percentage
  int carY = round(frontPerc * _matrixHeight); // Y position based on front percentage

  Serial.print("Car y: ");
  Serial.println(carY); 

  // Draw the car (only if within bounds, adjust fillRect/drawRect to handle clipping if needed)
  // Basic bounds check for the starting corner
  if (carX >= 0 && carX < _matrixWidth && carY >= 0 && carY < _matrixHeight) {
      drawRect(carX, carY, carWidth, carHeight, _colorCar); // Car body outline
      fillRect(carX + 1, carY + 1, carWidth - 2, carHeight - 2, _colorGrey); // Car interior (adjust width/height for fill)
  }

  // Draw indicators
  if (frontStatus == FrontPositionStatus::TOO_CLOSE) {
    // Draw red line above the car
    fillRect(carX, carY - 1, carWidth, 1, _colorRed);
  }
  // No indicator for OPTIMAL or TOO_FAR front status in this logic

  if (sideStatus == SidePositionStatus::LEFT) {
    // Draw yellow line to the left of the car
    fillRect(carX - 1, carY, 1, carHeight, _colorYellow);
  } else if (sideStatus == SidePositionStatus::RIGHT) {
    // Draw yellow line to the right of the car
    fillRect(carX + carWidth, carY, 1, carHeight, _colorYellow);
  }
  // No indicator for CENTER side status

  _strip->show(); // Update the display
}

int LedController::getPixelIndex(int x, int y) {
  // Convert x,y coordinates to pixel index
  // Assumes a zigzag layout typical of LED matrices
  // Even columns run top to bottom, odd columns bottom to top

  // Validate coordinates are within bounds
  if (x < 0 || x >= _matrixWidth || y < 0 || y >= _matrixHeight) {
    return -1; // Out of bounds
  }

  int index;
  if (x % 2 == 0) {
    // Even columns (0, 2, 4...) run top to bottom
    index = x * _matrixHeight + y;
  } else {
    // Odd columns (1, 3, 5...) run bottom to top (reversed)
    index = x * _matrixHeight + (_matrixHeight - 1 - y);
  }

  // Make sure index is within valid range (should be guaranteed by x,y checks)
  if (index >= _numLeds) {
    return -1;
  }

  return index;
}

// Draw rectangle outline
void LedController::drawRect(int x, int y, int width, int height, uint32_t color) {
  if (!_strip) return;
  // Draw horizontal lines
  for (int i = 0; i < width; i++) {
    int topPixelIndex = getPixelIndex(x + i, y);
    int bottomPixelIndex = getPixelIndex(x + i, y + height - 1);
    if (topPixelIndex != -1) _strip->setPixelColor(topPixelIndex, color);
    if (bottomPixelIndex != -1) _strip->setPixelColor(bottomPixelIndex, color);
  }
  // Draw vertical lines (avoiding corners already drawn)
  for (int i = 1; i < height - 1; i++) {
    int leftPixelIndex = getPixelIndex(x, y + i);
    int rightPixelIndex = getPixelIndex(x + width - 1, y + i);
     if (leftPixelIndex != -1) _strip->setPixelColor(leftPixelIndex, color);
     if (rightPixelIndex != -1) _strip->setPixelColor(rightPixelIndex, color);
  }
}

// Fill rectangle solid
void LedController::fillRect(int x, int y, int width, int height, uint32_t color) {
  if (!_strip) return;
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width; i++) {
      int pixelIndex = getPixelIndex(x + i, y + j);
      if (pixelIndex != -1) { // Check if index is valid
        _strip->setPixelColor(pixelIndex, color);
      }
    }
  }
}

void LedController::setAllPixels(uint32_t color) {
  if (!_strip) return;
  _strip->fill(color); // Use Adafruit's fill method
  _strip->show();
}
