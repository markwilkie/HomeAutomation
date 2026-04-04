#ifndef TraccarUploader_h
#define TraccarUploader_h

#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "TrackLogger.h"
#include "../logging/logger.h"

/*
  TraccarUploader - Sends GPS positions to a Traccar server using the OsmAnd protocol.
  
  Two modes of operation:
  1. Live: When WiFi is available, sends current position immediately via HTTP GET
  2. Batch: When WiFi returns, uploads buffered GPX files point-by-point, then deletes them
  
  OsmAnd protocol is a simple HTTP GET:
    http://server:5055/?id=DEVICE_ID&lat=47.606&lon=-122.332&altitude=56&speed=65&timestamp=1708272600

  Live and batch sends are non-blocking: the main loop enqueues requests,
  and a FreeRTOS task on Core 0 drains the queue and does the actual HTTP calls.
  Ignition events (trip start/end) are sent synchronously so they are guaranteed.
*/

// ---- Traccar credentials defined in secrets.h ----

#define LIVE_SEND_INTERVAL  30000       // Send live position every 30 seconds when WiFi available
#define BATCH_POINTS_PER_LOOP 5         // Upload this many buffered points per loop() call (to not block)
#define BATCH_RETRY_INTERVAL  60000     // Retry batch upload every 60s if errors occur
#define TRACCAR_QUEUE_SIZE    20        // Max queued async requests before dropping

// A single position request to be sent to Traccar in the background
struct TraccarRequest {
    float lat;
    float lon;
    float elevMeters;
    float speedKnots;
    uint32_t unixTimestamp;
    int ignition;       // -1 = omit, 0 = off, 1 = on
};

class TraccarUploader
{
public:
    void init(TrackLogger *trackLogger);
    
    // Send live position (called every loop when WiFi is up) — non-blocking
    void sendLivePosition(float lat, float lon, float elevFeet, float speedMph, uint32_t secondsSince2000);
    
    // Upload buffered files (called periodically when WiFi is up) — non-blocking
    void uploadBuffered();
    
    // Status
    bool isUploading();
    int  getUploadedCount();
    int  getFailedCount();

    // Send ignition on/off event to Traccar (synchronous — guaranteed delivery)
    void sendIgnitionEvent(bool ignitionOn, float lat, float lon, float elevFeet, float speedMph, uint32_t secondsSince2000);

private:
    TrackLogger *trackLoggerPtr = nullptr;
    
    // Synchronous HTTP send (used by background task and ignition events)
    bool sendToTraccar(float lat, float lon, float elevMeters, float speedKnots, uint32_t unixTimestamp, int ignition = -1);
    
    // Enqueue a request for the background task (non-blocking, drops if full)
    bool enqueue(float lat, float lon, float elevMeters, float speedKnots, uint32_t unixTimestamp, int ignition = -1);

    // FreeRTOS background task — runs on Core 0
    static void backgroundTask(void* param);
    QueueHandle_t _requestQueue = nullptr;
    TaskHandle_t  _taskHandle = nullptr;

    // Timing
    unsigned long nextLiveSend = 0;
    unsigned long nextBatchRetry = 0;
    
    // Batch upload state
    bool batchInProgress = false;
    int  currentBatchFileIndex = 0;
    String currentBatchFileName;
    int  currentBatchLineIndex = 0;
    
    // Stats (updated from background task — minor races OK for display counters)
    int uploadedCount = 0;
    int failedCount = 0;
};

#endif
