/*
 * RF Blind Controller for FireBeetle ESP32
 * 
 * Controls A-OK blinds via RF transmission based on MQTT messages
 * Uses PubSubClient library for MQTT communication
 * Uses A-OK library for RF transmission
 * 
 * MQTT Message Format:
 * Topic: "blinds/control"
 * 
 * Individual blind control:
 * Payload: {"blind": 1, "action": "up"} or {"blind": 1, "action": "down"}
 * 
 * Group blind control:
 * End blinds (1-2): {"group": "endblinds", "action": "up"} or {"group": "endblinds", "action": "down"}
 * Side blinds (3-5): {"group": "sideblinds", "action": "up"} or {"group": "sideblinds", "action": "down"}
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "AOK.h"
#include "credentials.h"  // Contains WiFi and MQTT credentials
#include "blind_data.h"   // Contains all RF timing data for blinds 1-5
const char* mqtt_topic = "blinds/control";

// RF transmission pin for FireBeetle ESP32
const int RF_TX_PIN = 14;  // GPIO2 on FireBeetle ESP32

// A-OK remote configuration
// You'll need to capture these codes from your actual remote
// Each blind should have unique codes for up and down
struct BlindCodes {
  unsigned long up_code;
  unsigned long down_code;
};

// Raw RF timing data arrays are now included from blind_data.h

// Configure your blind codes here (capture from original remote)
BlindCodes blinds[] = {
  {0x123456, 0x654321},  // Blind 1: up_code, down_code
  {0x789ABC, 0xCBA987},  // Blind 2: up_code, down_code
  {0xDEF012, 0x210FED},  // Blind 3: up_code, down_code
  {0x345678, 0x876543},  // Blind 4: up_code, down_code
  {0x9ABCDE, 0xEDCBA9}   // Blind 5: up_code, down_code
};

const int MAX_BLINDS = sizeof(blinds) / sizeof(blinds[0]);

WiFiClient espClient;
PubSubClient client(espClient);
AOK aok(RF_TX_PIN);

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("Starting RF Blind Controller");
  
  // Initialize A-OK transmitter
  aok.begin();
  
  // Connect to WiFi
  setup_wifi();
  
  // Configure MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  
  Serial.println("Setup complete");
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  // Convert payload to string
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);
  
  // Parse JSON message
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }
  
  // Extract action
  String action = doc["action"];
  
  // Validate action
  if (action != "up" && action != "down") {
    Serial.println("Error: Invalid action. Use 'up' or 'down'");
    return;
  }
  
  // Check if it's a group command or individual blind
  if (doc.containsKey("group")) {
    String group = doc["group"];
    if (group == "endblinds") {
      // Control blinds 1-2 (end blinds)
      Serial.println("Controlling end blinds (1-2) - Action: " + action);
      control_blind(1, action);
      delay(500);  // Small delay between commands
      control_blind(2, action);
    } else if (group == "sideblinds") {
      // Control blinds 3-5 (side blinds)
      Serial.println("Controlling side blinds (3-5) - Action: " + action);
      control_blind(3, action);
      delay(500);  // Small delay between commands
      control_blind(4, action);
      delay(500);
      control_blind(5, action);
    } else {
      Serial.println("Error: Invalid group. Use 'endblinds' or 'sideblinds'");
      return;
    }
  } else if (doc.containsKey("blind")) {
    // Individual blind control
    int blind_number = doc["blind"];
    
    // Validate blind number
    if (blind_number < 1 || blind_number > MAX_BLINDS) {
      Serial.println("Error: Invalid blind number");
      return;
    }
    
    // Control the individual blind
    control_blind(blind_number, action);
  } else {
    Serial.println("Error: Missing 'blind' or 'group' parameter");
    return;
  }
}

void sendRawData(unsigned int rawData[], int length) {
  // Send raw RF data using timing values in microseconds
  for (int i = 0; i < length; i++) {
    if (i % 2 == 0) {
      // Even index = HIGH pulse
      digitalWrite(RF_TX_PIN, HIGH);
    } else {
      // Odd index = LOW pulse
      digitalWrite(RF_TX_PIN, LOW);
    }
    delayMicroseconds(rawData[i]);
  }
  // Ensure we end in LOW state
  digitalWrite(RF_TX_PIN, LOW);
}

void control_blind(int blind_number, String action) {
  Serial.print("Controlling blind ");
  Serial.print(blind_number);
  Serial.print(" - Action: ");
  Serial.println(action);
  
  // Validate blind number
  if (blind_number < 1 || blind_number > 5) {
    Serial.println("Error: Invalid blind number. Must be 1-5");
    return;
  }
  
  // Use raw RF data for all blinds
  unsigned int* rawData = nullptr;
  String blindAction = "Blind " + String(blind_number) + " " + action;
  
  // Select the appropriate raw data array based on blind number and action
  if (blind_number == 1 && action == "down") {
    rawData = blind1_down;
  } else if (blind_number == 1 && action == "up") {
    rawData = blind1_up;
  } else if (blind_number == 2 && action == "down") {
    rawData = blind2_down;
  } else if (blind_number == 2 && action == "up") {
    rawData = blind2_up;
  } else if (blind_number == 3 && action == "down") {
    rawData = blind3_down;
  } else if (blind_number == 3 && action == "up") {
    rawData = blind3_up;
  } else if (blind_number == 4 && action == "down") {
    rawData = blind4_down;
  } else if (blind_number == 4 && action == "up") {
    rawData = blind4_up;
  } else if (blind_number == 5 && action == "down") {
    rawData = blind5_down;
  } else if (blind_number == 5 && action == "up") {
    rawData = blind5_up;
  }
  
  if (rawData != nullptr) {
    Serial.println("Using raw RF data for " + blindAction);
    for (int i = 0; i < 9; i++) {  // Send 3 times for reliability
      sendRawData(rawData, 511);
      Serial.print("Sending transmission ");
      Serial.print(i + 1);
      Serial.println("/9");
      delay(10);  // delay between repeats
    }
    Serial.println("Completed raw RF transmission for " + blindAction);
  } else {
    Serial.println("Error: No raw data available for " + blindAction);
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    // Create a random client ID
    String clientId = "BlindController-";
    clientId += String(random(0xffff), HEX);
    
    // Attempt to connect
    bool connected;
    if (strlen(mqtt_user) > 0) {
      connected = client.connect(clientId.c_str(), mqtt_user, mqtt_pass);
    } else {
      connected = client.connect(clientId.c_str());
    }
    
    if (connected) {
      Serial.println("connected");
      // Subscribe to the control topic
      client.subscribe(mqtt_topic);
      Serial.print("Subscribed to: ");
      Serial.println(mqtt_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  // Optional: Add a heartbeat or status message
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 60000) {  // Every 60 seconds
    if (client.connected()) {
      String status = "{\"status\":\"online\",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
      client.publish("blinds/status", status.c_str());
    }
    lastHeartbeat = millis();
  }
}