#ifndef TrackLogger_h
#define TrackLogger_h

#include <LittleFS.h>
#include "logger.h"

/*
  TrackLogger - Logs GPS trackpoints to LittleFS as GPX files.
  
  Strategy:
  - One GPX file per day (e.g., /tracks/2026-02-18.gpx)
  - Writes trackpoints at a configurable interval (default 5s)
  - Uses barometric altitude (more accurate) with GPS altitude as fallback
  - When LittleFS is nearing capacity, "thins" older points in the middle
    of each file while preserving the first and last N points. This keeps 
    trip start/end intact and reduces interior resolution proportionally.
  - Files are served via HTTP for download and uploadable to Traccar.
  
  Storage math (1.5MB LittleFS):
    ~100 bytes per trackpoint
    ~15,000 points = ~20 hours of driving at 5s intervals
*/

#define TRACK_DIR           "/tracks"
#define TRACK_INTERVAL_MS   5000       // Log a point every 5 seconds
#define FS_WARN_THRESHOLD   0.85       // Start thinning at 85% capacity
#define FS_CRITICAL_THRESH  0.95       // More aggressive thinning at 95%
#define PRESERVE_ENDPOINTS  20         // Keep this many points at start and end of each file
#define GPX_HEADER          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gpx version=\"1.1\" creator=\"SprinterDash\">\n<trk><name>Sprinter</name><trkseg>\n"
#define GPX_FOOTER          "</trkseg></trk>\n</gpx>\n"
#define TRKPT_FMT           "<trkpt lat=\"%.6f\" lon=\"%.6f\"><ele>%.1f</ele><time>%s</time><speed>%.1f</speed></trkpt>\n"

// Trackpoint stored in RAM before flush
struct TrackPoint
{
    float lat;
    float lon;
    float elevation;    // feet (barometric preferred)
    float speedMph;
    uint32_t timestamp; // seconds since 2000
};

class TrackLogger
{
public:
    void init();
    void logPoint(float lat, float lon, float elevFeet, float speedMph, uint32_t secondsSince2000);
    bool isReady();
    
    // File management
    int  getFileCount();
    String getFileName(int index);
    String getFileContents(const String &filename);
    bool deleteFile(const String &filename);
    void deleteAllFiles();
    
    // Storage info
    size_t getTotalBytes();
    size_t getUsedBytes();
    float  getUsagePercent();
    
    // Called periodically to manage space
    void   manageStorage();

private:
    bool ready = false;
    unsigned long nextLogTime = 0;
    String currentFileName;
    int currentDay = -1;    // day of year for file rotation

    // File operations
    void   openNewFile(uint32_t secondsSince2000);
    void   writeTrackpoint(File &f, float lat, float lon, float elevFeet, float speedMph, uint32_t secondsSince2000);
    String formatTimestamp(uint32_t secondsSince2000);
    String buildDateString(uint32_t secondsSince2000);
    int    dayOfYear(uint32_t secondsSince2000);
    
    // Space management
    void   thinFile(const String &filepath, int keepEveryN);
    int    countLines(const String &filepath);
};

#endif
