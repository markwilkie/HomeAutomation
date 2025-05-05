#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Adafruit_NeoPixel.h>

#define LED_DATA_PIN 5 // Pin connected to the data line of the LED strip
#define LED_MATRIX_WIDTH 16 // Width of the LED matrix
#define LED_MATRIX_HEIGHT 16 // Height of the LED matrix

#define CAR_WIDTH 4 // Width of the car in pixels
#define CAR_HEIGHT 8 // Height of the car in pixels


// Define enums for position status
enum class FrontPositionStatus {
  DANGEROUSLY_CLOSE,
  TOO_CLOSE,
  OPTIMAL,
  TOO_FAR,
  DANGEROUSLY_FAR 
};

enum class SidePositionStatus {
  DANGEROUSLY_LEFT,
  LEFT,
  CENTER,
  RIGHT,
  DANGEROUSLY_RIGHT
};

class LedController {
  private:
    int _dataPin;
    int _numLeds;
    int _matrixWidth;
    int _matrixHeight;

    // Adafruit NeoPixel object
    Adafruit_NeoPixel* _strip;

    // Colors (using uint32_t for Adafruit NeoPixel)
    uint32_t _colorGreen;
    uint32_t _colorYellow;
    uint32_t _colorRed;
    uint32_t _colorOff;
    uint32_t _colorCar;
    uint32_t _colorGrey;
    uint32_t _colorOptimal;

    // Helper methods for matrix
    int getPixelIndex(int x, int y);
    void drawLine(int x0, int y0, int x1, int y1, uint32_t color); // Add drawLine declaration
    void drawRect(int x, int y, int width, int height, uint32_t color);
    void fillRect(int x, int y, int width, int height, uint32_t color);
    void setPixelSafe(int x, int y, uint32_t color);
    void drawSideIndicators(SidePositionStatus sideStatus);
    void drawCenterLine(SidePositionStatus sideStatus);
    void drawOptimalFrontTargetLine(FrontPositionStatus frontStatus); // Add frontStatus parameter
    void drawArrow(int x, int y, int direction, uint32_t color); // Add drawArrow declaration

  public:
    LedController(); //
    ~LedController(); // Destructor to free allocated memory
    void init(uint8_t brightness = 150); // Add brightness parameter
    void clearScreen();
    void visualizeCar(float sidePerc, float frontPerc, SidePositionStatus sideStatus, FrontPositionStatus frontStatus);
    void setBrightness(uint8_t brightness); // Method to adjust brightness
    void show(); // Method to update the strip
};

#endif