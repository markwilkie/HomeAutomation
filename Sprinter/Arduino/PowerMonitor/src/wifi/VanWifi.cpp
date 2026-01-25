#include "wifi.h"

extern Logger logger;

const uint32_t connectTimeoutMs = 10000;

void VanWifi::startWifi()
{
  //just return if we're already connected
  if(isConnected())
    return;

  logger.log(INFO, "Starting WiFi...");

  //Setup wifi 
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);    // Switch WiFi on

  // Add list of wifi networks (only once)
  if (!apAdded) {
    wifiMulti.addAP(VANSSID, PASSWORD);
    wifiMulti.addAP(LFPSSID, PASSWORD);
    apAdded = true;
  }

  //Connects to the wifi with the strongest signal
  if(wifiMulti.run(connectTimeoutMs) == WL_CONNECTED) 
  {
    logger.log(INFO,"Connected to %s, IP: %s",WiFi.SSID().c_str(),WiFi.localIP().toString().c_str());
  }  
  else
  {
    logger.log(ERROR,"Could not connect to Wifi.  Moving on....");
  }

  //send logs
  logger.sendLogs(isConnected());
}

void VanWifi::stopWifi()
{
  if(!isConnected()) {
    logger.log(WARNING, "WiFi not connected, skipping stop");
    return;
  }
  
  //logger.log(INFO, "Stopping WiFi");
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true,true);  // disconnect and turn off station mode
  delay(200);  // Allow disconnect to complete
  WiFi.mode(WIFI_OFF);  
  delay(200);  // Allow mode change to complete  
  // Note: Don't call esp_wifi_stop() - it causes crashes on ESP32-S3
  // WiFi.mode(WIFI_OFF) is sufficient to release GPIO11
  //logger.log(INFO, "WiFi stopped");
}

/*
 * sendGetMessage() - Perform HTTP GET request and parse JSON response
 * 
 * Used primarily for time synchronization API calls.
 * Supports both HTTP and HTTPS URLs.
 * 
 * REQUEST HANDLING:
 * 1. Detect URL scheme (http:// vs https://)
 * 2. For HTTPS: Use WiFiClientSecure with certificate validation disabled
 *    (setInsecure() - simpler than managing certs on embedded device)
 * 3. For HTTP: Use standard WiFiClient
 * 4. Set 10-second timeout to prevent hangs
 * 5. Add RapidAPI headers for time API authentication
 * 
 * RESPONSE HANDLING:
 * - HTTP errors (negative codes): Log error, return empty JSON
 * - Success: Parse payload as JSON and return DynamicJsonDocument
 * 
 * @param url - Full URL to request (http:// or https://)
 * @return DynamicJsonDocument containing parsed response (empty on error)
 * 
 * NOTE: HTTPClient and WiFiClient are stack-allocated to avoid memory leaks.
 */
DynamicJsonDocument VanWifi::sendGetMessage(const char*url)
{
  DynamicJsonDocument doc(512);
  HTTPClient http;

  // Use stack-allocated clients to avoid memory leaks
  WiFiClientSecure clientSecure;
  WiFiClient client;

  // Check if URL is HTTPS
  bool isHttps = (strncmp(url, "https://", 8) == 0);
  
  if (isHttps) {
    clientSecure.setInsecure();  // Skip certificate validation for simplicity
    http.begin(clientSecure, url);
  } else {
    http.begin(client, url);
  }
  
  http.setTimeout(10000);  // Set timeout to 10 seconds (was 45)
  
  // Add RapidAPI headers
  http.addHeader("X-Rapidapi-Key", "bd06869c8cmsh236b160699d9725p1e8579jsn8c7774bf1408");
  http.addHeader("X-Rapidapi-Host", "world-time-api3.p.rapidapi.com");

  // Send HTTP GET request
  int httpResponseCode = http.GET();

  if (httpResponseCode<0)
  {
    ERRORPRINT("GET http Code: ");
    VERBOSEPRINT(httpResponseCode);
    VERBOSEPRINT(" - ");
    VERBOSEPRINTLN(http.errorToString(httpResponseCode));   

    return doc;
  }

  //get payload
  String payload = http.getString();
    
  // Free resources
  http.end();

  //return payload
  deserializeJson(doc, payload);  
  return doc;
}

bool VanWifi::isConnected()
{
  return (WiFi.status() == WL_CONNECTED);
}

String VanWifi::getSSID()
{
  return WiFi.SSID();
}

String VanWifi::getIP()
{
  return WiFi.localIP().toString();
}

int VanWifi::getRSSI()
{
  return WiFi.RSSI();
}
