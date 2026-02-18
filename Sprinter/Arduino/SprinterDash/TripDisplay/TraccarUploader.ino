#include "TraccarUploader.h"
#include "RTClib.h"

//
// TraccarUploader.ino - Uploads GPS positions to a Traccar server
//
// Uses the OsmAnd protocol, which is a simple HTTP GET request:
//   http://host:5055/?id=DEVICE_ID&lat=47.606&lon=-122.332&altitude=56&speed=65&timestamp=1708272600
//
// Traccar receives this on its OsmAnd listener port (default 5055).
// No authentication is needed — the device is identified by the 'id' parameter,
// which must match a device configured in the Traccar web UI.
//
// Two operating modes:
//
// 1. LIVE MODE (WiFi connected while driving)
//    - Sends current position every LIVE_SEND_INTERVAL (10s)
//    - Quick and lightweight — single HTTP GET per position
//
// 2. BATCH MODE (WiFi reconnects after offline driving)
//    - Reads stored GPX files from LittleFS via TrackLogger
//    - Parses each <trkpt> and uploads to Traccar with original timestamps
//    - Uploads BATCH_POINTS_PER_LOOP (5) points per loop() call to avoid blocking
//    - Deletes file after all points successfully uploaded
//    - On failure, pauses for BATCH_RETRY_INTERVAL (60s) before retrying
//

extern Logger logger;

void TraccarUploader::init(TrackLogger *_trackLogger)
{
    trackLoggerPtr = _trackLogger;
    logger.log(INFO, "TraccarUploader ready -> %s:%d id=%s", TRACCAR_HOST, TRACCAR_PORT, TRACCAR_DEVICE_ID);
}

// ---- Live position sending ----

void TraccarUploader::sendLivePosition(float lat, float lon, float elevFeet, float speedMph, uint32_t secondsSince2000)
{
    // Skip if no valid position (0,0 = no fix yet)
    if (lat == 0 || lon == 0)
        return;

    // Throttle to LIVE_SEND_INTERVAL to avoid flooding the server
    // and consuming WiFi bandwidth needed for OTA, logging, etc.
    if (millis() < nextLiveSend)
        return;
    nextLiveSend = millis() + LIVE_SEND_INTERVAL;

    // Convert units: Traccar/OsmAnd expects meters and knots
    float elevMeters = elevFeet * 0.3048;
    float speedKnots = speedMph * 0.868976;
    
    // Convert our RTC time (seconds since 2000) to Unix timestamp
    // (seconds since 1970) which is what Traccar expects
    uint32_t unixTs = secondsSince2000 + SECONDS_FROM_1970_TO_2000;

    if (sendToTraccar(lat, lon, elevMeters, speedKnots, unixTs))
    {
        uploadedCount++;
    }
    else
    {
        failedCount++;
    }
}

// ---- Batch upload of stored GPX files ----
// Called periodically from the main loop when WiFi is connected.
// Reads trackpoints from previously-recorded GPX files on LittleFS
// and replays them to Traccar with their original timestamps.
// This backfills the Traccar history with positions recorded while offline.
// Only processes BATCH_POINTS_PER_LOOP points per call to keep the main
// loop responsive (display updates, CAN reads, etc. must not stall).

void TraccarUploader::uploadBuffered()
{
    // Don't retry too frequently on errors
    if (millis() < nextBatchRetry)
        return;

    int fileCount = trackLoggerPtr->getFileCount();
    if (fileCount == 0)
        return;

    // Process a few points per call to avoid blocking the main loop
    int pointsSent = 0;

    for (int fi = 0; fi < fileCount && pointsSent < BATCH_POINTS_PER_LOOP; fi++)
    {
        String filename = trackLoggerPtr->getFileName(fi);
        if (filename.length() == 0)
            continue;

        String fullPath = String(TRACK_DIR) + "/" + filename;
        File f = LittleFS.open(fullPath, "r");
        if (!f)
            continue;

        // Parse each <trkpt> line and upload
        bool allSent = true;
        int lineNum = 0;

        while (f.available() && pointsSent < BATCH_POINTS_PER_LOOP)
        {
            String line = f.readStringUntil('\n');
            lineNum++;

            // Skip lines we've already processed
            if (lineNum <= currentBatchLineIndex && fi == currentBatchFileIndex)
                continue;

            // Only process trackpoint lines
            if (line.indexOf("<trkpt") < 0)
                continue;

            // Parse lat and lon from: <trkpt lat="47.606000" lon="-122.332000">
            float lat = 0, lon = 0, ele = 0, spd = 0;
            uint32_t ts = 0;

            int latIdx = line.indexOf("lat=\"");
            int lonIdx = line.indexOf("lon=\"");
            if (latIdx >= 0 && lonIdx >= 0)
            {
                lat = line.substring(latIdx + 5, line.indexOf("\"", latIdx + 5)).toFloat();
                lon = line.substring(lonIdx + 5, line.indexOf("\"", lonIdx + 5)).toFloat();
            }

            // Parse elevation: <ele>56.4</ele>
            int eleIdx = line.indexOf("<ele>");
            if (eleIdx >= 0)
            {
                ele = line.substring(eleIdx + 5, line.indexOf("</ele>")).toFloat();
            }

            // Parse speed: <speed>29.1</speed> (already in m/s)
            int spdIdx = line.indexOf("<speed>");
            if (spdIdx >= 0)
            {
                float speedMs = line.substring(spdIdx + 7, line.indexOf("</speed>")).toFloat();
                spd = speedMs * 1.94384;  // m/s to knots
            }

            // Parse time: <time>2026-02-18T14:30:00Z</time>
            int timeIdx = line.indexOf("<time>");
            if (timeIdx >= 0)
            {
                String timeStr = line.substring(timeIdx + 6, line.indexOf("</time>"));
                // Parse ISO 8601: YYYY-MM-DDTHH:MM:SSZ
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
                if (sendToTraccar(lat, lon, ele, spd, ts))
                {
                    uploadedCount++;
                    pointsSent++;
                }
                else
                {
                    // Upload failed - pause and retry later
                    allSent = false;
                    currentBatchFileIndex = fi;
                    currentBatchLineIndex = lineNum;
                    nextBatchRetry = millis() + BATCH_RETRY_INTERVAL;
                    f.close();
                    return;
                }
            }
        }

        f.close();

        // If we processed the whole file, delete it
        if (allSent && !f.available())
        {
            trackLoggerPtr->deleteFile(filename);
            logger.log(INFO, "Uploaded and deleted track: %s", filename.c_str());
            currentBatchLineIndex = 0;  // reset for next file
        }
    }
}

// ---- Send a single point to Traccar via OsmAnd HTTP protocol ----
// Returns true on HTTP 200, false on any error.
// Uses a 5-second timeout to prevent blocking if the server is unreachable.
// The OsmAnd protocol is fire-and-forget — Traccar responds with 200 OK
// and stores the position.  No session or authentication needed.

bool TraccarUploader::sendToTraccar(float lat, float lon, float elevMeters, float speedKnots, uint32_t unixTimestamp)
{
    HTTPClient http;

    // Build OsmAnd protocol URL
    char url[300];
    snprintf(url, sizeof(url),
             "http://%s:%d/?id=%s&lat=%.6f&lon=%.6f&altitude=%.1f&speed=%.1f&timestamp=%lu",
             TRACCAR_HOST, TRACCAR_PORT, TRACCAR_DEVICE_ID,
             lat, lon, elevMeters, speedKnots, (unsigned long)unixTimestamp);

    http.begin(url);
    http.setTimeout(5000);  // 5 second timeout
    int httpCode = http.GET();
    http.end();

    if (httpCode == 200)
    {
        return true;
    }
    else
    {
        logger.log(WARNING, "Traccar upload failed: HTTP %d", httpCode);
        return false;
    }
}

bool TraccarUploader::isUploading()
{
    return batchInProgress;
}

int TraccarUploader::getUploadedCount()
{
    return uploadedCount;
}

int TraccarUploader::getFailedCount()
{
    return failedCount;
}
