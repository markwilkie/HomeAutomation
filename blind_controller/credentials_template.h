/*
 * Credentials Configuration
 * 
 * Copy this file to credentials.h and update with your actual values
 * The credentials.h file should be added to .gitignore
 */

#ifndef CREDENTIALS_H
#define CREDENTIALS_H

// WiFi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// MQTT broker settings
const char* mqtt_server = "YOUR_MQTT_BROKER_IP";
const int mqtt_port = 1883;
const char* mqtt_user = "YOUR_MQTT_USER";     // Leave empty "" if no authentication
const char* mqtt_pass = "YOUR_MQTT_PASS";     // Leave empty "" if no authentication

#endif