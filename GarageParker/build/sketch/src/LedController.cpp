#line 1 "C:\\Users\\mark\\MyProjects\\GitHub\\GarageParker\\src\\LedController.cpp"
#include "../include/LedController.h"
#include <Arduino.h>

LedController::LedController(int dataPin, int numLeds, int safeDistance, int warningDistance) : 
  _pixels(numLeds, dataPin, NEO_GRB + NEO_KHZ800) {
  _dataPin = dataPin;
  _numLeds = numLeds;
  _safeDistance = safeDistance;
  _warningDistance = warningDistance;
  
  // Default matrix dimensions
  _matrixWidth = 10;
  _matrixHeight = 4;
  
  // Define colors (RGB format)
  _colorGreen = _pixels.Color(0, 255, 0);    // Green
  _colorYellow = _pixels.Color(255, 255, 0); // Yellow
  _colorRed = _pixels.Color(255, 0, 0);      // Red
  _colorOff = _pixels.Color(0, 0, 0);        // Off
  _colorCar = _pixels.Color(255, 255, 255);  // White for car
  _colorOptimal = _pixels.Color(0, 100, 255); // Light blue for optimal position
}

void LedController::init() {
  _pixels.begin();
  setAllPixels(_colorOff); // Turn all pixels off initially
}

void LedController::setupMatrix(int width, int height) {
  // Set up the matrix dimensions
  _matrixWidth = width;
  _matrixHeight = height;
  
  // Verify that the matrix size doesn't exceed the number of LEDs
  if (width * height > _numLeds) {
    // If it does, adjust to fit
    _matrixWidth = min(width, _numLeds / height);
    _matrixHeight = min(height, _numLeds / width);
  }
}

int LedController::getPixelIndex(int x, int y) {
  // Convert x,y coordinates to pixel index
  // Assumes a zigzag layout typical of LED matrices
  // Even rows run left to right, odd rows right to left
  
  // Validate coordinates are within bounds
  if (x < 0 || x >= _matrixWidth || y < 0 || y >= _matrixHeight) {
    return -1; // Out of bounds
  }
  
  int index;
  if (y % 2 == 0) {
    // Even rows (0, 2, 4...) run left to right
    index = y * _matrixWidth + x;
  } else {
    // Odd rows (1, 3, 5...) run right to left (reversed)
    index = y * _matrixWidth + (_matrixWidth - 1 - x);
  }
  
  // Make sure index is within valid range
  if (index >= _numLeds) {
    return -1;
  }
  
  return index;
}

void LedController::drawRect(int x, int y, int width, int height, uint32_t color) {
  // Draw the horizontal lines
  for (int i = x; i < x + width; i++) {
    // Top line
    int pixelIndex = getPixelIndex(i, y);
    if (pixelIndex >= 0) {
      _pixels.setPixelColor(pixelIndex, color);
    }
    
    // Bottom line
    pixelIndex = getPixelIndex(i, y + height - 1);
    if (pixelIndex >= 0) {
      _pixels.setPixelColor(pixelIndex, color);
    }
  }
  
  // Draw the vertical lines
  for (int j = y; j < y + height; j++) {
    // Left line
    int pixelIndex = getPixelIndex(x, j);
    if (pixelIndex >= 0) {
      _pixels.setPixelColor(pixelIndex, color);
    }
    
    // Right line
    pixelIndex = getPixelIndex(x + width - 1, j);
    if (pixelIndex >= 0) {
      _pixels.setPixelColor(pixelIndex, color);
    }
  }
}

void LedController::fillRect(int x, int y, int width, int height, uint32_t color) {
  for (int j = y; j < y + height; j++) {
    for (int i = x; i < x + width; i++) {
      int pixelIndex = getPixelIndex(i, j);
      if (pixelIndex >= 0) {
        _pixels.setPixelColor(pixelIndex, color);
      }
    }
  }
}

void LedController::updateBasedOnDistance(long distance) {
  // Set color based on distance
  uint32_t color;
  
  if (distance > _safeDistance) {
    // Safe distance - green light
    color = _colorGreen;
  } else if (distance > _warningDistance) {
    // Getting closer - yellow light
    color = _colorYellow;
  } else {
    // Too close - red light
    color = _colorRed;
  }
  
  // Show the color on all LEDs
  setAllPixels(color);
}

void LedController::showProgressBar(long distance, long maxDistance) {
  // Clear all pixels first
  setAllPixels(_colorOff);
  
  // Calculate how many LEDs to light up based on distance
  int ledsToLight = map(distance, 0, maxDistance, 0, _numLeds);
  ledsToLight = constrain(ledsToLight, 0, _numLeds);
  
  // Determine color based on distance
  uint32_t color;
  if (distance > _safeDistance) {
    color = _colorGreen;
  } else if (distance > _warningDistance) {
    color = _colorYellow;
  } else {
    color = _colorRed;
  }
  
  // Light up the appropriate number of LEDs
  for (int i = 0; i < ledsToLight; i++) {
    _pixels.setPixelColor(i, color);
  }
  
  _pixels.show();
}

void LedController::setAllPixels(uint32_t color) {
  for (int i = 0; i < _numLeds; i++) {
    _pixels.setPixelColor(i, color);
  }
  _pixels.show();
}

void LedController::visualizeCar(float angle, int sidePosition, float distance) {
  // Clear all pixels first
  setAllPixels(_colorOff);
  
  // Set background color based on position and angle
  uint32_t backgroundColor;
  if (abs(angle) < 5.0 && sidePosition == 0) {
    // Perfect position - optimal background
    backgroundColor = _colorOptimal;
  } else if (abs(angle) < 5.0) {
    // Straight but not centered - yellow background
    backgroundColor = _colorYellow;
  } else {
    // Angled - red background
    backgroundColor = _colorRed;
  }
  
  // Fill the background
  fillRect(0, 0, _matrixWidth, _matrixHeight, backgroundColor);
  
  // Draw "garage walls" on the top and bottom
  for (int i = 0; i < _matrixWidth; i++) {
    int topWall = getPixelIndex(i, 0);
    int bottomWall = getPixelIndex(i, _matrixHeight - 1);
    if (topWall >= 0) {
      _pixels.setPixelColor(topWall, _pixels.Color(120, 120, 120)); // Gray wall
    }
    if (bottomWall >= 0) {
      _pixels.setPixelColor(bottomWall, _pixels.Color(120, 120, 120)); // Gray wall
    }
  }
  
  // Calculate car position
  // Center car horizontally by default
  int carWidth = 10;  // Car width in pixels (increased for 16x16 matrix)
  int carHeight = 6;  // Car height in pixels (increased for 16x16 matrix)
  int carX = (_matrixWidth - carWidth) / 2;
  int carY = (_matrixHeight - carHeight) / 2;
  
  // Adjust for side position - move more on larger matrix
  if (sidePosition != 0) {
    carX += sidePosition * 2; // Move left or right based on sidePosition (-1 or 1)
  }
  
  // Constrain car position to be visible
  carX = constrain(carX, 0, _matrixWidth - carWidth);
  
  // Calculate car rotation based on angle
  int angleOffset = 0;
  if (abs(angle) >= 5.0) {
    // Map the angle to an offset for the car front
    angleOffset = constrain(angle / 5, -2, 2);
  }
  
  // Draw the car body
  uint32_t carColor = _pixels.Color(255, 255, 255); // White car
  
  // Draw the car body (main rectangle)
  fillRect(carX + 1, carY + 1, carWidth - 2, carHeight - 2, carColor);
  
  // Draw car front with angle offset
  int frontOffset = angleOffset;
  int frontX = carX + (angleOffset > 0 ? angleOffset : 0);
  
  // Draw car "windows" (darker areas inside the car)
  uint32_t windowColor = _pixels.Color(100, 100, 255); // Blue-tinted windows
  fillRect(carX + 2, carY + 2, 2, 2, windowColor);  // Left window
  fillRect(carX + carWidth - 4, carY + 2, 2, 2, windowColor);  // Right window
  
  // Add car details - headlights and taillights
  uint32_t headlightColor = _pixels.Color(255, 255, 200); // Yellow headlights
  uint32_t taillightColor = _pixels.Color(255, 0, 0);    // Red taillights
  
  // Front headlights (with angle offset)
  int frontLightX1 = carX + 1 + frontOffset;
  int frontLightX2 = carX + carWidth - 2 + frontOffset;
  
  if (frontLightX1 >= 0 && frontLightX1 < _matrixWidth) {
    int headlight1 = getPixelIndex(frontLightX1, carY);
    if (headlight1 >= 0) {
      _pixels.setPixelColor(headlight1, headlightColor);
    }
  }
  
  if (frontLightX2 >= 0 && frontLightX2 < _matrixWidth) {
    int headlight2 = getPixelIndex(frontLightX2, carY);
    if (headlight2 >= 0) {
      _pixels.setPixelColor(headlight2, headlightColor);
    }
  }
  
  // Back taillights
  int taillight1 = getPixelIndex(carX + 1, carY + carHeight - 1);
  int taillight2 = getPixelIndex(carX + carWidth - 2, carY + carHeight - 1);
  
  if (taillight1 >= 0) {
    _pixels.setPixelColor(taillight1, taillightColor);
  }
  
  if (taillight2 >= 0) {
    _pixels.setPixelColor(taillight2, taillightColor);
  }
  
  // Draw guides to indicate optimal position
  if (sidePosition != 0 || abs(angle) >= 5.0) {
    // Draw arrow pointing to the center when not centered
    uint32_t arrowColor = _pixels.Color(255, 255, 0); // Yellow for guidance
    
    // Draw horizontal arrow pointing from car to center
    int arrowY = _matrixHeight - 3;
    int centerX = _matrixWidth / 2;
    int arrowStartX, arrowEndX;
    
    if (sidePosition < 0) {
      // Car too far left, arrow points right
      arrowStartX = carX + carWidth;
      arrowEndX = centerX + 2;
      
      for (int x = arrowStartX; x <= arrowEndX; x++) {
        int pixelIndex = getPixelIndex(x, arrowY);
        if (pixelIndex >= 0) {
          _pixels.setPixelColor(pixelIndex, arrowColor);
        }
      }
      
      // Arrow head (right)
      int head1 = getPixelIndex(arrowEndX - 1, arrowY - 1);
      int head2 = getPixelIndex(arrowEndX - 1, arrowY + 1);
      if (head1 >= 0) _pixels.setPixelColor(head1, arrowColor);
      if (head2 >= 0) _pixels.setPixelColor(head2, arrowColor);
    } else if (sidePosition > 0) {
      // Car too far right, arrow points left
      arrowStartX = centerX - 2;
      arrowEndX = carX;
      
      for (int x = arrowStartX; x <= arrowEndX; x++) {
        int pixelIndex = getPixelIndex(x, arrowY);
        if (pixelIndex >= 0) {
          _pixels.setPixelColor(pixelIndex, arrowColor);
        }
      }
      
      // Arrow head (left)
      int head1 = getPixelIndex(arrowStartX + 1, arrowY - 1);
      int head2 = getPixelIndex(arrowStartX + 1, arrowY + 1);
      if (head1 >= 0) _pixels.setPixelColor(head1, arrowColor);
      if (head2 >= 0) _pixels.setPixelColor(head2, arrowColor);
    }
    
    // Indicate angle correction needed with blinker effect
    if (abs(angle) >= 5.0) {
      if ((millis() / 250) % 2 == 0) {
        uint32_t blinkerColor = _pixels.Color(255, 120, 0); // Orange for angle indicator
        
        if (angle < 0) {
          // Indicate left turn needed
          for (int y = 3; y < 6; y++) {
            int blinkerIndex = getPixelIndex(2, y);
            if (blinkerIndex >= 0) {
              _pixels.setPixelColor(blinkerIndex, blinkerColor);
            }
          }
        } else {
          // Indicate right turn needed
          for (int y = 3; y < 6; y++) {
            int blinkerIndex = getPixelIndex(_matrixWidth - 3, y);
            if (blinkerIndex >= 0) {
              _pixels.setPixelColor(blinkerIndex, blinkerColor);
            }
          }
        }
      }
    }
  }
  
  // Update the LED strip
  _pixels.show();
}