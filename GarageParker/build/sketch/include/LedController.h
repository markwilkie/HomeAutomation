#line 1 "C:\\Users\\mark\\MyProjects\\GitHub\\GarageParker\\include\\LedController.h"
#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Adafruit_NeoPixel.h>

class LedController {
  private:
    int _dataPin;
    int _numLeds;
    int _safeDistance;
    int _warningDistance;
    
    // 2D matrix dimensions
    int _matrixWidth;
    int _matrixHeight;
    
    // Colors
    uint32_t _colorGreen;
    uint32_t _colorYellow;
    uint32_t _colorRed;
    uint32_t _colorOff;
    uint32_t _colorCar;
    uint32_t _colorOptimal;
    
    // Helper methods for matrix
    int getPixelIndex(int x, int y);
    void drawRect(int x, int y, int width, int height, uint32_t color);
    void fillRect(int x, int y, int width, int height, uint32_t color);
    
  public:
    Adafruit_NeoPixel _pixels; // Made public for external animations
    
    LedController(int dataPin, int numLeds, int safeDistance, int warningDistance);
    void init();
    void setupMatrix(int width, int height);
    void updateBasedOnDistance(long distance);
    void showProgressBar(long distance, long maxDistance);
    void setAllPixels(uint32_t color);
    void visualizeCar(float angle, int sidePosition, float distance);
};

#endif