#include "../include/TFLunaUART.h"
#include <SoftwareSerial.h> // Ensure SoftwareSerial is included

// Constructor implementation using SoftwareSerial
TFLunaUART::TFLunaUART(SoftwareSerial& serial) : _serialPort(serial) {
  _lidarData = {0, 0, 0, false}; // Initialize data structure
}

// Initialize the serial port used for the TF-Luna
void TFLunaUART::init(long baudRate) {
  _serialPort.begin(baudRate);
}

// Read data from the TF-Luna sensor via the specified serial port
bool TFLunaUART::readData() {
  static char i = 0;
  char j = 0;
  int checksum = 0;
  static int rx[9]; // Buffer to store incoming bytes

  _lidarData.receiveComplete = false; // Reset flag before reading

  while (_serialPort.available()) {
    rx[i] = _serialPort.read();

    // Check for the header bytes (0x59, 0x59)
    if (rx[0] != 0x59) {
      i = 0; // Reset if first byte is incorrect
    } else if (i == 1 && rx[1] != 0x59) {
      i = 0; // Reset if second byte is incorrect
    } else if (i == 8) { // We have received all 9 bytes
      // Calculate checksum
      checksum = 0; // Reset checksum calculation
      for (j = 0; j < 8; j++) {
        checksum += rx[j];
      }
      checksum %= 256; // Modulo 256

      // Verify checksum
      if (rx[8] == checksum) {
        // Checksum is valid, parse data
        _lidarData.distance = rx[2] + (rx[3] * 256); // Distance in cm
        _lidarData.strength = rx[4] + (rx[5] * 256); // Signal strength
        // Temperature calculation: (value / 8) - 256
        _lidarData.temp = (rx[6] + (rx[7] * 256)) / 8 - 256;
        _lidarData.receiveComplete = true; // Mark data as complete
      }
      // Reset index regardless of checksum validity to start receiving next packet
      i = 0;
      return _lidarData.receiveComplete; // Return immediately after processing a full packet
    } else {
      // Increment index if header is correct and packet is not yet complete
      i++;
    }
  }
  // Return false if no complete packet was processed in this call
  return false;
}

// Getter methods
int TFLunaUART::getDistance() {
  return _lidarData.distance;
}

int TFLunaUART::getStrength() {
  return _lidarData.strength;
}

int TFLunaUART::getTemperature() {
  return _lidarData.temp;
}

bool TFLunaUART::isDataReceived() {
  return _lidarData.receiveComplete;
}
