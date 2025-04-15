# GarageParker

An Arduino-based garage parking assistant that helps you park your car in the garage by measuring and displaying the angle of your car using multiple distance sensors.

## Hardware Requirements

- Arduino board (Uno, Nano, or similar)
- Sharp GP2Y0A21YK0F IR Distance Sensor (middle position)
- 2× HC-SR04 Ultrasonic Distance Sensors (left and right positions)
- WS2812B LED strip (16 LEDs recommended)
- Jumper wires
- 5V power supply (for powering the LED strip if using more than a few LEDs)
- Optional: 330-470 Ohm resistor for the LED data line

## Wiring

- Connect the Sharp GP2Y0A21YK0F IR sensor:
  - VCC to Arduino 5V
  - GND to Arduino GND
  - Output to Arduino analog pin A0

- Connect the left HC-SR04 ultrasonic sensor:
  - VCC to Arduino 5V
  - GND to Arduino GND
  - Trigger pin to Arduino pin 9
  - Echo pin to Arduino pin 10

- Connect the right HC-SR04 ultrasonic sensor:
  - VCC to Arduino 5V
  - GND to Arduino GND
  - Trigger pin to Arduino pin 11
  - Echo pin to Arduino pin 12

- Connect the WS2812B LED strip:
  - Data pin to Arduino pin 6
  - VCC to 5V power supply
  - GND to Arduino GND
  - Optional: Add a 330-470 Ohm resistor between Arduino pin 6 and the LED strip data pin

## Functionality

The system uses three distance sensors mounted on the side wall of your garage to calculate the angle of your car and provides visual feedback using a WS2812B LED strip:

- **Car Angle Visualization**:
  - Green LED: Car is perfectly positioned (straight and centered)
  - Yellow LED: Car is straight but not centered
  - Red LED: Car is at an angle

- **Angle Direction and Magnitude**:
  - The position of the lit LED indicates the angle of the car
  - Blue gradient indicates car angled to the left
  - Purple gradient indicates car angled to the right
  - Blinking green in the center guides you to the perfect position

- **Distance Warning**:
  - When too close to the side wall, all LEDs pulse red

## Sensor Placement

For accurate angle calculation, the sensors should be mounted as follows:
- On the side wall of your garage at approximately the height of your car's side
- Left and right ultrasonic sensors spaced 100cm apart (adjustable in code)
- IR sensor positioned in the middle between the two ultrasonic sensors
- All sensors facing perpendicular to the wall towards your parking area

## How It Works

1. The left and right ultrasonic sensors measure the distance to the side of your car
2. The IR sensor in the middle measures the middle distance
3. The system calculates the angle of your car based on the difference in these distances
4. The LED strip displays the angle and guides you to the perfect parking position

## Required Libraries

- Adafruit NeoPixel library for controlling the WS2812B LED strip
- Math.h (built into Arduino) for angle calculations

To install Adafruit NeoPixel: Sketch > Include Library > Manage Libraries... > Search for "Adafruit NeoPixel"

## Setup Instructions

1. Connect the hardware as described in the wiring section
2. Install the required libraries if not already installed
3. Upload the GarageParker.ino sketch to your Arduino board
4. Mount the sensors on the side wall of your garage
   - Ensure the sensors are level and properly aligned
   - The standard spacing is 100cm between the left and right ultrasonic sensors
   - If you change this spacing, update the `sensorSpacing` constant in the code
5. Position the LED strip where it's easily visible from the driver's seat

## Tips for Optimal Usage

- For best results, mount the sensors at the same height as the widest part of your car
- Calibrate the `angleThreshold` constant in the code based on your parking needs
- The `maxAngle` constant can be adjusted based on your garage's geometry
- Ensure the sensors have a clear line of sight to your car

## Future Enhancements

- Add motor control for automated garage door opening
- Implement Bluetooth connectivity for mobile app integration
- Add EEPROM storage for saving calibration settings
- Add a buzzer for audible feedback when in optimal position