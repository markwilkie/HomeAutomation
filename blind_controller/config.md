# Blind Controller Configuration

## Required Libraries

Install these libraries through Arduino IDE Library Manager:

1. **PubSubClient** by Nick O'Leary
2. **ArduinoJson** by Benoit Blanchon
3. **A-OK Library** - Download from: https://github.com/akirjavainen/a-ok

## Configuration Steps

1. **Create credentials file:**
   - Copy `credentials_template.h` to `credentials.h`
   - Update `credentials.h` with your actual WiFi and MQTT settings
   - The `credentials.h` file is ignored by git for security

2. **Set RF codes:**
   - Update A-OK blind RF codes in the `blinds[]` array in `blind_controller.ino`

3. **Upload to FireBeetle ESP32**

## MQTT Commands

Send JSON to topic `blinds/control`:
- Raise: `{"blind": 1, "action": "up"}`
- Lower: `{"blind": 1, "action": "down"}`

## SmartThings Integration (Optional)

You can integrate this blind controller with SmartThings using **MQTTDevices**:

### About MQTTDevices
- **Repository:** https://github.com/toddaustin07/MQTTDevices
- **Purpose:** SmartThings Edge driver for creating MQTT-connected devices
- **Benefits:** Local execution (no cloud), supports multiple device types
- **License:** Apache-2.0

### Installation Steps:
1. **Install MQTTDevices driver:**
   - Use this [channel invite link](https://bestow-regional.api.smartthings.com/invite/Q1jP7BqnNNlL)
   - Enroll your hub and select "MQTT Devices V1.7"

2. **Configure MQTT connection:**
   - Add device → Scan for nearby devices
   - Find 'MQTT Device Creator V1.7' device
   - Configure MQTT broker IP, username, password in device settings

3. **Create blind control devices:**
   - Use the Creator device to make **Button** or **Switch** devices
   - Configure each device with:
     - **Subscribe Topic:** `blinds/status` (for status feedback)
     - **Publish Topic:** `blinds/control` 
     - **Message Format:** JSON
     - **Publish Values:** Configure up/down commands per blind

### Example SmartThings Device Configuration:
**For Blind 1:**
- **Device Type:** Switch or Button
- **Publish Topic:** `blinds/control`
- **Publish ON Value:** `{"blind": 1, "action": "up"}`
- **Publish OFF Value:** `{"blind": 1, "action": "down"}`
- **Subscribe Topic:** `blinds/status` (optional, for status updates)

**Supported Features:**
- Manual control via SmartThings app
- SmartThings automations and scenes
- Voice control (Alexa/Google via SmartThings)
- Multiple blinds as separate devices

## Hardware Connections

- RF Transmitter DATA pin → GPIO2
- RF Transmitter VCC → 3.3V
- RF Transmitter GND → GND