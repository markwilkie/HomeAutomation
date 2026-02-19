#include <Arduino.h>
#include "ElevationAPI.h"
#include "../Globals.h"
#include "../net/VanWifi.h"

// ============================================================================
//  ElevationAPI — Queries Open Topo Data to auto-calibrate the barometer
// ============================================================================
//
//  The MPL3115A2 barometric altimeter assumes standard atmosphere (1013.25 hPa
//  at sea level).  Real atmospheric pressure varies with weather, causing the
//  raw reading to drift by 50–200+ feet.  This module uses the GPS position
//  to look up the true ground elevation from a DEM (Digital Elevation Model)
//  and computes a correction offset.
//
//  Flow:
//    1. update() is called every loop iteration (self-throttled)
//    2. When it's time, queryAPI() sends an HTTPS GET to Open Topo Data
//    3. Parse the JSON response → true ground elevation in meters
//    4. Convert to feet, compare with raw baro reading
//    5. Compute offset = (API elevation) - (raw baro elevation)
//    6. Sanity-check the offset, then apply it
//
//  The offset is stored externally in PropBag (EEPROM) by the caller so it
//  persists across reboots.  This module only computes the offset value;
//  it doesn't write EEPROM itself.
// ============================================================================


void ElevationAPI::init()
{
    elevationOffset = 0;
    calibrated = false;
    lastAPIElevFeet = 0;
    lastCalibMillis = 0;
    nextCalibMillis = 0;  // calibrate ASAP on first call
    calibCount = 0;
}

// ----------------------------------------------------------------------------
//  update() — Call from loop() when WiFi + GPS are available
// ----------------------------------------------------------------------------
//  Self-throttled: only queries the API when nextCalibMillis has elapsed.
//  rawBaroElevFeet is the UNCORRECTED barometer altitude (before offset).
//
//  Returns true if a new calibration offset was just computed.

bool ElevationAPI::update(float lat, float lon, int rawBaroElevFeet)
{
    // Not time yet?
    if (millis() < nextCalibMillis)
        return false;

    // Schedule the next calibration regardless of success/failure.
    // This prevents hammering the API if there's a transient error.
    nextCalibMillis = millis() + RECALIB_INTERVAL_MS;

    // Sanity-check GPS coordinates — reject obviously invalid positions
    // (e.g. 0,0 from cold start or bad fix)
    if (lat == 0.0 && lon == 0.0)
    {
        logger.log(WARNING, "ElevationAPI: Skipping calibration — GPS at 0,0");
        return false;
    }

    // Query the API
    float apiElevMeters = 0;
    if (!queryAPI(lat, lon, apiElevMeters))
    {
        logger.log(WARNING, "ElevationAPI: API query failed");
        return false;
    }

    // Convert API result from meters to feet
    float apiElevFeet = apiElevMeters * 3.28084;
    lastAPIElevFeet = apiElevFeet;

    // Compute the offset: how far off is the barometer?
    int newOffset = (int)(apiElevFeet - (float)rawBaroElevFeet);

    // Sanity check: reject wildly unreasonable offsets.
    // This catches bad GPS fixes, API errors, or being in a tunnel/garage.
    if (abs(newOffset) > MAX_REASONABLE_OFFSET_FT)
    {
        logger.log(WARNING, "ElevationAPI: Offset %d ft too large, ignoring (API=%f, Baro=%d)",
                   newOffset, apiElevFeet, rawBaroElevFeet);
        return false;
    }

    // Apply the new offset
    elevationOffset = newOffset;
    calibrated = true;
    lastCalibMillis = millis();
    calibCount++;

    logger.log(INFO, "ElevationAPI: Calibrated! API=%f ft, Baro=%d ft, Offset=%d ft (count=%d)",
               apiElevFeet, rawBaroElevFeet, elevationOffset, calibCount);

    return true;
}

// ----------------------------------------------------------------------------
//  queryAPI() — HTTP GET to Open Topo Data, parse JSON response
// ----------------------------------------------------------------------------
//  URL format: https://api.opentopodata.org/v1/ned10m?locations=47.65,-117.42
//  Response:   {"results":[{"elevation":715.3,...}],"status":"OK"}
//
//  Returns true on success, writing the elevation (meters) to elevMeters.

bool ElevationAPI::queryAPI(float lat, float lon, float &elevMeters)
{
    HTTPClient http;

    // Build the API URL
    char url[200];
    snprintf(url, sizeof(url),
             "https://%s/v1/%s?locations=%.6f,%.6f",
             ELEVATION_API_HOST, ELEVATION_API_DATASET, lat, lon);

    logger.log(VERBOSE, "ElevationAPI: GET %s", url);

    http.begin(url);
    http.setTimeout(ELEVATION_API_TIMEOUT);
    int httpCode = http.GET();

    if (httpCode != 200)
    {
        logger.log(WARNING, "ElevationAPI: HTTP %d", httpCode);
        http.end();
        return false;
    }

    // Parse JSON response
    // Example: {"results":[{"dataset":"ned10m","elevation":715.3,"location":{"lat":47.65,"lng":-117.42}}],"status":"OK"}
    String payload = http.getString();
    http.end();

    // Use ArduinoJson to parse
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err)
    {
        logger.log(WARNING, "ElevationAPI: JSON parse error: %s", err.c_str());
        return false;
    }

    // Check status field
    const char* status = doc["status"];
    if (!status || strcmp(status, "OK") != 0)
    {
        logger.log(WARNING, "ElevationAPI: API status not OK");
        return false;
    }

    // Extract elevation from first result
    JsonArray results = doc["results"];
    if (results.size() == 0)
    {
        logger.log(WARNING, "ElevationAPI: No results in response");
        return false;
    }

    // Check for null elevation (happens when coordinates are outside dataset coverage)
    if (results[0]["elevation"].isNull())
    {
        logger.log(WARNING, "ElevationAPI: Null elevation (outside NED coverage?)");
        return false;
    }

    elevMeters = results[0]["elevation"].as<float>();
    logger.log(VERBOSE, "ElevationAPI: API returned %f meters (%f feet)", elevMeters, elevMeters * 3.28084);

    return true;
}

// ---- Accessors ----

int ElevationAPI::getElevationOffset()
{
    return elevationOffset;
}

void ElevationAPI::forceRecalibrate()
{
    nextCalibMillis = 0;  // will trigger on next update() call
}

bool ElevationAPI::isCalibrated()
{
    return calibrated;
}

float ElevationAPI::getLastAPIElevation()
{
    return lastAPIElevFeet;
}

unsigned long ElevationAPI::getLastCalibTime()
{
    return lastCalibMillis;
}

int ElevationAPI::getCalibrationCount()
{
    return calibCount;
}
