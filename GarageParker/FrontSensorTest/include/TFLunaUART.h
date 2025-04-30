#ifndef TFLUNA_UART_H
#define TFLUNA_UART_H

#include <Arduino.h>
#include <SoftwareSerial.h> // Include SoftwareSerial

// Structure to hold TF-Luna data
typedef struct {
  int distance;
  int strength;
  int temp;
  boolean receiveComplete;
} TFLunaData;

class TFLunaUART {
  private:
    TFLunaData _lidarData;
    SoftwareSerial& _serialPort; // Reference to the SoftwareSerial port

  public:
    // Constructor takes a reference to the SoftwareSerial port
    TFLunaUART(SoftwareSerial& serial);

    // Initialize the serial communication for the sensor
    void init(long baudRate = 115200);

    // Attempt to read data from the sensor
    // Returns true if a complete packet was received, false otherwise
    bool readData();

    // Get the latest distance reading (in cm)
    int getDistance();

    // Get the latest signal strength reading
    int getStrength();

    // Get the latest temperature reading (in degrees C)
    int getTemperature();

    // Check if the last read attempt resulted in a complete packet
    bool isDataReceived();
};

#endif // TFLUNA_UART_H
