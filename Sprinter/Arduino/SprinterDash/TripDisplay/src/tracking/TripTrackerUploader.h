#ifndef TripTrackerUploader_h
#define TripTrackerUploader_h

#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "TrackLogger.h"
#include "../logging/logger.h"

/*
  TripTrackerUploader - Sends GPS + sensor telemetry directly to TripTracker's
  own ingestion API (POST /api/v1/positions and /api/v1/positions/batch),
  authenticated with a per-device X-Device-Key header. Reached over a
  Tailscale tunnel (the van's GL-iNet router is a tailnet node) rather than a
  public address - see secrets.h and TripTracker's own README.

  Replaces the old Traccar/OsmAnd uploader entirely: TripTracker does its own
  complete home-geofence trip detection from the raw position stream, so the
  firmware doesn't need to track trip boundaries or send ignition events at
  all anymore - every point is just reported as it comes in, tagged with the
  actual physical ignition state (stored, not used for trip detection).

  Two operating modes:
  1. Live: current position sent via a non-blocking background queue+task,
     same architecture as before.
  2. Batch: previously-buffered GPX files (recorded while WiFi/the tunnel was
     down) are replayed as JSON array POSTs to /positions/batch once
     connectivity returns - one request per chunk rather than one request per
     point, since our own ingestion API accepts up to 500 points per call.
*/

#define LIVE_SEND_INTERVAL      30000    // Send live position every 30 seconds when connected
#define BATCH_POINTS_PER_UPLOAD 20        // Points per buffered-replay POST (keeps each main-loop call bounded)
#define BATCH_RETRY_INTERVAL    60000     // Retry batch upload every 60s if errors occur
#define TRIPTRACKER_QUEUE_SIZE  20        // Max queued async live-send requests before dropping

// A single live position request to be sent in the background
struct PositionRequest {
    float lat;
    float lon;
    float elevMeters;
    float speedMps;
    uint32_t unixTimestamp;
    int ignition;       // -1 = omit, 0 = off, 1 = on
    float engineMiles;  // -1 = omit (not yet read from CAN this session)
};

class TripTrackerUploader
{
public:
    void init(TrackLogger *trackLogger);

    // Send live position (called every loop when connected) - non-blocking.
    void sendLivePosition(float lat, float lon, float elevFeet, float speedMph, uint32_t secondsSince2000, bool ignitionOn, float engineMiles = -1);

    // Upload buffered files (called periodically when connected) - blocking
    // for the duration of one batch POST, called from the main loop same as
    // the old per-point version was.
    void uploadBuffered();

    // Status
    bool isUploading();
    int  getUploadedCount();
    int  getFailedCount();

private:
    TrackLogger *trackLoggerPtr = nullptr;

    // Enqueue a live-position request for the background task (non-blocking, drops if full)
    bool enqueue(float lat, float lon, float elevMeters, float speedMps, uint32_t unixTimestamp, int ignition, float engineMiles);

    // FreeRTOS background task - runs on Core 0, drains the live-position queue
    static void backgroundTask(void* param);
    QueueHandle_t _requestQueue = nullptr;
    TaskHandle_t  _taskHandle = nullptr;

    // Synchronous HTTP POST of a single live position (used by the background task)
    bool sendPosition(const PositionRequest &req);

    // Timing
    unsigned long nextLiveSend = 0;
    unsigned long nextBatchRetry = 0;

    // Batch upload state
    bool batchInProgress = false;
    int  currentBatchFileIndex = 0;
    String currentBatchFileName;
    int  currentBatchLineIndex = 0;

    // Stats (updated from background task - minor races OK for display counters)
    int uploadedCount = 0;
    int failedCount = 0;
};

#endif
