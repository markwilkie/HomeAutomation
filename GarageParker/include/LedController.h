#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <FastLED.h>

// Define enums for position status
enum class FrontPositionStatus {
  TOO_CLOSE,
  OPTIMAL,
  TOO_FAR
};

enum class SidePositionStatus {
  LEFT,
  CENTER,
  RIGHT
};

class LedController {
  private:
    int _dataPin;
    int _numLeds;
    
    // Colors
    CRGB _colorGreen;
    CRGB _colorYellow;
    CRGB _colorRed;
    CRGB _colorOff;
    CRGB _colorCar;
    CRGB _colorGrey;
    CRGB _colorOptimal;
    
    // LED array
    CRGB* _leds;
    
    // Helper methods for matrix
    int getPixelIndex(int x, int y);
    void drawRect(int x, int y, int width, int height, CRGB color);
    void fillRect(int x, int y, int width, int height, CRGB color);
    
  public:
    LedController(int safeDistance, int warningDistance);
    ~LedController(); // Destructor to free allocated memory
    void init();
    void clearScreen();
    void setAllPixels(CRGB color);
    void visualizeCar(float, float, SidePositionStatus sideStatus, FrontPositionStatus frontStatus);
    
    // Expose direct access to the LED array for external animations
    CRGB* getLeds() { return _leds; }
    int getNumLeds() { return _numLeds; }
    void show() { FastLED.show(); }
};

#endif