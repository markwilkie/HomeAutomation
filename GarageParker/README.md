# GarageParker

An Arduino-based garage parking assistant that helps you park your car in the garage by visualizing its position using ultrasonic sensors, a Time-of-Flight sensor, and an LED matrix.

## Hardware Requirements

- Arduino board (Uno, Nano, or similar)
- 2× HC-SR04 Ultrasonic Distance Sensors (Left and Right positions)
- 1x Adafruit VL53L1X Time-of-Flight Distance Sensor (Front position)
- WS2811/WS2812B LED Matrix (16x16 recommended, 256 LEDs total)
- Jumper wires
- 5V power supply (required for the LED matrix)
- Optional: Logic level shifter for LED data line if Arduino output is not reliably 5V.

## Wiring

- Connect the Left HC-SR04 ultrasonic sensor:
  - VCC to Arduino 5V
  - GND to Arduino GND
  - Trigger pin to Arduino pin 6 (defined as `LEFT_TRIG_PIN` in `DistanceSensor.cpp`)
  - Echo pin to Arduino pin 7 (defined as `LEFT_ECHO_PIN` in `DistanceSensor.cpp`)

- Connect the Right HC-SR04 ultrasonic sensor:
  - VCC to Arduino 5V
  - GND to Arduino GND
  - Trigger pin to Arduino pin 8 (defined as `RIGHT_TRIG_PIN` in `DistanceSensor.cpp`)
  - Echo pin to Arduino pin 9 (defined as `RIGHT_ECHO_PIN` in `DistanceSensor.cpp`)

- Connect the Adafruit VL53L1X Time-of-Flight sensor:
  - VIN to Arduino 5V (or 3.3V depending on board)
  - GND to Arduino GND
  - SCL to Arduino SCL pin (A5 on Uno/Nano)
  - SDA to Arduino SDA pin (A4 on Uno/Nano)
  - (Optional: Connect XSHUT and IRQ pins if needed for advanced configurations, but not required by the current code)

- Connect the WS2811/WS2812B LED Matrix:
  - Data pin to Arduino pin 3 (defined as `LED_PIN` in `LedController.cpp`)
  - VCC to 5V power supply
  - GND to Arduino GND and Power Supply GND

## Functionality

The system uses two side ultrasonic sensors and a front Time-of-Flight sensor to determine the car's position within the garage and provides visual feedback on a 16x16 LED matrix:

- **Car Position Visualization**:
  - A representation of the car is displayed on the matrix.
  - The car's X position on the matrix corresponds to its side-to-side position in the garage (based on Left/Right sensors).
  - The car's Y position on the matrix corresponds to its front-to-back position (based on the Front VL53L1X sensor).
- **Optimal Position Indication**:
  - When the car is within the optimal side-to-side and front-to-back range, the entire matrix background turns green.
- **Guidance Indicators**:
  - If the car is too far left or right, yellow indicator lines appear on the respective side of the car visualization.
  - If the car is too close to the front wall, a red indicator line appears above the car visualization.
- **Screen Timeout**:
  - The LED matrix display automatically turns off 2 minutes after the car is first detected in the garage to save power. It turns back on when the car leaves and re-enters.
- **Serial Output**:
  - Provides distance percentages and position status (e.g., "CENTERED LEFT/RIGHT", "TOO CLOSE TO FRONT WALL") via the Serial Monitor for debugging and monitoring.

## Sensor Placement

For accurate position calculation, the sensors should be mounted as follows:
- Left and Right HC-SR04 sensors: On the side wall of your garage, facing perpendicular to the wall. Their readings determine the side-to-side position.
- Front VL53L1X sensor: On the front wall of the garage (or another suitable location), facing the front of the car. Its reading determines the front-to-back position.
- Ensure sensors are mounted at a height that provides consistent readings from the car body.
- The code assumes garage dimensions defined by `GARAGE_WIDTH` (310cm) and `GARAGE_LENGTH` (670cm) in `DistanceSensor.h`. Adjust these if necessary.

## How It Works

1. The Left and Right ultrasonic sensors measure the distance to the side of the car. These are averaged (or handled individually if one is out of range) to determine the side distance.
2. The Front VL53L1X Time-of-Flight sensor measures the distance to the front of the car using I2C communication.
3. The system calculates the side position percentage (`sidePerc`) relative to `GARAGE_WIDTH` and the front position percentage (`frontPerc`) relative to `GARAGE_LENGTH`.
4. These percentages are compared to optimal ranges (`optimalSidePerc`, `optimalFrontPerc`, +/- `plusMinusSidePerc`, `plusMinusFrontPerc` defined in `GarageParker.ino`).
5. The `LedController` class uses these percentages and status enums (`SidePositionStatus`, `FrontPositionStatus`) to calculate the car's position on the 16x16 matrix and display appropriate visual feedback.
6. The display turns off after a timeout or when the car leaves the garage.

## Required Libraries

- Adafruit NeoPixel library for controlling the WS2811/WS2812B LED matrix.
- Adafruit VL53L1X library for the Time-of-Flight sensor.

To install libraries: Sketch > Include Library > Manage Libraries...
- Search for "Adafruit NeoPixel" and install it.
- Search for "Adafruit VL53L1X" and install it.

## Setup Instructions

1. Connect the hardware as described in the wiring section. Ensure the LED matrix has sufficient power from the external 5V supply.
2. Install the Adafruit NeoPixel and Adafruit VL53L1X libraries via the Arduino Library Manager.
3. Upload the `GarageParker.ino` sketch to your Arduino board.
4. Mount the sensors in their respective locations (Side wall for Left/Right HC-SR04, Front wall for VL53L1X).
   - Ensure sensors are level and properly aligned.
   - Adjust `GARAGE_WIDTH` and `GARAGE_LENGTH` in `DistanceSensor.h` if your garage dimensions differ significantly.
   - Adjust `optimalSidePerc`, `plusMinusSidePerc`, `optimalFrontPerc`, `plusMinusFrontPerc` in `GarageParker.ino` to fine-tune the desired parking position.
5. Position the LED matrix where it's easily visible from the driver's seat.

## Tips for Optimal Usage

- Ensure the sensors have a clear line of sight to the car in its typical parking path.
- Experiment with the `optimal...Perc` and `plusMinus...Perc` constants in `GarageParker.ino` to define the "sweet spot" for your specific car and garage.
- The `MAX_ULTRASONIC_DISTANCE` and `MAX_TOF_DISTANCE` in `DistanceSensor.h` define the maximum range the sensors will consider valid. Adjust if needed, but 300-400cm is typical for HC-SR04 and up to 400cm for VL53L1X.
- The brightness of the LED matrix can be adjusted using `strip.setBrightness()` in `LedController.cpp`.

## Future Enhancements

- Add EEPROM storage for saving calibration settings (optimal percentages, garage dimensions).
- Implement Bluetooth connectivity for mobile app integration or configuration.
- Add audible feedback (buzzer) for reaching the optimal position or warnings.
- Refine the car visualization on the LED matrix.