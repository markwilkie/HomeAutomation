# RF Blind Controller for FireBeetle ESP32

This project controls A-OK blinds via RF transmission based on MQTT messages using a FireBeetle ESP32 board.

## Hardware Requirements

- FireBeetle ESP32 board
- 433MHz RF transmitter module (connected to GPIO2)
- A-OK compatible blinds

## Libraries Required

Install these libraries through Arduino IDE Library Manager:

1. **PubSubClient** by Nick O'Leary
   - GitHub: https://github.com/knolleary/pubsubclient
   - Arduino Library Manager: Search for "PubSubClient"

2. **ArduinoJson** by Benoit Blanchon
   - For parsing MQTT JSON messages

3. **A-OK Library** by Akira Kivioja
   - GitHub: https://github.com/akirjavainen/a-ok
   - Manual installation required (see installation instructions below)

## A-OK Library Installation

Since the A-OK library may not be in the Arduino Library Manager:

1. Download the library from: https://github.com/akirjavainen/a-ok
2. Extract to your Arduino libraries folder
3. Restart Arduino IDE

## Wiring

Connect the 433MHz RF transmitter to your FireBeetle ESP32:

- **VCC** → 3.3V
- **GND** → GND  
- **DATA** → GPIO2 (defined as RF_TX_PIN in code)

## Configuration

### 1. Credentials Setup
For security, credentials are stored separately:
1. Copy `credentials_template.h` to `credentials.h`
2. Update `credentials.h` with your actual values:
```cpp
const char* ssid = "YOUR_ACTUAL_WIFI_SSID";
const char* password = "YOUR_ACTUAL_WIFI_PASSWORD";
const char* mqtt_server = "YOUR_ACTUAL_MQTT_BROKER_IP";
// ... etc
```
**Note:** `credentials.h` is excluded from version control via `.gitignore`

### 3. Blind RF Codes
You need to capture the RF codes from your original A-OK remote. Update the `blinds[]` array:
```cpp
BlindCodes blinds[] = {
  {0x123456, 0x654321},  // Blind 1: up_code, down_code
  {0x789ABC, 0xCBA987},  // Blind 2: up_code, down_code
  // Add more blinds as needed
};
```

## MQTT Message Format

### Control Topic: `blinds/control`

Send JSON messages in this format:

**Raise blind:**
```json
{"blind": 1, "action": "up"}
```

**Lower blind:**
```json
{"blind": 2, "action": "down"}
```

### Status Topic: `blinds/status`

The controller publishes status messages every 60 seconds:
```json
{"status": "online", "ip": "192.168.1.100"}
```

## Usage Examples

### Using mosquitto_pub (command line):
```bash
# Raise blind 1
mosquitto_pub -h YOUR_MQTT_BROKER_IP -t "blinds/control" -m '{"blind": 1, "action": "up"}'

# Lower blind 2
mosquitto_pub -h YOUR_MQTT_BROKER_IP -t "blinds/control" -m '{"blind": 2, "action": "down"}'
```

### Using Home Assistant:
```yaml
# In configuration.yaml
mqtt:
  cover:
    - name: "Living Room Blind"
      command_topic: "blinds/control"
      payload_open: '{"blind": 1, "action": "up"}'
      payload_close: '{"blind": 1, "action": "down"}'
```

## Troubleshooting

### RF Code Capture
If you don't have the RF codes, you can use an RF receiver and scanner to capture them:
1. Use a 433MHz receiver module
2. Use library examples to capture signals
3. Press buttons on your original remote to record codes

### Common Issues

1. **WiFi Connection Problems:**
   - Check SSID and password
   - Ensure 2.4GHz network (ESP32 doesn't support 5GHz)

2. **MQTT Connection Issues:**
   - Verify broker IP and port
   - Check authentication credentials
   - Ensure firewall allows connections

3. **RF Transmission Problems:**
   - Check wiring connections
   - Verify RF transmitter is working
   - Ensure correct codes are programmed

## Monitoring

The system provides serial output for debugging. Open Serial Monitor at 115200 baud to see:
- WiFi connection status
- MQTT connection status
- Received messages
- RF transmission confirmations

## License

This project uses libraries with their respective licenses:
- PubSubClient: MIT License
- A-OK: Check repository for license details