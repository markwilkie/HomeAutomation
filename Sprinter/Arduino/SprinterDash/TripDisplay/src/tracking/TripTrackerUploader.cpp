#include <Arduino.h>
#include <ArduinoJson.h>
#include "TripTrackerUploader.h"
#include "RTClib.h"
#include "../Globals.h"
#include "../../secrets.h"

//
// TripTrackerUploader.cpp - Sends telemetry straight to TripTracker's own
// ingestion API over a Tailscale tunnel.
//
// POST http://TRIPTRACKER_HOST:TRIPTRACKER_PORT/api/v1/positions
// Header: X-Device-Key: TRIPTRACKER_DEVICE_KEY
// Body: {"recorded_at": <unix ts>, "lat":.., "lon":.., "elevation_m":..,
//        "speed_mps":.., "ignition":.., "engine_miles":..}
//
// recorded_at is sent as a plain Unix timestamp (number, not an ISO8601
// string) - TripTracker's Pydantic schema accepts either.
//

void TripTrackerUploader::init(TrackLogger *_trackLogger)
{
    trackLoggerPtr = _trackLogger;

    _requestQueue = xQueueCreate(TRIPTRACKER_QUEUE_SIZE, sizeof(PositionRequest));
    xTaskCreatePinnedToCore(backgroundTask, "TripTrackerHTTP", 8192, this, 1, &_taskHandle, 0);

    logger.log(INFO, "TripTrackerUploader ready (async) -> %s:%d", TRIPTRACKER_HOST, TRIPTRACKER_PORT);
}

// ---- Background task running on Core 0 ----

void TripTrackerUploader::backgroundTask(void* param)
{
    TripTrackerUploader* self = (TripTrackerUploader*)param;
    PositionRequest req;

    for (;;)
    {
        if (xQueueReceive(self->_requestQueue, &req, portMAX_DELAY) == pdTRUE)
        {
            if (self->sendPosition(req))
                self->uploadedCount++;
            else
                self->failedCount++;
        }
    }
}

// ---- Enqueue a live-position request for the background task ----

bool TripTrackerUploader::enqueue(float lat, float lon, float elevMeters, float speedMps, uint32_t unixTimestamp, int ignition, float engineMiles)
{
    if (_requestQueue == nullptr)
        return false;

    PositionRequest req = { lat, lon, elevMeters, speedMps, unixTimestamp, ignition, engineMiles };

    if (xQueueSend(_requestQueue, &req, 0) != pdTRUE)
    {
        PositionRequest discarded;
        xQueueReceive(_requestQueue, &discarded, 0);  // remove oldest
        logger.log(WARNING, "TripTracker queue full — dropped oldest position");
        failedCount++;
        xQueueSend(_requestQueue, &req, 0);  // now guaranteed to succeed
    }
    return true;
}

// ---- Live position sending ----

void TripTrackerUploader::sendLivePosition(float lat, float lon, float elevFeet, float speedMph, uint32_t secondsSince2000, bool ignitionOn, float engineMiles)
{
    // Skip if no valid position (0,0 = no fix yet)
    if (lat == 0 || lon == 0)
        return;

    // Throttle to LIVE_SEND_INTERVAL to avoid flooding the server/tunnel
    if (millis() < nextLiveSend)
        return;
    nextLiveSend = millis() + LIVE_SEND_INTERVAL;

    float elevMeters = elevFeet * 0.3048;
    float speedMps = speedMph * 0.44704;
    uint32_t unixTs = secondsSince2000 + SECONDS_FROM_1970_TO_2000;

    logger.log(VERBOSE, "TripTracker live: %f,%f elev=%fm spd=%fmps ign=%d miles=%f", lat, lon, elevMeters, speedMps, ignitionOn, engineMiles);

    enqueue(lat, lon, elevMeters, speedMps, unixTs, ignitionOn ? 1 : 0, engineMiles);
}

// ---- Synchronous send of a single position (used by the background task) ----

bool TripTrackerUploader::sendPosition(const PositionRequest &req)
{
    StaticJsonDocument<384> doc;
    doc["recorded_at"] = req.unixTimestamp;
    doc["lat"] = req.lat;
    doc["lon"] = req.lon;
    doc["elevation_m"] = req.elevMeters;
    doc["speed_mps"] = req.speedMps;
    if (req.ignition >= 0)
        doc["ignition"] = (bool)req.ignition;
    if (req.engineMiles >= 0)
        doc["engine_miles"] = req.engineMiles;

    String body;
    serializeJson(doc, body);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/api/v1/positions", TRIPTRACKER_HOST, TRIPTRACKER_PORT);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Key", TRIPTRACKER_DEVICE_KEY);
    http.setTimeout(5000);
    int httpCode = http.POST(body);
    http.end();

    if (httpCode == 201)
    {
        logger.log(VERBOSE, "TripTracker send OK (total: %d)", uploadedCount + 1);
        return true;
    }
    logger.log(WARNING, "TripTracker upload failed: HTTP %d  URL: %s  body: %s", httpCode, url, body.c_str());
    return false;
}

// ---- Batch upload of stored GPX files ----
// Called periodically from the main loop when connected. Reads trackpoints
// from previously-recorded GPX files on LittleFS and replays them as JSON
// array POSTs to /positions/batch, BATCH_POINTS_PER_UPLOAD at a time - this
// backfills TripTracker's history with positions recorded while offline.

void TripTrackerUploader::uploadBuffered()
{
    if (trackLoggerPtr == nullptr)
        return;

    if (millis() < nextBatchRetry)
        return;

    int fileCount = trackLoggerPtr->getFileCount();
    if (fileCount == 0)
        return;

    DynamicJsonDocument doc(8192);
    JsonArray points = doc.createNestedArray("points");
    int pointsQueued = 0;

    for (int fi = 0; fi < fileCount && pointsQueued < BATCH_POINTS_PER_UPLOAD; fi++)
    {
        String filename = trackLoggerPtr->getFileName(fi);
        if (filename.length() == 0)
            continue;

        String fullPath = String(TRACK_DIR) + "/" + filename;
        File f = LittleFS.open(fullPath, "r");
        if (!f)
            continue;

        int lineNum = 0;
        while (f.available() && pointsQueued < BATCH_POINTS_PER_UPLOAD)
        {
            String line = f.readStringUntil('\n');
            lineNum++;

            if (lineNum <= currentBatchLineIndex && fi == currentBatchFileIndex)
                continue;

            if (line.indexOf("<trkpt") < 0)
                continue;

            float lat = 0, lon = 0, ele = 0, speedMps = 0;
            long ts = 0;

            int latIdx = line.indexOf("lat=\"");
            int lonIdx = line.indexOf("lon=\"");
            if (latIdx >= 0 && lonIdx >= 0)
            {
                lat = line.substring(latIdx + 5, line.indexOf("\"", latIdx + 5)).toFloat();
                lon = line.substring(lonIdx + 5, line.indexOf("\"", lonIdx + 5)).toFloat();
            }

            int eleIdx = line.indexOf("<ele>");
            if (eleIdx >= 0)
                ele = line.substring(eleIdx + 5, line.indexOf("</ele>")).toFloat();

            // GPX <speed> is already stored in m/s (see TrackLogger)
            int spdIdx = line.indexOf("<speed>");
            if (spdIdx >= 0)
                speedMps = line.substring(spdIdx + 7, line.indexOf("</speed>")).toFloat();

            int timeIdx = line.indexOf("<time>");
            if (timeIdx >= 0)
            {
                String timeStr = line.substring(timeIdx + 6, line.indexOf("</time>"));
                int yr = timeStr.substring(0, 4).toInt();
                int mo = timeStr.substring(5, 7).toInt();
                int dy = timeStr.substring(8, 10).toInt();
                int hr = timeStr.substring(11, 13).toInt();
                int mn = timeStr.substring(14, 16).toInt();
                int sc = timeStr.substring(17, 19).toInt();
                DateTime dt(yr, mo, dy, hr, mn, sc);
                ts = dt.unixtime();
            }

            if (lat != 0 && lon != 0)
            {
                JsonObject point = points.createNestedObject();
                point["recorded_at"] = ts;
                point["lat"] = lat;
                point["lon"] = lon;
                point["elevation_m"] = ele;
                point["speed_mps"] = speedMps;
                // Buffered points were recorded mid-trip (offline driving);
                // ignition state isn't tracked per-buffered-point, so it's
                // just omitted here (TripTracker doesn't use it for trip
                // detection anyway).
                pointsQueued++;
            }
        }

        f.close();
        trackLoggerPtr->deleteFile(filename);
        currentBatchLineIndex = 0;
    }

    if (pointsQueued == 0)
        return;

    String body;
    serializeJson(doc, body);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/api/v1/positions/batch", TRIPTRACKER_HOST, TRIPTRACKER_PORT);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Key", TRIPTRACKER_DEVICE_KEY);
    http.setTimeout(10000);
    int httpCode = http.POST(body);
    http.end();

    if (httpCode == 201)
    {
        uploadedCount += pointsQueued;
        logger.log(VERBOSE, "TripTracker batch upload OK: %d points", pointsQueued);
    }
    else
    {
        failedCount += pointsQueued;
        logger.log(WARNING, "TripTracker batch upload failed: HTTP %d", httpCode);
        nextBatchRetry = millis() + BATCH_RETRY_INTERVAL;
    }
}

bool TripTrackerUploader::isUploading()
{
    return batchInProgress;
}

int TripTrackerUploader::getUploadedCount()
{
    return uploadedCount;
}

int TripTrackerUploader::getFailedCount()
{
    return failedCount;
}
