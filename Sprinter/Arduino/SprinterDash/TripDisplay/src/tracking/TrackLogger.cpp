#include <Arduino.h>
#include "TrackLogger.h"
#include "RTClib.h"
#include "../Globals.h"

//
// TrackLogger.cpp - GPS track recording to LittleFS as GPX files
//
// File strategy:
//   - One GPX file per calendar day: /tracks/YYYY-MM-DD.gpx
//   - Trackpoints are appended without a closing </gpx> footer
//   - The footer is added dynamically when the file is served via HTTP
//   - This avoids the cost of seeking/rewriting the footer on every append
//
// Space management strategy:
//   When LittleFS approaches capacity, older files are "thinned" by
//   removing interior trackpoints while preserving the first and last N.
//   This keeps trip start/end at full resolution and degrades gracefully.
//   Two thresholds:
//     85% full → keep every 2nd middle point (halves file size)
//     95% full → keep every 4th middle point (quarters file size)
//   The current day's file is never thinned.
//

void TrackLogger::init()
{
    if (!LittleFS.begin(true))  // true = format on first use
    {
        logger.log(ERROR, "LittleFS mount failed!");
        ready = false;
        return;
    }

    // Create tracks directory if it doesn't exist
    if (!LittleFS.exists(TRACK_DIR))
    {
        LittleFS.mkdir(TRACK_DIR);
    }

    ready = true;
    logger.log(INFO, "TrackLogger ready. Used: %d/%d bytes (%f%%)", 
               getUsedBytes(), getTotalBytes(), getUsagePercent());
}

bool TrackLogger::isReady()
{
    return ready;
}

void TrackLogger::logPoint(float lat, float lon, float elevFeet, float speedMph, uint32_t secondsSince2000)
{
    // Guard: don't log if filesystem isn't mounted or position is clearly invalid
    // (0,0 coordinates = Null Island in the Gulf of Guinea — not a real position)
    if (!ready || lat == 0 || lon == 0)
        return;

    // Throttle writes to TRACK_INTERVAL_MS (default 5 seconds)
    // Flash has limited write endurance (~100K cycles per sector), and LittleFS
    // wear-levels, but we still want to be conservative
    if (millis() < nextLogTime)
        return;
    nextLogTime = millis() + TRACK_INTERVAL_MS;

    // Check if we need a new file (new day)
    int today = dayOfYear(secondsSince2000);
    if (today != currentDay)
    {
        currentDay = today;
        openNewFile(secondsSince2000);
    }

    // Append trackpoint to current file
    File f = LittleFS.open(currentFileName, "r+");  // read+write to preserve header
    if (!f)
    {
        // File may not exist yet (shouldn't happen, but recover)
        openNewFile(secondsSince2000);
        f = LittleFS.open(currentFileName, "r+");
        if (!f)
        {
            logger.log(ERROR, "Cannot open track file for writing");
            return;
        }
    }

    // Seek to just before the GPX footer to insert new point
    // We'll use a simpler approach: write without footer, add footer at download time
    f.seek(0, SeekEnd);
    writeTrackpoint(f, lat, lon, elevFeet, speedMph, secondsSince2000);
    f.close();

    // Periodically check storage
    manageStorage();
}

void TrackLogger::openNewFile(uint32_t secondsSince2000)
{
    currentFileName = String(TRACK_DIR) + "/" + buildDateString(secondsSince2000) + ".gpx";
    
    // Only create if it doesn't exist
    if (!LittleFS.exists(currentFileName))
    {
        File f = LittleFS.open(currentFileName, "w");
        if (f)
        {
            f.print(GPX_HEADER);
            f.close();
            logger.log(INFO, "New track file: %s", currentFileName.c_str());
        }
    }
}

void TrackLogger::writeTrackpoint(File &f, float lat, float lon, float elevFeet, float speedMph, uint32_t secondsSince2000)
{
    // GPX standard uses meters for elevation and m/s for speed
    // Our inputs are in feet (from barometer) and mph (from OBD)
    float elevMeters = elevFeet * 0.3048;
    float speedMs = speedMph * 0.44704;

    String ts = formatTimestamp(secondsSince2000);
    
    char buf[200];
    snprintf(buf, sizeof(buf), TRKPT_FMT, 
             lat, lon, elevMeters, ts.c_str(), speedMs);
    f.print(buf);
}

// Format: 2026-02-18T14:30:00Z
String TrackLogger::formatTimestamp(uint32_t secondsSince2000)
{
    DateTime dt(secondsSince2000 + SECONDS_FROM_1970_TO_2000);
    char buf[25];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             dt.year(), dt.month(), dt.day(),
             dt.hour(), dt.minute(), dt.second());
    return String(buf);
}

// Returns "2026-02-18" style string
String TrackLogger::buildDateString(uint32_t secondsSince2000)
{
    DateTime dt(secondsSince2000 + SECONDS_FROM_1970_TO_2000);
    char buf[12];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", dt.year(), dt.month(), dt.day());
    return String(buf);
}

int TrackLogger::dayOfYear(uint32_t secondsSince2000)
{
    DateTime dt(secondsSince2000 + SECONDS_FROM_1970_TO_2000);
    // Simple unique day number: year*366 + dayOfYear approximation
    return dt.year() * 400 + dt.month() * 32 + dt.day();
}

// ---- File management for HTTP serving ----

int TrackLogger::getFileCount()
{
    if (!ready) return 0;
    int count = 0;
    File root = LittleFS.open(TRACK_DIR);
    if (!root) return 0;
    File f = root.openNextFile();
    while (f)
    {
        count++;
        f = root.openNextFile();
    }
    return count;
}

String TrackLogger::getFileName(int index)
{
    if (!ready) return "";
    File root = LittleFS.open(TRACK_DIR);
    if (!root) return "";
    int count = 0;
    File f = root.openNextFile();
    while (f)
    {
        if (count == index)
        {
            String name = f.name();
            return name;
        }
        count++;
        f = root.openNextFile();
    }
    return "";
}

// Returns file contents with GPX footer appended.
// During logging, we omit the closing </trkseg></trk></gpx> tags so that
// we can simply append trackpoints without seeking.  The footer is added
// here when the file is served for download, making it valid GPX XML.
String TrackLogger::getFileContents(const String &filename)
{
    String fullPath = String(TRACK_DIR) + "/" + filename;
    File f = LittleFS.open(fullPath, "r");
    if (!f)
        return "";

    String content = f.readString();
    f.close();

    // Append the GPX footer for a valid file
    content += GPX_FOOTER;
    return content;
}

bool TrackLogger::deleteFile(const String &filename)
{
    String fullPath = String(TRACK_DIR) + "/" + filename;
    return LittleFS.remove(fullPath);
}

void TrackLogger::deleteAllFiles()
{
    File root = LittleFS.open(TRACK_DIR);
    if (!root) return;
    File f = root.openNextFile();
    while (f)
    {
        String path = String(TRACK_DIR) + "/" + f.name();
        f = root.openNextFile();  // advance before deleting
        LittleFS.remove(path);
    }
    logger.log(INFO, "All track files deleted");
}

// ---- Storage info ----

size_t TrackLogger::getTotalBytes()
{
    return LittleFS.totalBytes();
}

size_t TrackLogger::getUsedBytes()
{
    return LittleFS.usedBytes();
}

float TrackLogger::getUsagePercent()
{
    if (getTotalBytes() == 0) return 0;
    return (float)getUsedBytes() / (float)getTotalBytes() * 100.0;
}

// ---- Space management: thin middle points while preserving start/end ----

void TrackLogger::manageStorage()
{
    float usage = getUsagePercent();

    if (usage < FS_WARN_THRESHOLD * 100)
        return;  // plenty of space

    logger.log(WARNING, "LittleFS at %f%% - thinning tracks", usage);

    // Determine thinning aggressiveness
    int keepEveryN = (usage >= FS_CRITICAL_THRESH * 100) ? 4 : 2;

    // Thin all files EXCEPT the current one
    File root = LittleFS.open(TRACK_DIR);
    if (!root) return;
    File f = root.openNextFile();
    while (f)
    {
        String name = f.name();
        String fullPath = String(TRACK_DIR) + "/" + name;
        f = root.openNextFile();  // advance before modifying

        // Don't thin the file we're currently writing to
        if (fullPath == currentFileName)
            continue;

        thinFile(fullPath, keepEveryN);
    }

    logger.log(INFO, "After thinning: %d/%d bytes (%f%%)", 
               getUsedBytes(), getTotalBytes(), getUsagePercent());
}

/*
  Thin a GPX file by keeping every Nth trackpoint in the middle,
  while preserving the first and last PRESERVE_ENDPOINTS points intact.
  
  This means:
  - Start of trip: full resolution (first 20 points)
  - Middle of trip: reduced to every Nth point
  - End of trip: full resolution (last 20 points)
*/
void TrackLogger::thinFile(const String &filepath, int keepEveryN)
{
    File f = LittleFS.open(filepath, "r");
    if (!f) return;

    // Read all lines into a temporary structure
    // We'll identify <trkpt> lines and selectively keep them
    String tempPath = filepath + ".tmp";
    File tmp = LittleFS.open(tempPath, "w");
    if (!tmp)
    {
        f.close();
        return;
    }

    // First pass: count total trackpoints
    int totalPoints = 0;
    while (f.available())
    {
        String line = f.readStringUntil('\n');
        if (line.indexOf("<trkpt") >= 0)
            totalPoints++;
    }

    // If not many points, nothing worth thinning
    if (totalPoints <= PRESERVE_ENDPOINTS * 2 + keepEveryN)
    {
        f.close();
        tmp.close();
        LittleFS.remove(tempPath);
        return;
    }

    // Second pass: copy with thinning
    f.seek(0);
    int pointIndex = 0;
    int middleStart = PRESERVE_ENDPOINTS;
    int middleEnd = totalPoints - PRESERVE_ENDPOINTS;
    int middleCount = 0;

    while (f.available())
    {
        String line = f.readStringUntil('\n');

        if (line.indexOf("<trkpt") >= 0)
        {
            bool keep = false;

            if (pointIndex < middleStart)
            {
                keep = true;  // preserve start
            }
            else if (pointIndex >= middleEnd)
            {
                keep = true;  // preserve end
            }
            else
            {
                // Middle section: keep every Nth
                keep = (middleCount % keepEveryN == 0);
                middleCount++;
            }

            if (keep)
            {
                tmp.println(line);
            }
            pointIndex++;
        }
        else
        {
            // Non-trackpoint lines (header etc.) - always keep
            tmp.println(line);
        }
    }

    f.close();
    tmp.close();

    // Replace old file with thinned version
    LittleFS.remove(filepath);
    LittleFS.rename(tempPath, filepath);

    logger.log(INFO, "Thinned %s: %d -> ~%d points (keep every %d)", 
               filepath.c_str(), totalPoints, 
               PRESERVE_ENDPOINTS * 2 + (middleEnd - middleStart) / keepEveryN,
               keepEveryN);
}

int TrackLogger::countLines(const String &filepath)
{
    File f = LittleFS.open(filepath, "r");
    if (!f) return 0;
    int count = 0;
    while (f.available())
    {
        f.readStringUntil('\n');
        count++;
    }
    f.close();
    return count;
}
