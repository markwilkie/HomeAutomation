#include "../include/LedController.h"
#include <Arduino.h>
#include <math.h> // Include for fabs()

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
  _colorGrey = _strip->Color(25, 25, 15);    // Grey interior
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

void LedController::visualizeCar(float sidePerc, float frontPerc, SidePositionStatus sideStatus, FrontPositionStatus frontStatus) {
  if (!_strip) return;

  // Clear all pixels first or fill with background color
  if (sideStatus == SidePositionStatus::CENTER && frontStatus == FrontPositionStatus::OPTIMAL) {
    _strip->fill(_colorGreen);
  } else {
    _strip->fill(_colorOff); // Use fill for solid color (clear)
  }

  // Draw optimal front target line, any arrows, and center line for orientation
  drawOptimalFrontTargetLine(frontStatus); // Pass frontStatus
  drawCenterLine(sideStatus);
  drawSideIndicators(sideStatus);

  //get ready to calc y
  //int targetFrontY = (_matrixHeight/2)-(CAR_HEIGHT/2);  //where front ends up

  //100% is centered, 0 is all the way to the right, 200 is all the way left
  int carX = _matrixWidth - (sidePerc * (_matrixWidth/2));
  int carY = frontPerc * ((_matrixHeight/2)-(CAR_HEIGHT/2)); // Calculate car's y position

  Serial.print(F("Car X: "));
  Serial.print(carX); 
  Serial.print(F(", Car Y: "));
  Serial.println(carY);

  // Draw and fill the car rectangle
  drawRect(carX, carY, CAR_WIDTH, CAR_HEIGHT, _colorCar); // Draw outline
  fillRect(carX+1, carY+1, CAR_WIDTH-2, CAR_HEIGHT-2, _colorGrey);
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

// Implementation of Bresenham's line algorithm or similar
void LedController::drawLine(int x0, int y0, int x1, int y1, uint32_t color) {
  if (!_strip) return;

  // Handle simple cases: horizontal and vertical lines
  if (x0 == x1) { // Vertical line
    int startY = min(y0, y1);
    int endY = max(y0, y1);
    for (int y = startY; y <= endY; ++y) {
      setPixelSafe(x0, y, color);
    }
    return;
  }
  if (y0 == y1) { // Horizontal line
    int startX = min(x0, x1);
    int endX = max(x0, x1);
    for (int x = startX; x <= endX; ++x) {
      setPixelSafe(x, y0, color);
    }
    return;
  }

  // Bresenham's line algorithm for other cases (optional, can be added if needed)
  // For now, only horizontal and vertical lines are implemented as they are used.
  // If diagonal lines are needed, implement Bresenham's algorithm here.
  // Example: Simple diagonal (slope 1 or -1) - adapt if needed
  int dx = abs(x1 - x0);
  int dy = abs(y1 - y0);
  if (dx == dy) { // Simple diagonal case
      int sx = x0 < x1 ? 1 : -1;
      int sy = y0 < y1 ? 1 : -1;
      int x = x0;
      int y = y0;
      while (true) {
          setPixelSafe(x, y, color);
          if (x == x1 && y == y1) break;
          x += sx;
          y += sy;
      }
  }
  // Add full Bresenham's implementation here if general lines are required.
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

// Add helper function to set pixel color safely with bounds checking
void LedController::setPixelSafe(int x, int y, uint32_t color) {
  if (x >= 0 && x < _matrixWidth && y >= 0 && y < _matrixHeight) {
    int pixelIndex = getPixelIndex(x, y);
    if (pixelIndex != -1) {
      _strip->setPixelColor(pixelIndex, color);
    }
  }
}

// Add new function to draw side indicators
void LedController::drawSideIndicators(SidePositionStatus sideStatus) {
  if (!_strip) return;

  int row1 = _matrixHeight / 3;
  int row2 = _matrixHeight * 2 / 3;

  if (sideStatus == SidePositionStatus::RIGHT) { // Need to move LEFT ('<')
    int x = 0; // Left edge
    drawArrow(x, row1, 2, _colorYellow); // Draw arrow pointing left
    drawArrow(x, row2, 2, _colorYellow); // Draw arrow pointing left
  } else if (sideStatus == SidePositionStatus::LEFT) { // Need to move RIGHT ('>')
    int x = _matrixWidth - 1; // Right edge
    drawArrow(x, row1, 3, _colorYellow); // Draw arrow pointing right 
    drawArrow(x, row2, 3, _colorYellow); // Draw arrow pointing right
  } else if (sideStatus == SidePositionStatus::DANGEROUSLY_LEFT) { // Need to move RIGHT ('>')
    int x = _matrixWidth - 1; // Right edge
    drawArrow(x, row1, 3, _colorRed); // Draw arrow pointing right 
    drawArrow(x, row2, 3, _colorRed); // Draw arrow pointing right
  } else if (sideStatus == SidePositionStatus::DANGEROUSLY_RIGHT) { // Need to move LEFT ('<')
    int x = 0; // Left edge
    drawArrow(x, row1, 2, _colorRed); // Draw arrow pointing left
    drawArrow(x, row2, 2, _colorRed); // Draw arrow pointing left
  }
  // If status is CENTER, no indicators are drawn
}

void LedController::drawArrow(int x, int y, int direction, uint32_t color) {
  if (!_strip) return;
  // Draw a simple arrow shape based on direction
  if (direction == 0) { // Up
    setPixelSafe(x, y - 1, color);
    setPixelSafe(x - 1, y, color);
    setPixelSafe(x + 1, y, color);
    setPixelSafe(x, y + 1, color);
  } else if (direction == 1) { // Down
    setPixelSafe(x, y + 1, color);
    setPixelSafe(x - 1, y, color);
    setPixelSafe(x + 1, y, color);
    setPixelSafe(x, y - 1, color);
  } else if (direction == 2) { // Left
    setPixelSafe(x + 1, y - 1, color);
    setPixelSafe(x, y, color);
    setPixelSafe(x + 1, y + 1, color);
  } else if (direction == 3) { // Right
    setPixelSafe(x-1, y - 1, color);
    setPixelSafe(x, y, color);
    setPixelSafe(x-1, y + 1, color);
  }
}

// Add new function to draw the center line
void LedController::drawCenterLine(SidePositionStatus sideStatus) {
  if (!_strip) return;

  uint32_t color = _colorGrey; // Default color for center line

  if(sideStatus == SidePositionStatus::DANGEROUSLY_LEFT || sideStatus == SidePositionStatus::DANGEROUSLY_RIGHT) {
    color = _colorRed; // Change color to red for dangerous status); 
  }
  if(sideStatus == SidePositionStatus::LEFT || sideStatus == SidePositionStatus::RIGHT) {
    color = _colorYellow; 
  }

  drawLine(_matrixWidth / 2, 0, _matrixWidth / 2, _matrixHeight - 1, color); // Draw center line
  drawLine((_matrixWidth / 2) - 1, 0, (_matrixWidth / 2) - 1, _matrixHeight - 1, color); // Draw left line
}

// Add new function to draw the optimal front target line
void LedController::drawOptimalFrontTargetLine(FrontPositionStatus frontStatus) { // Add frontStatus parameter
  if (!_strip) return;

  uint32_t color = _colorGrey; // Default color for optimal front target line
  if(frontStatus == FrontPositionStatus::DANGEROUSLY_CLOSE) {
    color = _colorRed; // Change color to red for dangerous status
  }

  // Map optimalFrontPerc (0 to 1) to matrix rows (0 to _matrixHeight - 1)
  // This line represents where the *front* of the car should ideally be.
  int targetFrontY = (_matrixHeight/2)-(CAR_HEIGHT/2); // Centered vertically in the matrix)
  drawLine(0, targetFrontY, _matrixWidth - 1, targetFrontY, color); // Draw horizontal line
}
