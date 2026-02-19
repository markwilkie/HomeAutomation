#ifndef TraccarUploader_h
#define TraccarUploader_h

#include <HTTPClient.h>
#include <WiFi.h>
#include "TrackLogger.h"
#include "../logging/logger.h"

/*
  TraccarUploader - Sends GPS positions to a Traccar server using the OsmAnd protocol.
  
  Two modes of operation:
  1. Live: When WiFi is available, sends current position immediately via HTTP GET
  2. Batch: When WiFi returns, uploads buffered GPX files point-by-point, then deletes them
  
  OsmAnd protocol is a simple HTTP GET:
    http://server:5055/?id=DEVICE_ID&lat=47.606&lon=-122.332&altitude=56&speed=65&timestamp=1708272600
*/

// ---- Configure these for your server ----
#define TRACCAR_HOST        "sprinter-traccar.westus2.azurecontainer.io"
#define TRACCAR_PORT        5055
#define TRACCAR_DEVICE_ID   "sprinter01"

#define LIVE_SEND_INTERVAL  10000       // Send live position every 10 seconds when WiFi available
#define BATCH_POINTS_PER_LOOP 5         // Upload this many buffered points per loop() call (to not block)
#define BATCH_RETRY_INTERVAL  60000     // Retry batch upload every 60s if errors occur

class TraccarUploader
{
public:
    void init(TrackLogger *trackLogger);
    
    // Send live position (called every loop when WiFi is up)
    void sendLivePosition(float lat, float lon, float elevFeet, float speedMph, uint32_t secondsSince2000);
    
    // Upload buffered files (called periodically when WiFi is up)
    void uploadBuffered();
    
    // Status
    bool isUploading();
    int  getUploadedCount();
    int  getFailedCount();

    // Send ignition on/off event to Traccar (for trip start/end detection)
    void sendIgnitionEvent(bool ignitionOn, float lat, float lon, float elevFeet, float speedMph, uint32_t secondsSince2000);

private:
    TrackLogger *trackLoggerPtr = nullptr;
    
    bool sendToTraccar(float lat, float lon, float elevMeters, float speedKnots, uint32_t unixTimestamp, int ignition = -1);
    
    // Timing
    unsigned long nextLiveSend = 0;
    unsigned long nextBatchRetry = 0;
    
    // Batch upload state
    bool batchInProgress = false;
    int  currentBatchFileIndex = 0;
    String currentBatchFileName;
    int  currentBatchLineIndex = 0;
    
    // Stats
    int uploadedCount = 0;
    int failedCount = 0;
};

#endif
