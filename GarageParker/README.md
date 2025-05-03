# GarageParker

An Arduino-based garage parking assistant that helps you park your car in the garage by visualizing its position using ultrasonic sensors, a TF-Luna Lidar sensor (via Serial), and an LED matrix.

**Last Updated:** May 3, 2025

## Hardware Requirements

- Arduino board (Uno, Nano, or similar - Uno/Nano require SoftwareSerial for TF-Luna)
- 2× HC-SR04 Ultrasonic Distance Sensors (Left and Right positions)
- 1x TF-Luna Lidar Sensor (Front position)
- WS2811/WS2812B LED Matrix (16x16 recommended, 256 LEDs total)
- Jumper wires
- 5V power supply (required for the LED matrix)
- Optional: Logic level shifter for LED data line if Arduino output is not reliably 5V.

## Wiring

- Connect the Left HC-SR04 ultrasonic sensor:
  - VCC to Arduino 5V
  - GND to Arduino GND
  - Trigger pin to Arduino pin 8 (defined as `LEFT_TRIG_PIN` in `DistanceSensor.cpp`)
  - Echo pin to Arduino pin 9 (defined as `LEFT_ECHO_PIN` in `DistanceSensor.cpp`)

- Connect the Right HC-SR04 ultrasonic sensor:
  - VCC to Arduino 5V
  - GND to Arduino GND
  - Trigger pin to Arduino pin 6 (defined as `RIGHT_TRIG_PIN` in `DistanceSensor.cpp`)
  - Echo pin to Arduino pin 7 (defined as `RIGHT_ECHO_PIN` in `DistanceSensor.cpp`)

- Connect the TF-Luna Lidar sensor (Serial Mode):
  - VCC (5V) to Arduino 5V 
  - GND to Arduino GND
  - TX (TF-Luna Pin 3) to Arduino Pin 10 (defined as `TFLUNA_RX_PIN` in `GarageParker.ino`) - Uses SoftwareSerial RX
  - RX (TF-Luna Pin 2) to Arduino Pin 11 (defined as `TFLUNA_TX_PIN` in `GarageParker.ino`) - Uses SoftwareSerial TX
  - Note: TF-Luna uses Serial communication (default 115200 baud). If using Arduino Mega or similar with hardware serial ports available, you could adapt the code to use `Serial1`, `Serial2`, etc. instead of SoftwareSerial.
  - See the [Waveshare Wiki](https://www.waveshare.com/wiki/TF-Luna_LiDAR_Range_Sensor) for more details on the sensor.

- Connect the WS2811/WS2812B LED Matrix:
  - Data pin to Arduino pin 5 (defined as `LED_DATA_PIN` in `LedController.h`)
  - VCC to 5V power supply
  - GND to Arduino GND and Power Supply GND

## Functionality

The system uses two side ultrasonic sensors and a front TF-Luna Lidar sensor (communicating via Serial) to determine the car's position within the garage and provides visual feedback on a 16x16 LED matrix:

- **Car Position Visualization**:
  - A representation of the car is displayed on the matrix.
  - The car's X position on the matrix corresponds to its side-to-side position in the garage (based on Left/Right sensors).
  - The car's Y position on the matrix corresponds to its front-to-back position (based on the Front TF-Luna sensor).
- **Optimal Position Indication**:
  - When the car is within the optimal side-to-side and front-to-back range, the entire matrix background turns green.
- **Guidance Indicators**:
  - If the car is too far left or right, yellow indicator lines appear on the respective side of the car visualization.
  - If the car is too close to the front wall, a red indicator line appears above the car visualization.

- **Garage Occupancy Detection**:
  - The system uses the **Right HC-SR04 ultrasonic sensor** to determine if the garage is occupied.
  - If the sensor detects an object (indicating a car is potentially present), the system immediately considers the garage occupied.
  - To avoid flickering or prematurely turning off the display, the system only marks the garage as **empty** after the right sensor has consistently reported no object for **10 seconds**.

- **Screen Timeout**:
  - The LED matrix display automatically turns off 2 minutes after the car is first detected in the garage to save power. It turns back on when the car leaves and re-enters.
- **Serial Output**:
  - Provides distance percentages and position status (e.g., "CENTERED LEFT/RIGHT", "TOO CLOSE TO FRONT WALL") via the Serial Monitor for debugging and monitoring.

## Sensor Placement

For accurate position calculation, the sensors should be mounted as follows:
- Left and Right HC-SR04 sensors: On the side wall of your garage, facing perpendicular to the wall. Their readings determine the side-to-side position.
- Front TF-Luna Lidar sensor: On the front wall of the garage (or another suitable location), facing the front of the car. Its reading determines the front-to-back position.
- Ensure sensors are mounted at a height that provides consistent readings from the car body.
- The code assumes garage dimensions defined by `GARAGE_WIDTH` (310cm) and `GARAGE_LENGTH` (670cm) in `DistanceSensor.h`. Adjust these if necessary.

## How It Works

1. The Left and Right ultrasonic sensors measure the distance to the side of the car. These are averaged (or handled individually if one is out of range) to determine the side distance.
2. The Front TF-Luna Lidar sensor measures the distance to the front of the car using Serial communication (via SoftwareSerial).
3. The system calculates the side position percentage (`sidePerc`) and the front position percentage (`frontPerc`). These percentages are used to determine the car's position relative to the desired optimal spot.
    - **Side Percentage (`sidePerc`):** This represents the car's side-to-side position.
        - `100%` (or `1.0`) means the car is perfectly centered according to the `optimalSidePerc` setting (e.g., 90cm from the left wall if `optimalSidePerc` is 0.9 and `GARAGE_WIDTH` is 300cm).
        - Values **greater than 100%** mean the car is **too far to the left**.
        - Values **less than 100%** mean the car is **too far to the right**.
        - The raw percentage is calculated based on the distance from the *left* sensor relative to the `GARAGE_WIDTH`. The comparison to `optimalSidePerc` determines the final status (LEFT, RIGHT, CENTER).
    - **Front Percentage (`frontPerc`):** This represents how close the car's front bumper is to the desired stopping point.
        - `100%` (or `1.0`) means the car has reached the optimal stopping distance defined by `optimalFrontPerc` relative to the `GARAGE_LENGTH`.
        - Values **greater than 100%** mean the car is **too far away** from the front wall (needs to move forward).
        - Values **less than 100%** mean the car is **too close** to the front wall.
4. These percentages are compared to optimal ranges (`optimalSidePerc`, `plusMinusSidePerc`, `optimalFrontPerc`, `plusMinusFrontPerc` defined in `GarageParker.ino`).
5. The `LedController` class uses these percentages and status enums (`SidePositionStatus`, `FrontPositionStatus`) to calculate the car's position on the 16x16 matrix and display appropriate visual feedback.
6. The display turns off after a timeout or when the car leaves the garage.

## Required Libraries

- Adafruit NeoPixel library for controlling the WS2811/WS2812B LED matrix.
- `TFLunaUART.h` (Included in this project's `include/` directory) for direct UART communication with the TF-Luna sensor. See the [Waveshare Wiki](https://www.waveshare.com/wiki/TF-Luna_LiDAR_Module) for sensor details.
- SoftwareSerial library (built-in Arduino library, used by `TFLunaUART.h`).

To install libraries: Sketch > Include Library > Manage Libraries...
- Search for "Adafruit NeoPixel" and install it.
- The `TFLunaUART.h` library is included locally and does not need separate installation.

## Setup Instructions

1. Connect the hardware as described in the wiring section. Ensure the LED matrix has sufficient power.
2. Install the Adafruit NeoPixel library via the Arduino Library Manager (if not already installed).
3. Upload the `GarageParker.ino` sketch to your Arduino board.
4. Mount the sensors in their respective locations (Side wall for Left/Right HC-SR04, Front wall for TF-Luna).
   - Ensure sensors are level and properly aligned.
   - Adjust `GARAGE_WIDTH` and `GARAGE_LENGTH` in `DistanceSensor.h` if your garage dimensions differ significantly.
   - Adjust `optimalSidePerc`, `plusMinusSidePerc`, `optimalFrontPerc`, `plusMinusFrontPerc` in `GarageParker.ino` to fine-tune the desired parking position.
5. Position the LED matrix where it's easily visible from the driver's seat.

## Tips for Optimal Usage

- Ensure the sensors have a clear line of sight to the car in its typical parking path.
- Experiment with the `optimal...Perc` and `plusMinus...Perc` constants in `GarageParker.ino` to define the "sweet spot" for your specific car and garage.
- The `MAX_ULTRASONIC_DISTANCE` and `MAX_LIDAR_DISTANCE` in `DistanceSensor.h` define the maximum range the sensors will consider valid. Adjust if needed (TF-Luna range is up to 800cm).
- The brightness of the LED matrix can be adjusted using `_strip->setBrightness()` in `LedController.cpp`.
- **Important:** Ensure the correct TX/RX pins are used for SoftwareSerial connection to the TF-Luna.

## Future Enhancements

- Add EEPROM storage for saving calibration settings (optimal percentages, garage dimensions).
- Implement Bluetooth connectivity for mobile app integration or configuration.
- Add audible feedback (buzzer) for reaching the optimal position or warnings.
- Refine the car visualization on the LED matrix.
- Allow configuration of TF-Luna settings (e.g., sample rate).